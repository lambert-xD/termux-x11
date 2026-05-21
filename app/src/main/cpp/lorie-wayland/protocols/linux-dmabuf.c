/* linux-dmabuf protocol implementation — DMA-BUF import via EGL */
#include "compositor.h"
#include "linux-dmabuf.h"
#include "renderer.h"
#include "stable-linux-dmabuf-linux-dmabuf-v1.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/* === Supported formats === */
const uint32_t lorie_dmabuf_supported_formats[] = {
    DRM_FORMAT_ABGR8888,
    DRM_FORMAT_XBGR8888,
    DRM_FORMAT_ARGB8888,
};
const int lorie_dmabuf_num_supported_formats =
    sizeof(lorie_dmabuf_supported_formats) / sizeof(lorie_dmabuf_supported_formats[0]);

int lorie_dmabuf_format_supported(uint32_t format) {
    for (int i = 0; i < lorie_dmabuf_num_supported_formats; i++) {
        if (lorie_dmabuf_supported_formats[i] == format)
            return 1;
    }
    return 0;
}

/* === Internal state === */
struct lorie_buffer_params {
    struct wl_resource *resource;
    struct lorie_compositor *compositor;
    struct lorie_dmabuf_plane planes[4];
    int used[4];
    int num_planes;
};

/* === Validation helper (exposed for tests) === */
int lorie_dmabuf_params_validate(int32_t width, int32_t height, uint32_t format,
                                 int num_planes, struct lorie_dmabuf_plane *planes) {
    if (width <= 0 || height <= 0)
        return ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS;

    if (!lorie_dmabuf_format_supported(format))
        return ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT;

    if (num_planes <= 0 || !planes)
        return ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE;

    int valid_planes = 0;
    for (int i = 0; i < num_planes; i++) {
        if (planes[i].fd >= 0)
            valid_planes++;
    }
    if (valid_planes == 0)
        return ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE;

    return 0;
}

/* Forward declaration */
static void dmabuf_buffer_resource_destroy(struct wl_resource *resource);

/* === Buffer import (exposed for tests) === */
struct lorie_dmabuf_buffer *lorie_dmabuf_buffer_import(
    struct wl_resource *buffer_resource,
    int32_t width, int32_t height, uint32_t format,
    int num_planes, struct lorie_dmabuf_plane *planes,
    void *egl_display,
    void *egl_create_image_khr,
    void *egl_destroy_image_khr,
    void *gl_egl_image_target_texture2d_oes) {

    (void)egl_destroy_image_khr;

    /* NULL EGL display means we can't import; return NULL so caller can post failed */
    if (!egl_display || !egl_create_image_khr || !gl_egl_image_target_texture2d_oes)
        return NULL;

    PFNEGLCREATEIMAGEKHRPROC create_image =
        (PFNEGLCREATEIMAGEKHRPROC)egl_create_image_khr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC target_texture =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)gl_egl_image_target_texture2d_oes;

    EGLint attribs[64];
    int a = 0;

    attribs[a++] = EGL_LINUX_DRM_FOURCC_EXT;
    attribs[a++] = (EGLint)format;
    attribs[a++] = EGL_WIDTH;
    attribs[a++] = width;
    attribs[a++] = EGL_HEIGHT;
    attribs[a++] = height;

    static const EGLint fd_attr[4] = {
        EGL_DMA_BUF_PLANE0_FD_EXT,
        EGL_DMA_BUF_PLANE1_FD_EXT,
        EGL_DMA_BUF_PLANE2_FD_EXT,
        EGL_DMA_BUF_PLANE3_FD_EXT,
    };
    static const EGLint offset_attr[4] = {
        EGL_DMA_BUF_PLANE0_OFFSET_EXT,
        EGL_DMA_BUF_PLANE1_OFFSET_EXT,
        EGL_DMA_BUF_PLANE2_OFFSET_EXT,
        EGL_DMA_BUF_PLANE3_OFFSET_EXT,
    };
    static const EGLint pitch_attr[4] = {
        EGL_DMA_BUF_PLANE0_PITCH_EXT,
        EGL_DMA_BUF_PLANE1_PITCH_EXT,
        EGL_DMA_BUF_PLANE2_PITCH_EXT,
        EGL_DMA_BUF_PLANE3_PITCH_EXT,
    };

    for (int i = 0; i < num_planes && i < 4; i++) {
        if (planes[i].fd < 0)
            continue;
        attribs[a++] = fd_attr[i];
        attribs[a++] = planes[i].fd;
        attribs[a++] = offset_attr[i];
        attribs[a++] = (EGLint)planes[i].offset;
        attribs[a++] = pitch_attr[i];
        attribs[a++] = (EGLint)planes[i].stride;
    }

    attribs[a++] = EGL_NONE;

    EGLImageKHR image = create_image(
        (EGLDisplay)egl_display, EGL_NO_CONTEXT,
        EGL_LINUX_DMA_BUF_EXT, NULL, attribs);

    /* Close all plane FDs immediately after import attempt */
    for (int i = 0; i < num_planes && i < 4; i++) {
        if (planes[i].fd >= 0) {
            close(planes[i].fd);
            planes[i].fd = -1;
        }
    }

    if (image == EGL_NO_IMAGE_KHR)
        return NULL;

    GLuint texture_id = 0;
    glGenTextures(1, &texture_id);
    if (texture_id == 0) {
        PFNEGLDESTROYIMAGEKHRPROC destroy_image =
            (PFNEGLDESTROYIMAGEKHRPROC)egl_destroy_image_khr;
        if (destroy_image)
            destroy_image((EGLDisplay)egl_display, image);
        return NULL;
    }

    glBindTexture(GL_TEXTURE_2D, texture_id);
    target_texture(GL_TEXTURE_2D, image);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    struct lorie_dmabuf_buffer *buf = calloc(1, sizeof(*buf));
    if (!buf) {
        PFNEGLDESTROYIMAGEKHRPROC destroy_image =
            (PFNEGLDESTROYIMAGEKHRPROC)egl_destroy_image_khr;
        if (destroy_image)
            destroy_image((EGLDisplay)egl_display, image);
        glDeleteTextures(1, &texture_id);
        return NULL;
    }

    buf->buffer_resource = buffer_resource;
    buf->egl_image = image;
    buf->texture_id = texture_id;
    buf->width = width;
    buf->height = height;
    buf->format = format;
    buf->num_planes = num_planes;
    for (int i = 0; i < num_planes && i < 4; i++) {
        buf->planes[i] = planes[i];
    }
    buf->imported = 1;
    return buf;
}

