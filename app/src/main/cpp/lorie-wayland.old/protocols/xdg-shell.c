#include "xdg-shell.h"
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <android/log.h>

#define LOG_TAG "xdg-shell"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* Forward declarations */
static void xdg_wm_base_destroy_handler(struct wl_client *client, struct wl_resource *resource);
static void xdg_wm_base_create_positioner(struct wl_client *client, struct wl_resource *resource, uint32_t id);
static void xdg_wm_base_get_xdg_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource);
static void xdg_wm_base_pong(struct wl_client *client, struct wl_resource *resource, uint32_t serial);

static const struct xdg_wm_base_interface xdg_wm_base_impl = {
    .destroy = xdg_wm_base_destroy_handler,
    .create_positioner = xdg_wm_base_create_positioner,
    .get_xdg_surface = xdg_wm_base_get_xdg_surface,
    .pong = xdg_wm_base_pong,
};

static void xdg_positioner_destroy(struct wl_client *client, struct wl_resource *resource);
static void xdg_positioner_set_size(struct wl_client *client, struct wl_resource *resource, int32_t width, int32_t height);
static void xdg_positioner_set_anchor_rect(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height);
static void xdg_positioner_set_anchor(struct wl_client *client, struct wl_resource *resource, uint32_t anchor);
static void xdg_positioner_set_gravity(struct wl_client *client, struct wl_resource *resource, uint32_t gravity);
static void xdg_positioner_set_constraint_adjustment(struct wl_client *client, struct wl_resource *resource, uint32_t constraint_adjustment);
static void xdg_positioner_set_offset(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y);

static const struct xdg_positioner_interface xdg_positioner_impl = {
    .destroy = xdg_positioner_destroy,
    .set_size = xdg_positioner_set_size,
    .set_anchor_rect = xdg_positioner_set_anchor_rect,
    .set_anchor = xdg_positioner_set_anchor,
    .set_gravity = xdg_positioner_set_gravity,
    .set_constraint_adjustment = xdg_positioner_set_constraint_adjustment,
    .set_offset = xdg_positioner_set_offset,
};

static void xdg_surface_destroy_handler(struct wl_client *client, struct wl_resource *resource);
static void xdg_surface_get_toplevel(struct wl_client *client, struct wl_resource *resource, uint32_t id);
static void xdg_surface_get_popup(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *parent_resource, struct wl_resource *positioner_resource);
static void xdg_surface_set_window_geometry(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height);
static void xdg_surface_ack_configure(struct wl_client *client, struct wl_resource *resource, uint32_t serial);

static const struct xdg_surface_interface xdg_surface_impl = {
    .destroy = xdg_surface_destroy_handler,
    .get_toplevel = xdg_surface_get_toplevel,
    .get_popup = xdg_surface_get_popup,
    .set_window_geometry = xdg_surface_set_window_geometry,
    .ack_configure = xdg_surface_ack_configure,
};

static void xdg_toplevel_destroy_handler(struct wl_client *client, struct wl_resource *resource);
static void xdg_toplevel_set_parent(struct wl_client *client, struct wl_resource *resource, struct wl_resource *parent_resource);
static void xdg_toplevel_set_title(struct wl_client *client, struct wl_resource *resource, const char *title);
static void xdg_toplevel_set_app_id(struct wl_client *client, struct wl_resource *resource, const char *app_id);
static void xdg_toplevel_show_window_menu(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial, int32_t x, int32_t y);
static void xdg_toplevel_move(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial);
static void xdg_toplevel_resize(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial, uint32_t edges);
static void xdg_toplevel_set_max_size(struct wl_client *client, struct wl_resource *resource, int32_t width, int32_t height);
static void xdg_toplevel_set_min_size(struct wl_client *client, struct wl_resource *resource, int32_t width, int32_t height);
static void xdg_toplevel_set_maximized(struct wl_client *client, struct wl_resource *resource);
static void xdg_toplevel_unset_maximized(struct wl_client *client, struct wl_resource *resource);
static void xdg_toplevel_set_fullscreen(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output_resource);
static void xdg_toplevel_unset_fullscreen(struct wl_client *client, struct wl_resource *resource);
static void xdg_toplevel_set_minimized(struct wl_client *client, struct wl_resource *resource);

