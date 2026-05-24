/* Lorie Wayland Compositor — Surface, Region, Subcompositor */

#include "compositor.h"
#include "input.h"
#include "renderer.h"
#include "../../lorie/buffer.h"
#include <wayland-server-protocol.h>
#include <stdlib.h>
#include <string.h>

/* Generated protocol header for xdg-shell error codes */
#include "stable-xdg-shell-xdg-shell.h"

/* Forward declaration needed for surface_impl table */
static void surface_handle_resource_destroy(struct wl_resource *resource);

/* --- Surface callbacks --- */

static void surface_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static void surface_attach(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *buffer,
                           int32_t x, int32_t y) {
    struct lorie_surface *s = wl_resource_get_user_data(resource);
    s->pending_buffer = buffer;
    s->pending_attached = 1;
    s->pending_x = x;
    s->pending_y = y;
    (void)client;
}

static void surface_damage(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x, int32_t y,
                           int32_t width, int32_t height) {
    struct lorie_surface *s = wl_resource_get_user_data(resource);
    pixman_region32_union_rect(&s->damage, &s->damage, x, y, width, height);
    if (s->compositor && s->compositor->renderer) {
        lorie_renderer_damage_surface(s->compositor->renderer, s, x, y, width, height);
    }
    (void)client;
}

static void surface_frame(struct wl_client *client,
                          struct wl_resource *resource,
                          uint32_t callback) {
    struct lorie_surface *s = wl_resource_get_user_data(resource);
    struct lorie_frame_callback *cb = calloc(1, sizeof(*cb));
    if (!cb) {
        wl_client_post_no_memory(client);
        return;
    }
    cb->resource = wl_resource_create(client, &wl_callback_interface, 1, callback);
    if (!cb->resource) {
        free(cb);
        wl_client_post_no_memory(client);
        return;
    }
    wl_list_insert(&s->frame_callbacks, &cb->link);
}

static void surface_set_opaque_region(struct wl_client *client,
                                      struct wl_resource *resource,
                                      struct wl_resource *region) {
    (void)client; (void)resource; (void)region;
}

static void surface_set_input_region(struct wl_client *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *region) {
    (void)client; (void)resource; (void)region;
}

void lorie_surface_compute_logical_size(struct lorie_surface *s) {
    if (!s) return;

    int32_t src_w = s->viewport.has_src ? (int32_t)s->viewport.src_w
                                        : s->width / s->buffer_scale;
    int32_t src_h = s->viewport.has_src ? (int32_t)s->viewport.src_h
                                        : s->height / s->buffer_scale;

    int32_t logical_w = s->viewport.has_dst ? s->viewport.dst_w : src_w;
    int32_t logical_h = s->viewport.has_dst ? s->viewport.dst_h : src_h;

    /* Swap width/height for 90° and 270° transforms */
    if (s->buffer_transform == WL_OUTPUT_TRANSFORM_90 ||
        s->buffer_transform == WL_OUTPUT_TRANSFORM_270 ||
        s->buffer_transform == WL_OUTPUT_TRANSFORM_FLIPPED_90 ||
        s->buffer_transform == WL_OUTPUT_TRANSFORM_FLIPPED_270) {
        int32_t tmp = logical_w;
        logical_w = logical_h;
        logical_h = tmp;
    }

    s->logical_width = logical_w > 0 ? logical_w : 0;
    s->logical_height = logical_h > 0 ? logical_h : 0;
}

