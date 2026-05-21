/* linux-dmabuf protocol implementation */
#include "compositor.h"
#include "stable-linux-dmabuf-linux-dmabuf-v1.h"
#include <stdlib.h>
#include <unistd.h>

struct lorie_dmabuf_resource {
    struct wl_resource *resource;
    struct wl_list link;
};

struct lorie_buffer_params {
    struct wl_resource *resource;
    int fd[4];
    uint32_t plane_idx;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint64_t modifier;
    int used[4];
};

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
    if (params->used[plane_idx]) close(params->fd[plane_idx]);
    params->fd[plane_idx] = fd;
    params->used[plane_idx] = 1;
    (void)client; (void)offset; (void)stride; (void)modifier_hi; (void)modifier_lo;
}

static void params_create(struct wl_client *client, struct wl_resource *resource,
                          int32_t width, int32_t height, uint32_t format, uint32_t flags) {
    struct lorie_buffer_params *params = wl_resource_get_user_data(resource);
    for (int i = 0; i < 4; i++) if (params->used[i]) close(params->fd[i]);
    wl_resource_destroy(resource);
    (void)client; (void)width; (void)height; (void)format; (void)flags;
}

static void params_create_immed(struct wl_client *client, struct wl_resource *resource,
                                uint32_t buffer_id, int32_t width, int32_t height, uint32_t format, uint32_t flags) {
    struct lorie_buffer_params *params = wl_resource_get_user_data(resource);
    struct wl_resource *buffer = wl_resource_create(client, &wl_buffer_interface, 1, buffer_id);
    if (!buffer) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(buffer, NULL, NULL, NULL);
    for (int i = 0; i < 4; i++) if (params->used[i]) close(params->fd[i]);
    wl_resource_destroy(resource);
    (void)width; (void)height; (void)format; (void)flags;
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
        for (int i = 0; i < 4; i++) if (params->used[i]) close(params->fd[i]);
        free(params);
    }
}

static void linux_dmabuf_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static void linux_dmabuf_create_params(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    struct lorie_buffer_params *params = calloc(1, sizeof(*params));
    if (!params) { wl_client_post_no_memory(client); return; }
    params->resource = wl_resource_create(client, &zwp_linux_buffer_params_v1_interface, 1, id);
    if (!params->resource) { free(params); wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(params->resource, &params_impl, params, params_handle_destroy);
    (void)resource;
}

static const struct zwp_linux_dmabuf_v1_interface linux_dmabuf_impl = {
    linux_dmabuf_destroy,
    linux_dmabuf_create_params,
};

static void linux_dmabuf_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
    if (!resource) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(resource, &linux_dmabuf_impl, data, NULL);
}

struct wl_global *lorie_linux_dmabuf_create(struct wl_display *display) {
    return wl_global_create(display, &zwp_linux_dmabuf_v1_interface, 1, NULL, linux_dmabuf_bind);
}
