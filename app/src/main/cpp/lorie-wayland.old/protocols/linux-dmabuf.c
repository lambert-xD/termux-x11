#include "linux-dmabuf.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <android/log.h>
#include <android/hardware_buffer.h>

#define LOG_TAG "linux-dmabuf"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* Forward declarations */
static void linux_dmabuf_destroy(struct wl_client *client, struct wl_resource *resource);
static void linux_dmabuf_create_params(struct wl_client *client, struct wl_resource *resource, uint32_t id);
static void linux_dmabuf_get_default_feedback(struct wl_client *client, struct wl_resource *resource, uint32_t id);
static void linux_dmabuf_get_surface_feedback(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource);

static const struct zwp_linux_dmabuf_v1_interface linux_dmabuf_impl = {
    .destroy = linux_dmabuf_destroy,
    .create_params = linux_dmabuf_create_params,
    .get_default_feedback = linux_dmabuf_get_default_feedback,
    .get_surface_feedback = linux_dmabuf_get_surface_feedback,
};

static void buffer_params_destroy(struct wl_client *client, struct wl_resource *resource);
static void buffer_params_add(struct wl_client *client, struct wl_resource *resource, int32_t fd, uint32_t plane_idx, uint32_t offset, uint32_t stride, uint64_t modifier);
static void buffer_params_create(struct wl_client *client, struct wl_resource *resource, int32_t width, int32_t height, uint32_t format, uint32_t flags);
static void buffer_params_create_immed(struct wl_client *client, struct wl_resource *resource, uint32_t buffer_id, int32_t width, int32_t height, uint32_t format, uint32_t flags);

static const struct zwp_linux_buffer_params_v1_interface buffer_params_impl = {
    .destroy = buffer_params_destroy,
    .add = buffer_params_add,
    .create = buffer_params_create,
    .create_immed = buffer_params_create_immed,
};

/* Resource destruction */
static void linux_dmabuf_resource_destroy(struct wl_resource *resource) {
    struct zwp_linux_dmabuf_v1 *dmabuf = wl_resource_get_user_data(resource);
    if (dmabuf) {
        /* Resource removed from list automatically */
    }
}

static void buffer_params_destroy_resource(struct wl_resource *resource) {
    struct zwp_linux_buffer_params_v1 *params = wl_resource_get_user_data(resource);
    if (params) {
        for (int i = 0; i < params->n_planes; i++) {
            if (params->planes[i].fd >= 0) {
                close(params->planes[i].fd);
            }
        }
        wl_list_remove(&params->link);
        free(params);
    }
}

/* Handlers */
static void linux_dmabuf_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void linux_dmabuf_create_params(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    struct zwp_linux_dmabuf_v1 *dmabuf = wl_resource_get_user_data(resource);
    
    struct zwp_linux_buffer_params_v1 *params = calloc(1, sizeof(*params));
    if (!params) {
        wl_resource_post_no_memory(resource);
        return;
    }

    params->resource = wl_resource_create(client, &zwp_linux_buffer_params_v1_interface, wl_resource_get_version(resource), id);
    if (!params->resource) {
        free(params);
        wl_resource_post_no_memory(resource);
        return;
    }

    for (int i = 0; i < 4; i++) {
        params->planes[i].fd = -1;
    }

    wl_resource_set_implementation(params->resource, &buffer_params_impl, params, buffer_params_destroy_resource);
    wl_list_insert(&dmabuf->compositor->dmabuf_params, &params->link);
}

static void linux_dmabuf_get_default_feedback(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    /* Simplified feedback - send supported formats */
    struct wl_resource *feedback = wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface, wl_resource_get_version(resource), id);
    if (!feedback) {
        wl_resource_post_no_memory(resource);
        return;
    }
    /* TODO: Send actual format table and feedback */
}

static void linux_dmabuf_get_surface_feedback(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource) {
    /* Same as default feedback for now */
    linux_dmabuf_get_default_feedback(client, resource, id);
}

/* Buffer params handlers */
static void buffer_params_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void buffer_params_add(struct wl_client *client, struct wl_resource *resource, int32_t fd, uint32_t plane_idx, uint32_t offset, uint32_t stride, uint64_t modifier) {
    struct zwp_linux_buffer_params_v1 *params = wl_resource_get_user_data(resource);
    
    if (plane_idx >= 4) {
        wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX, "Invalid plane index");
        close(fd);
        return;
    }

    if (params->planes[plane_idx].fd >= 0) {
        close(params->planes[plane_idx].fd);
    }

    params->planes[plane_idx].fd = fd;
    params->planes[plane_idx].offset = offset;
    params->planes[plane_idx].stride = stride;
    params->planes[plane_idx].modifier = modifier;
    if ((int)plane_idx >= params->n_planes) {
        params->n_planes = plane_idx + 1;
    }
}