static const struct xdg_toplevel_interface xdg_toplevel_impl = {
    .destroy = xdg_toplevel_destroy_handler,
    .set_parent = xdg_toplevel_set_parent,
    .set_title = xdg_toplevel_set_title,
    .set_app_id = xdg_toplevel_set_app_id,
    .show_window_menu = xdg_toplevel_show_window_menu,
    .move = xdg_toplevel_move,
    .resize = xdg_toplevel_resize,
    .set_max_size = xdg_toplevel_set_max_size,
    .set_min_size = xdg_toplevel_set_min_size,
    .set_maximized = xdg_toplevel_set_maximized,
    .unset_maximized = xdg_toplevel_unset_maximized,
    .set_fullscreen = xdg_toplevel_set_fullscreen,
    .unset_fullscreen = xdg_toplevel_unset_fullscreen,
    .set_minimized = xdg_toplevel_set_minimized,
};

static void xdg_popup_destroy_handler(struct wl_client *client, struct wl_resource *resource);
static void xdg_popup_grab(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial);
static void xdg_popup_reposition(struct wl_client *client, struct wl_resource *resource, struct wl_resource *positioner_resource, uint32_t token);

static const struct xdg_popup_interface xdg_popup_impl = {
    .destroy = xdg_popup_destroy_handler,
    .grab = xdg_popup_grab,
    .reposition = xdg_popup_reposition,
};

/* Resource destruction callbacks */
static void xdg_wm_base_resource_destroy(struct wl_resource *resource) {
    struct xdg_wm_base_resource *wm_base_res = wl_resource_get_user_data(resource);
    if (wm_base_res) {
        wl_list_remove(&wm_base_res->link);
        free(wm_base_res);
    }
}

static void xdg_positioner_destroy_resource(struct wl_resource *resource) {
    struct xdg_positioner *positioner = wl_resource_get_user_data(resource);
    if (positioner) {
        free(positioner);
    }
}

static void xdg_surface_destroy_resource(struct wl_resource *resource) {
    struct xdg_surface *surface = wl_resource_get_user_data(resource);
    if (surface) {
        wl_list_remove(&surface->link);
        if (surface->lorie_surface) {
            /* Notify compositor that xdg_surface is gone */
        }
        free(surface);
    }
}

static void xdg_toplevel_destroy_resource(struct wl_resource *resource) {
    struct xdg_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (toplevel) {
        wl_list_remove(&toplevel->link);
        free(toplevel->title);
        free(toplevel->app_id);
        free(toplevel);
    }
}

static void xdg_popup_destroy_resource(struct wl_resource *resource) {
    struct xdg_popup *popup = wl_resource_get_user_data(resource);
    if (popup) {
        wl_list_remove(&popup->link);
        free(popup);
    }
}

