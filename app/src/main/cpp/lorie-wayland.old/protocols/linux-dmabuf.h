#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <wayland-server-core.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lorie_compositor;

/* linux_dmabuf - DMA-BUF buffer import for zero-copy */
struct zwp_linux_dmabuf_v1 {
    struct wl_global *global;
    struct lorie_compositor *compositor;
    struct wl_list resources;
    uint32_t version;
};

struct zwp_linux_buffer_params_v1 {
    struct wl_resource *resource;
    struct wl_list link;
    int width, height;
    uint32_t format;
    uint32_t flags;
    int n_planes;
    struct {
        int fd;
        uint32_t offset;
        uint32_t stride;
        uint64_t modifier;
    } planes[4];
};

struct zwp_linux_dmabuf_feedback_v1 {
    struct wl_resource *resource;
    struct wl_list link;
};

/* Create/destroy linux_dmabuf global */
struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf_v1_create(struct wl_display *display, struct lorie_compositor *compositor, uint32_t version);
void zwp_linux_dmabuf_v1_destroy(struct zwp_linux_dmabuf_v1 *dmabuf);

/* Add supported format/modifier */
void zwp_linux_dmabuf_v1_add_format(struct zwp_linux_dmabuf_v1 *dmabuf, uint32_t format, uint64_t modifier);

#ifdef __cplusplus
}
#endif
