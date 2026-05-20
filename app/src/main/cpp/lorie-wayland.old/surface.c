#include "compositor.h"
#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#include <pixman.h>

#define LOG_TAG "LorieWayland"
#define log(prio, ...) __android_log_print(ANDROID_LOG_ ## prio, LOG_TAG, __VA_ARGS__)

/* ============================================================================
 * Surface protocol handlers
 * ============================================================================ */

static void surface_destroy(struct wl_client* client, struct wl_resource* resource);
static void surface_attach(struct wl_client* client, struct wl_resource* resource,
                           struct wl_resource* buffer, int32_t x, int32_t y);
static void surface_damage(struct wl_client* client, struct wl_resource* resource,
                           int32_t x, int32_t y, int32_t width, int32_t height);
static void surface_frame(struct wl_client* client, struct wl_resource* resource,
                          uint32_t callback);
static void surface_set_opaque_region(struct wl_client* client, struct wl_resource* resource,
                                      struct wl_resource* region);
static void surface_set_input_region(struct wl_client* client, struct wl_resource* resource,
                                     struct wl_resource* region);
static void surface_commit(struct wl_client* client, struct wl_resource* resource);
static void surface_set_buffer_transform(struct wl_client* client, struct wl_resource* resource,
                                         int32_t transform);
static void surface_set_buffer_scale(struct wl_client* client, struct wl_resource* resource,
                                     int32_t scale);
static void surface_damage_buffer(struct wl_client* client, struct wl_resource* resource,
                                  int32_t x, int32_t y, int32_t width, int32_t height);

const struct wl_surface_interface surface_interface = {
    .destroy = surface_destroy,
    .attach = surface_attach,
    .damage = surface_damage,
    .frame = surface_frame,
    .set_opaque_region = surface_set_opaque_region,
    .set_input_region = surface_set_input_region,
    .commit = surface_commit,
    .set_buffer_transform = surface_set_buffer_transform,
    .set_buffer_scale = surface_set_buffer_scale,
    .damage_buffer = surface_damage_buffer,
};

/* ============================================================================
 * Subsurface protocol handlers
 * ============================================================================ */

static void subsurface_destroy(struct wl_client* client, struct wl_resource* resource);
static void subsurface_set_position(struct wl_client* client, struct wl_resource* resource,
                                    int32_t x, int32_t y);
static void subsurface_place_above(struct wl_client* client, struct wl_resource* resource,
                                   struct wl_resource* sibling);
static void subsurface_place_below(struct wl_client* client, struct wl_resource* resource,
                                   struct wl_resource* sibling);
static void subsurface_set_sync(struct wl_client* client, struct wl_resource* resource);
static void subsurface_set_desync(struct wl_client* client, struct wl_resource* resource);

const struct wl_subsurface_interface subsurface_interface = {
    .destroy = subsurface_destroy,
    .set_position = subsurface_set_position,
    .place_above = subsurface_place_above,
    .place_below = subsurface_place_below,
    .set_sync = subsurface_set_sync,
    .set_desync = subsurface_set_desync,
};

/* ============================================================================
 * Shell surface protocol handlers
 * ============================================================================ */

static void shell_surface_pong(struct wl_client* client, struct wl_resource* resource,
                               uint32_t serial);
static void shell_surface_move(struct wl_client* client, struct wl_resource* resource,
                               struct wl_resource* seat, uint32_t serial);
static void shell_surface_resize(struct wl_client* client, struct wl_resource* resource,
                                 struct wl_resource* seat, uint32_t serial, uint32_t edges);
static void shell_surface_set_toplevel(struct wl_client* client, struct wl_resource* resource);
static void shell_surface_set_transient(struct wl_client* client, struct wl_resource* resource,
                                        struct wl_resource* parent, int32_t x, int32_t y,
                                        uint32_t flags);
static void shell_surface_set_fullscreen(struct wl_client* client, struct wl_resource* resource,
                                         uint32_t method, uint32_t framerate,
                                         struct wl_resource* output);