/* xdg_wm_base handlers */
static void xdg_wm_base_destroy_handler(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void xdg_wm_base_create_positioner(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    struct xdg_wm_base *wm_base = wl_resource_get_user_data(resource);
    struct xdg_positioner *positioner = calloc(1, sizeof(*positioner));
    if (!positioner) {
        wl_resource_post_no_memory(resource);
        return;
    }

    positioner->resource = wl_resource_create(client, &xdg_positioner_interface, wl_resource_get_version(resource), id);
    if (!positioner->resource) {
        free(positioner);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(positioner->resource, &xdg_positioner_impl, positioner, xdg_positioner_destroy_resource);
}

static void xdg_wm_base_get_xdg_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource) {
    struct xdg_wm_base *wm_base = wl_resource_get_user_data(resource);
    struct wl_surface *wl_surface = wl_resource_get_user_data(surface_resource);
    
    struct xdg_surface *xdg_surface = calloc(1, sizeof(*xdg_surface));
    if (!xdg_surface) {
        wl_resource_post_no_memory(resource);
        return;
    }

    xdg_surface->wl_surface = wl_surface;
    xdg_surface->resource = wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(resource), id);
    if (!xdg_surface->resource) {
        free(xdg_surface);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(xdg_surface->resource, &xdg_surface_impl, xdg_surface, xdg_surface_destroy_resource);
    wl_list_insert(&wm_base->compositor->xdg_surfaces, &xdg_surface->link);

    /* Send initial configure */
    uint32_t serial = wl_display_next_serial(wl_client_get_display(client));
    xdg_surface_send_configure(xdg_surface, serial);
}

static void xdg_wm_base_pong(struct wl_client *client, struct wl_resource *resource, uint32_t serial) {
    /* Client responded to ping - mark as responsive */
    LOGD("Client pong received, serial=%u", serial);
}

static void xdg_wm_base_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct xdg_wm_base *wm_base = data;
    struct xdg_wm_base_resource *wm_base_res = calloc(1, sizeof(*wm_base_res));
    if (!wm_base_res) {
        wl_client_post_no_memory(client);
        return;
    }

    wm_base_res->resource = wl_resource_create(client, &xdg_wm_base_interface, version, id);
    if (!wm_base_res->resource) {
        free(wm_base_res);
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(wm_base_res->resource, &xdg_wm_base_impl, wm_base, xdg_wm_base_resource_destroy);
    wl_list_insert(&wm_base->resources, &wm_base_res->link);
}

/* xdg_positioner handlers */
static void xdg_positioner_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void xdg_positioner_set_size(struct wl_client *client, struct wl_resource *resource, int32_t width, int32_t height) {
    struct xdg_positioner *positioner = wl_resource_get_user_data(resource);
    positioner->width = width;
    positioner->height = height;
}

static void xdg_positioner_set_anchor_rect(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height) {
    struct xdg_positioner *positioner = wl_resource_get_user_data(resource);
    positioner->anchor_rect_x = x;
    positioner->anchor_rect_y = y;
    positioner->anchor_rect_width = width;
    positioner->anchor_rect_height = height;
}

static void xdg_positioner_set_anchor(struct wl_client *client, struct wl_resource *resource, uint32_t anchor) {
    struct xdg_positioner *positioner = wl_resource_get_user_data(resource);
    positioner->anchor = anchor;
}

static void xdg_positioner_set_gravity(struct wl_client *client, struct wl_resource *resource, uint32_t gravity) {
    struct xdg_positioner *positioner = wl_resource_get_user_data(resource);
    positioner->gravity = gravity;
}

static void xdg_positioner_set_constraint_adjustment(struct wl_client *client, struct wl_resource *resource, uint32_t constraint_adjustment) {
    struct xdg_positioner *positioner = wl_resource_get_user_data(resource);
    positioner->constraint_adjustment = constraint_adjustment;
}

static void xdg_positioner_set_offset(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y) {
    struct xdg_positioner *positioner = wl_resource_get_user_data(resource);
    positioner->offset_x = x;
    positioner->offset_y = y;
}

/* xdg_surface handlers */
static void xdg_surface_destroy_handler(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void xdg_surface_get_toplevel(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    struct xdg_surface *xdg_surface = wl_resource_get_user_data(resource);
    
    if (xdg_surface->has_role) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED, "xdg_surface already has a role");
        return;
    }

    struct xdg_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    if (!toplevel) {
        wl_resource_post_no_memory(resource);
        return;
    }

    toplevel->xdg_surface = xdg_surface;
    toplevel->resource = wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
    if (!toplevel->resource) {
        free(toplevel);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(toplevel->resource, &xdg_toplevel_impl, toplevel, xdg_toplevel_destroy_resource);
    wl_list_insert(&xdg_surface->compositor->xdg_toplevels, &toplevel->link);
    xdg_surface->has_role = 1;

    /* On Android we only have one output, so map to fullscreen */
    uint32_t serial = wl_display_next_serial(wl_client_get_display(client));
    xdg_toplevel_send_configure(toplevel, 0, 0, XDG_TOPLEVEL_STATE_FULLSCREEN);
    xdg_surface_send_configure(xdg_surface, serial);
}

static void xdg_surface_get_popup(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *parent_resource, struct wl_resource *positioner_resource) {
    struct xdg_surface *xdg_surface = wl_resource_get_user_data(resource);
    struct xdg_surface *parent = wl_resource_get_user_data(parent_resource);
    struct xdg_positioner *positioner = wl_resource_get_user_data(positioner_resource);
    
    if (xdg_surface->has_role) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED, "xdg_surface already has a role");
        return;
    }

    struct xdg_popup *popup = calloc(1, sizeof(*popup));
    if (!popup) {
        wl_resource_post_no_memory(resource);
        return;
    }

    popup->xdg_surface = xdg_surface;
    popup->parent = parent;
    popup->x = positioner->offset_x;
    popup->y = positioner->offset_y;
    popup->width = positioner->width;
    popup->height = positioner->height;
    popup->resource = wl_resource_create(client, &xdg_popup_interface, wl_resource_get_version(resource), id);
    if (!popup->resource) {
        free(popup);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(popup->resource, &xdg_popup_impl, popup, xdg_popup_destroy_resource);
    wl_list_insert(&xdg_surface->compositor->xdg_popups, &popup->link);
    xdg_surface->has_role = 1;

    uint32_t serial = wl_display_next_serial(wl_client_get_display(client));
    xdg_popup_send_configure(popup, popup->x, popup->y, popup->width, popup->height);
    xdg_surface_send_configure(xdg_surface, serial);
}

