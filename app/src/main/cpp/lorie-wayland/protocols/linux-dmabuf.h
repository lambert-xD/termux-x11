#ifndef LORIE_LINUX_DMABUF_H
#define LORIE_LINUX_DMABUF_H

#include <stdint.h>
#include <stdbool.h>
#include <wayland-server-core.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DRM format fourcc codes (defined here if drm_fourcc.h is unavailable) */
#ifndef DRM_FORMAT_ABGR8888
#define DRM_FORMAT_ABGR8888 0x34324241 /* 'AB24' */
#endif
#ifndef DRM_FORMAT_XBGR8888
#define DRM_FORMAT_XBGR8888 0x34324258 /* 'XB24' */
#endif
#ifndef DRM_FORMAT_ARGB8888
#define DRM_FORMAT_ARGB8888 0x34325241 /* 'AR24' */
#endif

struct lorie_renderer;
struct lorie_compositor;

/* Single plane descriptor */
struct lorie_dmabuf_plane {
    int fd;
    uint32_t offset;
    uint32_t stride;
    uint64_t modifier;
};

/* Imported DMA-BUF buffer */
struct lorie_dmabuf_buffer {
    struct wl_resource *buffer_resource;
    struct lorie_renderer *renderer;
    void *egl_image;      /* EGLImageKHR stored as void* for header portability */
    uint32_t texture_id;
    int32_t width;
    int32_t height;
    uint32_t format;
    int num_planes;
    struct lorie_dmabuf_plane planes[4];
    int imported;
};

/* Supported formats list */
extern const uint32_t lorie_dmabuf_supported_formats[];
extern const int lorie_dmabuf_num_supported_formats;

/* Check if a DRM format is supported for import */
int lorie_dmabuf_format_supported(uint32_t format);

/* Validate params without importing. Returns 0 on success,
 * or a zwp_linux_buffer_params_v1_error code on failure. */
int lorie_dmabuf_params_validate(int32_t width, int32_t height, uint32_t format,
                                 int num_planes, struct lorie_dmabuf_plane *planes);

/* Import a dmabuf buffer using EGL. Any of the function pointer args may be NULL
 * to skip the actual EGL import (useful for tests that only validate params). */
struct lorie_dmabuf_buffer *lorie_dmabuf_buffer_import(
    struct wl_resource *buffer_resource,
    int32_t width, int32_t height, uint32_t format,
    int num_planes, struct lorie_dmabuf_plane *planes,
    void *egl_display,
    void *egl_create_image_khr,
    void *egl_destroy_image_khr,
    void *gl_egl_image_target_texture2d_oes);

/* Release EGL resources and free the buffer struct.
 * Uses renderer stored in buf if available, otherwise egl_display/egl_destroy_image_khr. */
void lorie_dmabuf_buffer_destroy(struct lorie_dmabuf_buffer *buf,
                                 void *egl_display,
                                 void *egl_destroy_image_khr);

/* Create the linux-dmabuf global (compositor provides renderer for import) */
struct wl_global *lorie_linux_dmabuf_create(struct wl_display *display, struct lorie_compositor *compositor);

#ifdef __cplusplus
}
#endif

#endif /* LORIE_LINUX_DMABUF_H */