static void shell_surface_set_popup(struct wl_client* client, struct wl_resource* resource,
                                    struct wl_resource* seat, uint32_t serial,
                                    struct wl_resource* parent, int32_t x, int32_t y,
                                    uint32_t flags);
static void shell_surface_set_maximized(struct wl_client* client, struct wl_resource* resource,
                                        struct wl_resource* output);
static void shell_surface_set_title(struct wl_client* client, struct wl_resource* resource,
                                    const char* title);
static void shell_surface_set_class(struct wl_client* client, struct wl_resource* resource,
                                    const char* class_);

const struct wl_shell_surface_interface shell_surface_interface = {
    .pong = shell_surface_pong,
    .move = shell_surface_move,
    .resize = shell_surface_resize,
    .set_toplevel = shell_surface_set_toplevel,
    .set_transient = shell_surface_set_transient,
    .set_fullscreen = shell_surface_set_fullscreen,
    .set_popup = shell_surface_set_popup,
    .set_maximized = shell_surface_set_maximized,
    .set_title = shell_surface_set_title,
    .set_class = shell_surface_set_class,
};

/* ============================================================================
 * Surface implementation
 * ============================================================================ */

static void surface_handle_resource_destroy(struct wl_resource* resource) {
    struct lorie_surface* surface = wl_resource_get_user_data(resource);
    if (!surface)
        return;

    lorie_compositor_lock(surface->compositor);

    wl_list_remove(&surface->link);

    if (surface->pending_buffer)
        LorieBuffer_release(surface->pending_buffer);
    if (surface->current_buffer)
        LorieBuffer_release(surface->current_buffer);

    pixman_region32_fini(&surface->pending_damage);
    pixman_region32_fini(&surface->current_damage);
    pixman_region32_fini(&surface->input_region);

    struct lorie_frame_callback* cb, *tmp;
    wl_list_for_each_safe(cb, tmp, &surface->frame_callbacks, link) {
        wl_list_remove(&cb->link);
        free(cb);
    }

    lorie_compositor_unlock(surface->compositor);
    free(surface);
}