void surface_commit(struct wl_client *client,
                    struct wl_resource *resource) {
    struct lorie_surface *s = wl_resource_get_user_data(resource);

    /* xdg-shell: reject buffer attach before first configure */
    if (s->xdg_surface && !s->xdg_surface->configured && s->pending_attached) {
        if (s->xdg_surface->resource)
            wl_resource_post_error(s->xdg_surface->resource,
                XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
                "buffer attached before first configure");
        s->pending_attached = 0;
        s->pending_buffer = NULL;
        /* Do not send configure or frame callbacks for rejected commits */
        return;
    }

    if (s->pending_attached) {
        if (s->buffer_resource) {
            wl_buffer_send_release(s->buffer_resource);
            if (s->buffer) {
                LorieBuffer_release((LorieBuffer*)s->buffer);
                s->buffer = NULL;
            }
        }
        s->buffer_resource = s->pending_buffer;
        s->x = s->pending_x;
        s->y = s->pending_y;
        s->pending_buffer = NULL;
        s->pending_attached = 0;

        if (s->buffer_resource) {
            struct lorie_shm_buffer *shm = lorie_shm_buffer_from_resource(s->buffer_resource);
            if (shm) {
                int8_t lfmt = (shm->format == WL_SHM_FORMAT_ARGB8888)
                    ? AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM
                    : AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM;
                LorieBuffer *lb = LorieBuffer_allocate(shm->width, shm->height, lfmt, LORIEBUFFER_REGULAR);
                if (lb) {
                    const LorieBuffer_Desc *desc = LorieBuffer_description(lb);
                    uint8_t *dst = (uint8_t*)desc->data;
                    uint8_t *src = (uint8_t*)shm->data;
                    int dst_stride = shm->width * 4;
                    for (int row = 0; row < shm->height; row++) {
                        memcpy(dst + row * dst_stride, src + row * shm->stride, shm->width * 4);
                    }
                    s->buffer = lb;
                    s->width = shm->width;
                    s->height = shm->height;
                }
            }
        }
    }

    /* Apply double-buffered viewport state */
    s->viewport = s->pending_viewport;

    /* Compute logical size from buffer, transform, scale and viewport */
    lorie_surface_compute_logical_size(s);

    if (s->compositor && s->compositor->renderer &&
        s->logical_width > 0 && s->logical_height > 0) {
        lorie_renderer_damage_surface(s->compositor->renderer, s,
                                       0, 0, s->logical_width, s->logical_height);
    }

    /* Frame callbacks are fired by the renderer; do not duplicate here. */

    lorie_xdg_surface_handle_commit(s, client);
    (void)client;
}

static void surface_set_buffer_transform(struct wl_client *client,
                                         struct wl_resource *resource,
                                         int32_t transform) {
    struct lorie_surface *s = wl_resource_get_user_data(resource);
    s->buffer_transform = transform;
    (void)client;
}

static void surface_set_buffer_scale(struct wl_client *client,
                                     struct wl_resource *resource,
                                     int32_t scale) {
    struct lorie_surface *s = wl_resource_get_user_data(resource);
    if (scale <= 0) {
        wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_SCALE,
                               "buffer_scale must be >= 1");
        return;
    }
    s->buffer_scale = scale;
    (void)client;
}

static void surface_damage_buffer(struct wl_client *client,
                                  struct wl_resource *resource,
                                  int32_t x, int32_t y,
                                  int32_t width, int32_t height) {
    surface_damage(client, resource, x, y, width, height);
}

static const struct wl_surface_interface surface_impl = {
    surface_destroy,
    surface_attach,
    surface_damage,
    surface_frame,
    surface_set_opaque_region,
    surface_set_input_region,
    surface_commit,
    surface_set_buffer_transform,
    surface_set_buffer_scale,
    surface_damage_buffer,
};

static void surface_handle_resource_destroy(struct wl_resource *resource) {
    struct lorie_surface *s = wl_resource_get_user_data(resource);
    wl_list_remove(&s->link);
    wl_list_remove(&s->subsurface_link);
    struct lorie_surface *child, *child_tmp;
    wl_list_for_each_safe(child, child_tmp, &s->subsurfaces, subsurface_link) {
        child->parent = NULL;
        wl_list_remove(&child->subsurface_link);
        wl_list_init(&child->subsurface_link);
    }
    if (s->viewport_resource) {
        wl_resource_destroy(s->viewport_resource);
        s->viewport_resource = NULL;
    }
    if (s->buffer_resource)
        wl_buffer_send_release(s->buffer_resource);
    if (s->buffer) {
        LorieBuffer_release((LorieBuffer*)s->buffer);
        s->buffer = NULL;
    }
    struct lorie_frame_callback *cb, *cb_tmp;
    wl_list_for_each_safe(cb, cb_tmp, &s->frame_callbacks, link) {
        wl_resource_destroy(cb->resource);
    }
    pixman_region32_fini(&s->damage);
    if (s->compositor && s->compositor->input)
        lorie_input_clear_focus_for_surface(s->compositor->input, s);
    if (s->compositor && s->compositor->renderer)
        lorie_renderer_remove_surface(s->compositor->renderer, s);
    if (s->xdg_surface) {
        s->xdg_surface->surface = NULL;
        s->xdg_surface = NULL;
    }
    free(s);
}

/* --- Internal API --- */