static void xdg_surface_set_window_geometry(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height) {
    struct xdg_surface *xdg_surface = wl_resource_get_user_data(resource);
    /* Store geometry for the compositor to use */
    LOGD("Window geometry: %dx%d+%d+%d", width, height, x, y);
}

static void xdg_surface_ack_configure(struct wl_client *client, struct wl_resource *resource, uint32_t serial) {
    struct xdg_surface *xdg_surface = wl_resource_get_user_data(resource);
    xdg_surface->configure_serial = serial;
    LOGD("Configure acknowledged, serial=%u", serial);
}

/* xdg_toplevel handlers */
static void xdg_toplevel_destroy_handler(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void xdg_toplevel_set_parent(struct wl_client *client, struct wl_resource *resource, struct wl_resource *parent_resource) {
    /* No-op for now - Android has single window */
}

static void xdg_toplevel_set_title(struct wl_client *client, struct wl_resource *resource, const char *title) {
    struct xdg_toplevel *toplevel = wl_resource_get_user_data(resource);
    free(toplevel->title);
    toplevel->title = strdup(title);
    LOGD("Window title: %s", title);
}

static void xdg_toplevel_set_app_id(struct wl_client *client, struct wl_resource *resource, const char *app_id) {
    struct xdg_toplevel *toplevel = wl_resource_get_user_data(resource);
    free(toplevel->app_id);
    toplevel->app_id = strdup(app_id);
    LOGD("App ID: %s", app_id);
}

static void xdg_toplevel_show_window_menu(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial, int32_t x, int32_t y) {
    /* No-op for Android */
}

static void xdg_toplevel_move(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial) {
    /* No-op for Android - windows are always fullscreen/maximized */
}

static void xdg_toplevel_resize(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial, uint32_t edges) {
    /* No-op for Android */
}

static void xdg_toplevel_set_max_size(struct wl_client *client, struct wl_resource *resource, int32_t width, int32_t height) {
    struct xdg_toplevel *toplevel = wl_resource_get_user_data(resource);
    /* Store but Android will ignore */
}

static void xdg_toplevel_set_min_size(struct wl_client *client, struct wl_resource *resource, int32_t width, int32_t height) {
    struct xdg_toplevel *toplevel = wl_resource_get_user_data(resource);
    /* Store but Android will ignore */
}

static void xdg_toplevel_set_maximized(struct wl_client *client, struct wl_resource *resource) {
    struct xdg_toplevel *toplevel = wl_resource_get_user_data(resource);
    toplevel->maximized = 1;
    uint32_t serial = wl_display_next_serial(wl_client_get_display(client));
    xdg_toplevel_send_configure(toplevel, 0, 0, XDG_TOPLEVEL_STATE_MAXIMIZED);
    xdg_surface_send_configure(toplevel->xdg_surface, serial);
}

static void xdg_toplevel_unset_maximized(struct wl_client *client, struct wl_resource *resource) {
    struct xdg_toplevel *toplevel = wl_resource_get_user_data(resource);
    toplevel->maximized = 0;
    uint32_t serial = wl_display_next_serial(wl_client_get_display(client));
    xdg_toplevel_send_configure(toplevel, 0, 0, 0);
    xdg_surface_send_configure(toplevel->xdg_surface, serial);
}

static void xdg_toplevel_set_fullscreen(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output_resource) {
    struct xdg_toplevel *toplevel = wl_resource_get_user_data(resource);
    toplevel->fullscreen = 1;
    uint32_t serial = wl_display_next_serial(wl_client_get_display(client));
    xdg_toplevel_send_configure(toplevel, 0, 0, XDG_TOPLEVEL_STATE_FULLSCREEN);
    xdg_surface_send_configure(toplevel->xdg_surface, serial);
}

static void xdg_toplevel_unset_fullscreen(struct wl_client *client, struct wl_resource *resource) {
    struct xdg_toplevel *toplevel = wl_resource_get_user_data(resource);
    toplevel->fullscreen = 0;
    uint32_t serial = wl_display_next_serial(wl_client_get_display(client));
    xdg_toplevel_send_configure(toplevel, 0, 0, 0);
    xdg_surface_send_configure(toplevel->xdg_surface, serial);
}

static void xdg_toplevel_set_minimized(struct wl_client *client, struct wl_resource *resource) {
    /* No-op for Android */
}

/* xdg_popup handlers */
static void xdg_popup_destroy_handler(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void xdg_popup_grab(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial) {
    /* No-op for Android */
}

static void xdg_popup_reposition(struct wl_client *client, struct wl_resource *resource, struct wl_resource *positioner_resource, uint32_t token) {
    struct xdg_popup *popup = wl_resource_get_user_data(resource);
    struct xdg_positioner *positioner = wl_resource_get_user_data(positioner_resource);
    
    popup->x = positioner->offset_x;
    popup->y = positioner->offset_y;
    popup->width = positioner->width;
    popup->height = positioner->height;
    
    xdg_popup_send_configure(popup, popup->x, popup->y, popup->width, popup->height);
    xdg_popup_send_repositioned(popup, token);
}

/* Public API */
struct xdg_wm_base *xdg_wm_base_create(struct wl_display *display, struct lorie_compositor *compositor) {
    struct xdg_wm_base *wm_base = calloc(1, sizeof(*wm_base));
    if (!wm_base) {
        return NULL;
    }

    wm_base->compositor = compositor;
    wl_list_init(&wm_base->resources);

    wm_base->global = wl_global_create(display, &xdg_wm_base_interface, 7, wm_base, xdg_wm_base_bind);
    if (!wm_base->global) {
        free(wm_base);
        return NULL;
    }

    LOGD("xdg_wm_base created");
    return wm_base;
}

void xdg_wm_base_destroy(struct xdg_wm_base *wm_base) {
    if (!wm_base) return;
    
    struct xdg_wm_base_resource *res, *tmp;
    wl_list_for_each_safe(res, tmp, &wm_base->resources, link) {
        wl_resource_destroy(res->resource);
    }
    
    wl_global_destroy(wm_base->global);
    free(wm_base);
}

void xdg_surface_send_configure(struct xdg_surface *surface, uint32_t serial) {
    if (surface && surface->resource) {
        xdg_surface_send_configure(surface->resource, serial);
    }
}

void xdg_toplevel_send_configure(struct xdg_toplevel *toplevel, int32_t width, int32_t height, uint32_t states) {
    if (toplevel && toplevel->resource) {
        struct wl_array state_array;
        wl_array_init(&state_array);
        if (states) {
            uint32_t *state = wl_array_add(&state_array, sizeof(uint32_t));
            *state = states;
        }
        xdg_toplevel_send_configure(toplevel->resource, width, height, &state_array);
        wl_array_release(&state_array);
    }
}

void xdg_toplevel_send_close(struct xdg_toplevel *toplevel) {
    if (toplevel && toplevel->resource) {
        xdg_toplevel_send_close(toplevel->resource);
    }
}

void xdg_popup_send_configure(struct xdg_popup *popup, int32_t x, int32_t y, int32_t width, int32_t height) {
    if (popup && popup->resource) {
        xdg_popup_send_configure(popup->resource, x, y, width, height);
    }
}

void xdg_popup_send_done(struct xdg_popup *popup) {
    if (popup && popup->resource) {
        xdg_popup_send_done(popup->resource);
    }
}

void xdg_toplevel_set_activated(struct xdg_toplevel *toplevel, int activated) {
    if (!toplevel) return;
    toplevel->activated = activated;
    uint32_t states = activated ? XDG_TOPLEVEL_STATE_ACTIVATED : 0;
    xdg_toplevel_send_configure(toplevel, 0, 0, states);
}

struct xdg_surface *xdg_surface_from_wl_surface(struct wl_surface *surface) {
    /* This would need a hash map or lookup in practice */
    return NULL;
}

void xdg_surface_map(struct xdg_surface *surface) {
    if (surface) {
        uint32_t serial = wl_display_get_serial(wl_client_get_display(wl_resource_get_client(surface->resource)));
        xdg_surface_send_configure(surface, serial);
    }
}

void xdg_surface_unmap(struct xdg_surface *surface) {
    /* Nothing specific needed for unmap in xdg_shell */
}