void lorie_dmabuf_buffer_destroy(struct lorie_dmabuf_buffer *buf,
                                 void *egl_display,
                                 void *egl_destroy_image_khr) {
    if (!buf)
        return;
    /* Use renderer from buf if egl_display not provided directly */
    if (!egl_display && buf->renderer) {
        egl_display = (void*)lorie_renderer_egl_display(buf->renderer);
    }
    if (!egl_destroy_image_khr && buf->renderer) {
        egl_destroy_image_khr = lorie_renderer_egl_destroy_image_khr(buf->renderer);
    }
    if (buf->texture_id) {
        glDeleteTextures(1, &buf->texture_id);
    }
    if (buf->egl_image && egl_display && egl_destroy_image_khr) {
        PFNEGLDESTROYIMAGEKHRPROC destroy =
            (PFNEGLDESTROYIMAGEKHRPROC)egl_destroy_image_khr;
        destroy((EGLDisplay)egl_display, (EGLImageKHR)buf->egl_image);
    }
    free(buf);
}

/* === Protocol callbacks === */

static void params_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static void params_add(struct wl_client *client, struct wl_resource *resource,
                       int32_t fd, uint32_t plane_idx, uint32_t offset,
                       uint32_t stride, uint32_t modifier_hi, uint32_t modifier_lo) {
    struct lorie_buffer_params *params = wl_resource_get_user_data(resource);
    if (plane_idx >= 4) {
        wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX,
                               "plane_idx out of range");
        close(fd);
        return;
    }
    if (params->used[plane_idx]) {
        wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET,
                               "plane already set");
        close(params->planes[plane_idx].fd);
        close(fd);
        return;
    }
    params->planes[plane_idx].fd = fd;
    params->planes[plane_idx].offset = offset;
    params->planes[plane_idx].stride = stride;
    params->planes[plane_idx].modifier = ((uint64_t)modifier_hi << 32) | modifier_lo;
    params->used[plane_idx] = 1;
    params->num_planes++;
    (void)client;
}

static void do_params_create(struct wl_client *client, struct wl_resource *resource,
                             int32_t width, int32_t height, uint32_t format,
                             uint32_t flags, uint32_t buffer_id, int immed) {
    struct lorie_buffer_params *params = wl_resource_get_user_data(resource);
    struct lorie_compositor *compositor = params ? params->compositor : NULL;
    (void)flags;

    /* Simple validation */
    int err = lorie_dmabuf_params_validate(width, height, format, params->num_planes, params->planes);
    if (err != 0) {
        for (int i = 0; i < 4; i++) {
            if (params->used[i]) close(params->planes[i].fd);
        }
        wl_resource_post_error(resource, (uint32_t)err, "invalid dmabuf params");
        wl_resource_destroy(resource);
        return;
    }

    /* If no renderer or no dmabuf support, fail the import */
    struct lorie_renderer *renderer = compositor ? compositor->renderer : NULL;
    if (!renderer || !lorie_renderer_has_dmabuf_import(renderer)) {
        for (int i = 0; i < 4; i++) {
            if (params->used[i]) close(params->planes[i].fd);
        }
        if (immed) {
            struct wl_resource *buffer = wl_resource_create(client, &wl_buffer_interface, 1, buffer_id);
            if (buffer) wl_resource_set_implementation(buffer, NULL, NULL, NULL);
            zwp_linux_buffer_params_v1_send_failed(resource);
        } else {
            zwp_linux_buffer_params_v1_send_failed(resource);
        }
        wl_resource_destroy(resource);
        return;
    }

    /* Import via renderer */
    struct wl_resource *buffer = NULL;
    if (immed) {
        buffer = wl_resource_create(client, &wl_buffer_interface, 1, buffer_id);
        if (!buffer) {
            for (int i = 0; i < 4; i++) {
                if (params->used[i]) close(params->planes[i].fd);
            }
            wl_client_post_no_memory(client);
            wl_resource_destroy(resource);
            return;
        }
    } else {
        buffer = wl_resource_create(client, &wl_buffer_interface, 1, 0);
        if (!buffer) {
            for (int i = 0; i < 4; i++) {
                if (params->used[i]) close(params->planes[i].fd);
            }
            wl_client_post_no_memory(client);
            wl_resource_destroy(resource);
            return;
        }
    }

    struct lorie_dmabuf_buffer *dmabuf = lorie_dmabuf_buffer_import(
        buffer, width, height, format, params->num_planes, params->planes,
        (void*)lorie_renderer_egl_display(renderer),
        lorie_renderer_egl_create_image_khr(renderer),
        lorie_renderer_egl_destroy_image_khr(renderer),
        lorie_renderer_gl_egl_image_target_texture2d_oes(renderer));

    if (!dmabuf) {
        wl_resource_destroy(buffer);
        zwp_linux_buffer_params_v1_send_failed(resource);
        wl_resource_destroy(resource);
        return;
    }

    dmabuf->renderer = renderer;
    wl_resource_set_implementation(buffer, NULL, dmabuf,
        dmabuf_buffer_resource_destroy);

    if (!immed) {
        zwp_linux_buffer_params_v1_send_created(resource, buffer);
    }
    wl_resource_destroy(resource);
}