static void create_buffer_common(struct wl_client *client, struct wl_resource *resource, uint32_t buffer_id, int32_t width, int32_t height, uint32_t format, uint32_t flags, int immediate) {
    struct zwp_linux_buffer_params_v1 *params = wl_resource_get_user_data(resource);
    
    if (width <= 0 || height <= 0) {
        wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS, "Invalid dimensions");
        return;
    }

    if (params->n_planes == 0) {
        wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "No planes added");
        return;
    }

    /* On Android, we can try to create an AHardwareBuffer from the FD */
    /* For now, create a wl_buffer that wraps the DMA-BUF */
    
    struct wl_resource *buffer;
    if (immediate) {
        buffer = wl_resource_create(client, &wl_buffer_interface, 1, buffer_id);
        if (!buffer) {
            wl_resource_post_no_memory(resource);
            return;
        }
    } else {
        buffer = NULL; /* Will be created and sent via event */
    }

    /* TODO: Import DMA-BUF into LorieBuffer/AHardwareBuffer */
    LOGD("Creating DMA-BUF buffer: %dx%d, format=0x%x, planes=%d", width, height, format, params->n_planes);

    if (!immediate) {
        zwp_linux_buffer_params_v1_send_created(resource, buffer_id);
    }
}

static void buffer_params_create(struct wl_client *client, struct wl_resource *resource, int32_t width, int32_t height, uint32_t format, uint32_t flags) {
    create_buffer_common(client, resource, 0, width, height, format, flags, 0);
}

static void buffer_params_create_immed(struct wl_client *client, struct wl_resource *resource, uint32_t buffer_id, int32_t width, int32_t height, uint32_t format, uint32_t flags) {
    create_buffer_common(client, resource, buffer_id, width, height, format, flags, 1);
}

/* Binding */
static void linux_dmabuf_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct zwp_linux_dmabuf_v1 *dmabuf = data;
    struct wl_resource *resource = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &linux_dmabuf_impl, dmabuf, linux_dmabuf_resource_destroy);
    wl_list_insert(&dmabuf->resources, resource); /* Simplified - should use proper list */

    /* Send supported formats */
    /* Common Android formats */
    zwp_linux_dmabuf_v1_send_format(resource, WL_SHM_FORMAT_ARGB8888);
    zwp_linux_dmabuf_v1_send_format(resource, WL_SHM_FORMAT_XRGB8888);
    zwp_linux_dmabuf_v1_send_format(resource, WL_SHM_FORMAT_ABGR8888);
    zwp_linux_dmabuf_v1_send_format(resource, WL_SHM_FORMAT_XBGR8888);
    
    /* With modifiers (DRM_FORMAT_MOD_INVALID for implicit modifier) */
    if (version >= 3) {
        zwp_linux_dmabuf_v1_send_modifier(resource, WL_SHM_FORMAT_ARGB8888, DRM_FORMAT_MOD_INVALID);
        zwp_linux_dmabuf_v1_send_modifier(resource, WL_SHM_FORMAT_XRGB8888, DRM_FORMAT_MOD_INVALID);
        zwp_linux_dmabuf_v1_send_modifier(resource, WL_SHM_FORMAT_ABGR8888, DRM_FORMAT_MOD_INVALID);
        zwp_linux_dmabuf_v1_send_modifier(resource, WL_SHM_FORMAT_XBGR8888, DRM_FORMAT_MOD_INVALID);
    }
}

/* Public API */
struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf_v1_create(struct wl_display *display, struct lorie_compositor *compositor, uint32_t version) {
    struct zwp_linux_dmabuf_v1 *dmabuf = calloc(1, sizeof(*dmabuf));
    if (!dmabuf) {
        return NULL;
    }

    dmabuf->compositor = compositor;
    dmabuf->version = version;
    wl_list_init(&dmabuf->resources);

    dmabuf->global = wl_global_create(display, &zwp_linux_dmabuf_v1_interface, version, dmabuf, linux_dmabuf_bind);
    if (!dmabuf->global) {
        free(dmabuf);
        return NULL;
    }

    LOGD("zwp_linux_dmabuf_v1 created (version %d)", version);
    return dmabuf;
}

void zwp_linux_dmabuf_v1_destroy(struct zwp_linux_dmabuf_v1 *dmabuf) {
    if (!dmabuf) return;
    wl_global_destroy(dmabuf->global);
    free(dmabuf);
}

void zwp_linux_dmabuf_v1_add_format(struct zwp_linux_dmabuf_v1 *dmabuf, uint32_t format, uint64_t modifier) {
    /* Store format for later use */
    LOGD("Adding format 0x%x modifier 0x%lx", format, (unsigned long)modifier);
}