struct lorie_surface *lorie_surface_create_internal(struct lorie_compositor *c,
                                                     struct wl_client *client,
                                                     uint32_t id) {
    struct lorie_surface *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->compositor = c;
    s->buffer = NULL;
    s->buffer_scale = 1;
    wl_list_init(&s->link);
    wl_list_init(&s->frame_callbacks);
    wl_list_init(&s->subsurfaces);
    wl_list_init(&s->subsurface_link);
    pixman_region32_init(&s->damage);
    if (client) {
        s->resource = wl_resource_create(client, &wl_surface_interface, 6, id);
        if (!s->resource) {
            pixman_region32_fini(&s->damage);
            free(s);
            return NULL;
        }
        wl_resource_set_implementation(s->resource, &surface_impl, s,
                                       surface_handle_resource_destroy);
    }
    if (c)
        wl_list_insert(&c->surfaces, &s->link);
    if (c && c->renderer)
        lorie_renderer_add_surface(c->renderer, s);
    return s;
}

void lorie_surface_destroy_internal(struct lorie_surface *s) {
    if (!s) return;
    if (s->resource)
        wl_resource_destroy(s->resource);
    else {
        if (s->compositor && s->compositor->input)
            lorie_input_clear_focus_for_surface(s->compositor->input, s);
        if (s->compositor && s->compositor->renderer)
            lorie_renderer_remove_surface(s->compositor->renderer, s);
        if (s->xdg_surface) {
            s->xdg_surface->surface = NULL;
            s->xdg_surface = NULL;
        }
        wl_list_remove(&s->link);
        wl_list_remove(&s->subsurface_link);
        pixman_region32_fini(&s->damage);
        free(s);
    }
}

/* --- Compositor callback wrappers --- */

void compositor_create_surface(struct wl_client *client,
                               struct wl_resource *resource,
                               uint32_t id) {
    struct lorie_compositor *c = wl_resource_get_user_data(resource);
    struct lorie_surface *s = lorie_surface_create_internal(c, client, id);
    if (!s)
        wl_client_post_no_memory(client);
}

/* --- Region --- */

static void region_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static void region_add(struct wl_client *client, struct wl_resource *resource,
                       int32_t x, int32_t y, int32_t w, int32_t h) {
    struct lorie_region *r = wl_resource_get_user_data(resource);
    pixman_region32_union_rect(&r->region, &r->region, x, y, w, h);
    (void)client;
}

static void region_subtract(struct wl_client *client, struct wl_resource *resource,
                            int32_t x, int32_t y, int32_t w, int32_t h) {
    struct lorie_region *r = wl_resource_get_user_data(resource);
    pixman_region32_t rect;
    pixman_region32_init_rect(&rect, x, y, w, h);
    pixman_region32_subtract(&r->region, &r->region, &rect);
    pixman_region32_fini(&rect);
    (void)client;
}

static const struct wl_region_interface region_impl = {
    region_destroy,
    region_add,
    region_subtract,
};

static void region_handle_resource_destroy(struct wl_resource *resource) {
    struct lorie_region *r = wl_resource_get_user_data(resource);
    pixman_region32_fini(&r->region);
    free(r);
}

void compositor_create_region(struct wl_client *client,
                              struct wl_resource *resource,
                              uint32_t id) {
    struct lorie_region *r = calloc(1, sizeof(*r));
    if (!r) {
        wl_client_post_no_memory(client);
        return;
    }
    pixman_region32_init(&r->region);
    r->resource = wl_resource_create(client, &wl_region_interface, 1, id);
    if (!r->resource) {
        pixman_region32_fini(&r->region);
        free(r);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(r->resource, &region_impl, r,
                                   region_handle_resource_destroy);
}

/* --- Subcompositor --- */

void subcompositor_get_subsurface(struct wl_client *client,
                                  struct wl_resource *resource,
                                  uint32_t id,
                                  struct wl_resource *surface_resource,
                                  struct wl_resource *parent_resource) {
    struct lorie_surface *surface = wl_resource_get_user_data(surface_resource);
    struct lorie_surface *parent = wl_resource_get_user_data(parent_resource);
    if (surface == parent) {
        wl_resource_post_error(resource,
            WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
            "cannot subsurface a surface to itself");
        return;
    }
    if (surface->parent) {
        wl_list_remove(&surface->subsurface_link);
        surface->parent = NULL;
    }
    surface->parent = parent;
    wl_list_insert(&parent->subsurfaces, &surface->subsurface_link);
    (void)client; (void)resource; (void)id;
}