static void params_create(struct wl_client *client, struct wl_resource *resource,
                          int32_t width, int32_t height, uint32_t format, uint32_t flags) {
    do_params_create(client, resource, width, height, format, flags, 0, 0);
}

static void params_create_immed(struct wl_client *client, struct wl_resource *resource,
                                uint32_t buffer_id, int32_t width, int32_t height,
                                uint32_t format, uint32_t flags) {
    do_params_create(client, resource, width, height, format, flags, buffer_id, 1);
}

static const struct zwp_linux_buffer_params_v1_interface params_impl = {
    params_destroy,
    params_add,
    params_create,
    params_create_immed,
};

static void params_handle_destroy(struct wl_resource *resource) {
    struct lorie_buffer_params *params = wl_resource_get_user_data(resource);
    if (params) {
        for (int i = 0; i < 4; i++) {
            if (params->used[i]) close(params->planes[i].fd);
        }
        free(params);
    }
}

static void dmabuf_buffer_resource_destroy(struct wl_resource *resource) {
    struct lorie_dmabuf_buffer *buf = wl_resource_get_user_data(resource);
    if (buf) {
        lorie_dmabuf_buffer_destroy(buf,
            (void*)lorie_renderer_egl_display(buf->renderer),
            lorie_renderer_egl_destroy_image_khr(buf->renderer));
    }
}

static void linux_dmabuf_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static void linux_dmabuf_create_params(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    struct lorie_compositor *compositor = wl_resource_get_user_data(resource);
    struct lorie_buffer_params *params = calloc(1, sizeof(*params));
    if (!params) { wl_client_post_no_memory(client); return; }
    for (int i = 0; i < 4; i++) params->planes[i].fd = -1;
    params->compositor = compositor;
    params->resource = wl_resource_create(client, &zwp_linux_buffer_params_v1_interface, 1, id);
    if (!params->resource) { free(params); wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(params->resource, &params_impl, params, params_handle_destroy);
    (void)resource;
}

static void linux_dmabuf_get_default_feedback(struct wl_client *client,
                                               struct wl_resource *resource,
                                               uint32_t id) {
    struct wl_resource *feedback = wl_resource_create(client,
        &zwp_linux_dmabuf_feedback_v1_interface, 1, id);
    if (!feedback) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(feedback, NULL, NULL, NULL);
    zwp_linux_dmabuf_feedback_v1_send_done(feedback);
    (void)resource;
}

static void linux_dmabuf_get_surface_feedback(struct wl_client *client,
                                               struct wl_resource *resource,
                                               uint32_t id,
                                               struct wl_resource *surface) {
    (void)surface;
    linux_dmabuf_get_default_feedback(client, resource, id);
}

static const struct zwp_linux_dmabuf_v1_interface linux_dmabuf_impl = {
    linux_dmabuf_destroy,
    linux_dmabuf_create_params,
    linux_dmabuf_get_default_feedback,
    linux_dmabuf_get_surface_feedback,
};

static void linux_dmabuf_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
    if (!resource) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(resource, &linux_dmabuf_impl, data, NULL);

    /* Advertise supported formats */
    for (int i = 0; i < lorie_dmabuf_num_supported_formats; i++) {
        zwp_linux_dmabuf_v1_send_format(resource, lorie_dmabuf_supported_formats[i]);
    }
}

struct wl_global *lorie_linux_dmabuf_create(struct wl_display *display, struct lorie_compositor *compositor) {
    return wl_global_create(display, &zwp_linux_dmabuf_v1_interface, 4, compositor, linux_dmabuf_bind);
}