static void surface_destroy(struct wl_client* client, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void surface_attach(struct wl_client* client, struct wl_resource* resource,
                           struct wl_resource* buffer_resource, int32_t x, int32_t y) {
    struct lorie_surface* surface = wl_resource_get_user_data(resource);
    if (!surface)
        return;

    lorie_compositor_lock(surface->compositor);

    surface->dx = x;
    surface->dy = y;
    surface->pending_attached = 1;

    if (surface->pending_buffer) {
        LorieBuffer_release(surface->pending_buffer);
        surface->pending_buffer = NULL;
    }

    surface->pending_buffer_resource = buffer_resource;

    lorie_compositor_unlock(surface->compositor);
}

static void surface_damage(struct wl_client* client, struct wl_resource* resource,
                           int32_t x, int32_t y, int32_t width, int32_t height) {
    struct lorie_surface* surface = wl_resource_get_user_data(resource);
    if (!surface)
        return;

    lorie_compositor_lock(surface->compositor);
    pixman_region32_union_rect(
        &surface->pending_damage, &surface->pending_damage,
        x, y, width, height);
    lorie_compositor_unlock(surface->compositor);
}

static void surface_frame(struct wl_client* client, struct wl_resource* resource,
                          uint32_t id) {
    struct lorie_surface* surface = wl_resource_get_user_data(resource);
    if (!surface)
        return;

    struct lorie_frame_callback* cb = calloc(1, sizeof(*cb));
    if (!cb)
        return;

    cb->resource = wl_resource_create(client, &wl_callback_interface, 1, id);
    if (!cb->resource) {
        free(cb);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(cb->resource, NULL, cb, NULL);

    lorie_compositor_lock(surface->compositor);
    wl_list_insert(&surface->frame_callbacks, &cb->link);
    lorie_compositor_unlock(surface->compositor);
}

static void surface_set_opaque_region(struct wl_client* client, struct wl_resource* resource,
                                      struct wl_resource* region) {
    /* TODO: opaque region optimization */
}

static void surface_set_input_region(struct wl_client* client, struct wl_resource* resource,
                                     struct wl_resource* region) {
    struct lorie_surface* surface = wl_resource_get_user_data(resource);
    if (!surface)
        return;

    lorie_compositor_lock(surface->compositor);
    if (region) {
        surface->input_region_set = 1;
    } else {
        surface->input_region_set = 0;
        pixman_region32_reset(&surface->input_region, NULL);
    }
    lorie_compositor_unlock(surface->compositor);
}

static void surface_commit(struct wl_client* client, struct wl_resource* resource) {
    struct lorie_surface* surface = wl_resource_get_user_data(resource);
    if (!surface)
        return;

    lorie_compositor_lock(surface->compositor);

    if (surface->pending_attached) {
        if (surface->current_buffer) {
            LorieBuffer_release(surface->current_buffer);
        }
        surface->current_buffer = surface->pending_buffer;
        surface->current_buffer_resource = surface->pending_buffer_resource;
        surface->pending_buffer = NULL;
        surface->pending_buffer_resource = NULL;
        surface->pending_attached = 0;
        surface->mapped = 1;
    }

    if (surface->dx != 0 || surface->dy != 0) {
        surface->x += surface->dx;
        surface->y += surface->dy;
        surface->dx = 0;
        surface->dy = 0;
    }

    pixman_region32_union(
        &surface->current_damage, &surface->current_damage,
        &surface->pending_damage);
    pixman_region32_clear(&surface->pending_damage);

    surface->compositor->redraw_needed = 1;
    pthread_cond_broadcast(&surface->compositor->cond);

    lorie_compositor_unlock(surface->compositor);
}

static void surface_set_buffer_transform(struct wl_client* client, struct wl_resource* resource,
                                         int32_t transform) {
    /* TODO: buffer transform */
}

static void surface_set_buffer_scale(struct wl_client* client, struct wl_resource* resource,
                                     int32_t scale) {
    /* TODO: HiDPI scaling */
}

static void surface_damage_buffer(struct wl_client* client, struct wl_resource* resource,
                                  int32_t x, int32_t y, int32_t width, int32_t height) {
    surface_damage(client, resource, x, y, width, height);
}

/* ============================================================================
 * Subsurface implementation
 * ============================================================================ */

static void subsurface_destroy(struct wl_client* client, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

static void subsurface_set_position(struct wl_client* client, struct wl_resource* resource,
                                    int32_t x, int32_t y) {
    struct lorie_surface* surface = wl_resource_get_user_data(resource);
    if (surface) {
        surface->x = x;
        surface->y = y;
    }
}

static void subsurface_place_above(struct wl_client* client, struct wl_resource* resource,
                                   struct wl_resource* sibling) {
    /* TODO: z-ordering */
}

static void subsurface_place_below(struct wl_client* client, struct wl_resource* resource,
                                   struct wl_resource* sibling) {
    /* TODO: z-ordering */
}

static void subsurface_set_sync(struct wl_client* client, struct wl_resource* resource) {
    /* TODO: sync mode */
}

static void subsurface_set_desync(struct wl_client* client, struct wl_resource* resource) {
    /* TODO: desync mode */
}

/* ============================================================================
 * Compositor global
 * ============================================================================ */

static void compositor_create_surface(struct wl_client* client, struct wl_resource* resource,
                                      uint32_t id) {
    struct lorie_compositor* compositor = wl_resource_get_user_data(resource);
    struct lorie_surface* surface = calloc(1, sizeof(*surface));
    if (!surface) {
        wl_resource_post_no_memory(resource);
        return;
    }

    surface->compositor = compositor;
    surface->resource = wl_resource_create(client, &wl_surface_interface,
                                           wl_resource_get_version(resource), id);
    if (!surface->resource) {
        free(surface);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(surface->resource, &surface_interface, surface,
                                   surface_handle_resource_destroy);

    pixman_region32_init(&surface->pending_damage);
    pixman_region32_init(&surface->current_damage);
    pixman_region32_init(&surface->input_region);
    wl_list_init(&surface->frame_callbacks);
    wl_list_init(&surface->subsurfaces);
    wl_list_init(&surface->parent_link);

    lorie_compositor_lock(compositor);
    wl_list_insert(&compositor->surfaces, &surface->link);
    lorie_compositor_unlock(compositor);
}

static void compositor_create_region(struct wl_client* client, struct wl_resource* resource,
                                     uint32_t id) {
    struct wl_resource* region = wl_resource_create(client, &wl_region_interface, 1, id);
    if (!region) {
        wl_resource_post_no_memory(resource);
        return;
    }
    wl_resource_set_implementation(region, NULL, NULL, NULL);
}

const struct wl_compositor_interface compositor_impl = {
    .create_surface = compositor_create_surface,
    .create_region = compositor_create_region,
};

void compositor_bind(struct wl_client* client, void* data,
                     uint32_t version, uint32_t id) {
    struct lorie_compositor* compositor = data;
    struct wl_resource* resource = wl_resource_create(client, &wl_compositor_interface,
                                                      version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &compositor_impl, compositor, NULL);
}

/* ============================================================================
 * Subcompositor global
 * ============================================================================ */

static void subcompositor_get_subsurface(struct wl_client* client, struct wl_resource* resource,
                                         uint32_t id, struct wl_resource* surface_resource,
                                         struct wl_resource* parent_resource) {
    struct lorie_surface* surface = wl_resource_get_user_data(surface_resource);
    struct lorie_surface* parent = wl_resource_get_user_data(parent_resource);

    if (!surface || !parent) {
        wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "invalid surface or parent");
        return;
    }

    surface->parent = parent;
    struct wl_resource* subsurface = wl_resource_create(client, &wl_subsurface_interface, 1, id);
    if (!subsurface) {
        wl_resource_post_no_memory(resource);
        return;
    }
    wl_resource_set_implementation(subsurface, &subsurface_interface, surface, NULL);
    wl_list_insert(&parent->subsurfaces, &surface->parent_link);
}

const struct wl_subcompositor_interface subcompositor_impl = {
    .destroy = NULL,
    .get_subsurface = subcompositor_get_subsurface,
};

void subcompositor_bind(struct wl_client* client, void* data,
                        uint32_t version, uint32_t id) {
    struct wl_resource* resource = wl_resource_create(client, &wl_subcompositor_interface,
                                                      version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &subcompositor_impl, data, NULL);
}

/* ============================================================================
 * Shell global (basic wl_shell)
 * ============================================================================ */

static void shell_surface_handle_resource_destroy(struct wl_resource* resource) {
    struct lorie_shell_surface* ss = wl_resource_get_user_data(resource);
    if (ss)
        free(ss);
}

static void shell_get_shell_surface(struct wl_client* client, struct wl_resource* resource,
                                    uint32_t id, struct wl_resource* surface_resource) {
    struct lorie_surface* surface = wl_resource_get_user_data(surface_resource);
    if (!surface) {
        wl_resource_post_error(resource, WL_SHELL_ERROR_ROLE,
                               "invalid surface for shell_surface");
        return;
    }

    struct lorie_shell_surface* ss = calloc(1, sizeof(*ss));
    if (!ss) {
        wl_resource_post_no_memory(resource);
        return;
    }

    ss->surface = surface;
    ss->resource = wl_resource_create(client, &wl_shell_surface_interface, 1, id);
    if (!ss->resource) {
        free(ss);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(ss->resource, &shell_surface_interface, ss,
                                   shell_surface_handle_resource_destroy);
    surface->shell_surface = ss->resource;
}

const struct wl_shell_interface shell_impl = {
    .get_shell_surface = shell_get_shell_surface,
};

void shell_bind(struct wl_client* client, void* data,
                uint32_t version, uint32_t id) {
    struct wl_resource* resource = wl_resource_create(client, &wl_shell_interface,
                                                      version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &shell_impl, data, NULL);
}

static void shell_surface_pong(struct wl_client* client, struct wl_resource* resource,
                               uint32_t serial) {
    /* Ping/pong is a no-op for now */
}

static void shell_surface_move(struct wl_client* client, struct wl_resource* resource,
                               struct wl_resource* seat, uint32_t serial) {
    /* TODO: interactive move */
}

static void shell_surface_resize(struct wl_client* client, struct wl_resource* resource,
                                 struct wl_resource* seat, uint32_t serial, uint32_t edges) {
    /* TODO: interactive resize */
}

static void shell_surface_set_toplevel(struct wl_client* client, struct wl_resource* resource) {
    struct lorie_shell_surface* ss = wl_resource_get_user_data(resource);
    if (ss && ss->surface) {
        ss->surface->x = 0;
        ss->surface->y = 0;
        struct lorie_output* output = wl_container_of(ss->surface->compositor->outputs.next,
                                                       output, link);
        if (output) {
            ss->surface->width = output->width;
            ss->surface->height = output->height;
        }
    }
}

static void shell_surface_set_transient(struct wl_client* client, struct wl_resource* resource,
                                        struct wl_resource* parent, int32_t x, int32_t y,
                                        uint32_t flags) {
    struct lorie_shell_surface* ss = wl_resource_get_user_data(resource);
    if (ss && ss->surface) {
        ss->surface->x = x;
        ss->surface->y = y;
    }
}

static void shell_surface_set_fullscreen(struct wl_client* client, struct wl_resource* resource,
                                         uint32_t method, uint32_t framerate,
                                         struct wl_resource* output) {
    shell_surface_set_toplevel(client, resource);
}

static void shell_surface_set_popup(struct wl_client* client, struct wl_resource* resource,
                                    struct wl_resource* seat, uint32_t serial,
                                    struct wl_resource* parent, int32_t x, int32_t y,
                                    uint32_t flags) {
    struct lorie_shell_surface* ss = wl_resource_get_user_data(resource);
    if (ss && ss->surface) {
        ss->surface->x = x;
        ss->surface->y = y;
    }
}

static void shell_surface_set_maximized(struct wl_client* client, struct wl_resource* resource,
                                        struct wl_resource* output) {
    shell_surface_set_toplevel(client, resource);
}

static void shell_surface_set_title(struct wl_client* client, struct wl_resource* resource,
                                    const char* title) {
    /* TODO: title tracking */
}

static void shell_surface_set_class(struct wl_client* client, struct wl_resource* resource,
                                    const char* class_) {
    /* TODO: app-id tracking */
}

/* ============================================================================
 * Surface helpers
 * ============================================================================ */

struct lorie_surface* lorie_surface_at(struct lorie_compositor* c, int32_t x, int32_t y) {
    struct lorie_surface* surface;
    struct lorie_surface* found = NULL;

    lorie_compositor_lock(c);
    wl_list_for_each(surface, &c->surfaces, link) {
        if (surface->mapped &&
            x >= surface->x && x < surface->x + surface->width &&
            y >= surface->y && y < surface->y + surface->height) {
            found = surface;
        }
    }
    lorie_compositor_unlock(c);
    return found;
}

void lorie_surface_send_frame_callbacks(struct lorie_surface* surface) {
    if (!surface)
        return;

    struct lorie_frame_callback* cb, *tmp;
    wl_list_for_each_safe(cb, tmp, &surface->frame_callbacks, link) {
        wl_callback_send_done(cb->resource, 0);
        wl_resource_destroy(cb->resource);
        wl_list_remove(&cb->link);
        free(cb);
    }
}
