/* xdg-shell protocol implementation */
#include "compositor.h"
#include "stable-xdg-shell-xdg-shell.h"
#include <stdlib.h>
#include <string.h>

struct lorie_xdg_surface {
    struct wl_resource *resource;
    struct lorie_surface *surface;
    struct wl_resource *role; /* toplevel or popup */
    uint32_t pending_configure_serial;
    int configured;
};

struct lorie_xdg_toplevel {
    struct wl_resource *resource;
    struct lorie_xdg_surface *xdg_surface;
    char *title;
    char *app_id;
};

static void lorie_xdg_surface_send_configure(struct lorie_xdg_surface *xdg_surf, uint32_t serial) {
    if (xdg_surf && xdg_surf->resource)
        xdg_surface_send_configure(xdg_surf->resource, serial);
}

static void xdg_surface_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static void xdg_surface_ack_configure(struct wl_client *client, struct wl_resource *resource, uint32_t serial) {
    struct lorie_xdg_surface *xdg_surf = wl_resource_get_user_data(resource);
    if (xdg_surf && xdg_surf->configured && xdg_surf->pending_configure_serial == serial)
        xdg_surf->pending_configure_serial = 0;
    (void)client;
}

static void xdg_surface_set_window_geometry(struct wl_client *c, struct wl_resource *r,
                                            int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)c; (void)r; (void)x; (void)y; (void)w; (void)h;
}

static void xdg_surface_get_toplevel(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    struct lorie_xdg_surface *xdg_surf = wl_resource_get_user_data(resource);
    if (xdg_surf->role) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_ROLE, "surface already has role");
        return;
    }
    struct lorie_xdg_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    if (!toplevel) { wl_client_post_no_memory(client); return; }
    toplevel->resource = wl_resource_create(client, &xdg_toplevel_interface, 1, id);
    if (!toplevel->resource) { free(toplevel); wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(toplevel->resource, NULL, toplevel, NULL);
    toplevel->xdg_surface = xdg_surf;
    xdg_surf->role = toplevel->resource;
    /* Defer configure until first commit */
}

static void xdg_surface_get_popup(struct wl_client *client, struct wl_resource *resource,
                                  uint32_t id, struct wl_resource *parent_resource,
                                  struct wl_resource *positioner_resource) {
    struct lorie_xdg_surface *xdg_surf = wl_resource_get_user_data(resource);
    if (xdg_surf->role) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_ROLE, "surface already has role");
        return;
    }
    if (!positioner_resource) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_NOT_CONSTRUCTED, "null positioner");
        return;
    }
    struct wl_resource *popup = wl_resource_create(client, &xdg_popup_interface, 1, id);
    if (!popup) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(popup, NULL, xdg_surf, NULL);
    xdg_surf->role = popup;
    (void)parent_resource;
}

static const struct xdg_surface_interface xdg_surface_impl = {
    xdg_surface_destroy,
    xdg_surface_ack_configure,
    xdg_surface_set_window_geometry,
    xdg_surface_get_toplevel,
    xdg_surface_get_popup,
};

static void xdg_toplevel_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static void xdg_toplevel_set_parent(struct wl_client *c, struct wl_resource *r, struct wl_resource *parent) {
    (void)c; (void)r; (void)parent;
}

static void xdg_toplevel_set_title(struct wl_client *client, struct wl_resource *resource, const char *title) {
    struct lorie_xdg_toplevel *toplevel = wl_resource_get_user_data(resource);
    free(toplevel->title);
    toplevel->title = strdup(title);
    (void)client;
}

static void xdg_toplevel_set_app_id(struct wl_client *client, struct wl_resource *resource, const char *app_id) {
    struct lorie_xdg_toplevel *toplevel = wl_resource_get_user_data(resource);
    free(toplevel->app_id);
    toplevel->app_id = strdup(app_id);
    (void)client;
}

static void xdg_toplevel_show_window_menu(struct wl_client *c, struct wl_resource *r,
                                          struct wl_resource *seat, uint32_t serial,
                                          int32_t x, int32_t y) {
    (void)c; (void)r; (void)seat; (void)serial; (void)x; (void)y;
}

static void xdg_toplevel_move(struct wl_client *c, struct wl_resource *r,
                              struct wl_resource *seat, uint32_t serial) {
    (void)c; (void)r; (void)seat; (void)serial;
}

static void xdg_toplevel_resize(struct wl_client *c, struct wl_resource *r,
                                struct wl_resource *seat, uint32_t serial, uint32_t edges) {
    (void)c; (void)r; (void)seat; (void)serial; (void)edges;
}

static void xdg_toplevel_set_max_size(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h) {
    (void)c; (void)r; (void)w; (void)h;
}

static void xdg_toplevel_set_min_size(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h) {
    (void)c; (void)r; (void)w; (void)h;
}

static void xdg_toplevel_set_maximized(struct wl_client *c, struct wl_resource *r) {
    (void)c; (void)r;
}

static void xdg_toplevel_unset_maximized(struct wl_client *c, struct wl_resource *r) {
    (void)c; (void)r;
}

static void xdg_toplevel_set_fullscreen(struct wl_client *c, struct wl_resource *r, struct wl_resource *output) {
    (void)c; (void)r; (void)output;
}

static void xdg_toplevel_unset_fullscreen(struct wl_client *c, struct wl_resource *r) {
    (void)c; (void)r;
}

static void xdg_toplevel_set_minimized(struct wl_client *c, struct wl_resource *r) {
    (void)c; (void)r;
}

static const struct xdg_toplevel_interface xdg_toplevel_impl = {
    xdg_toplevel_destroy,
    xdg_toplevel_set_parent,
    xdg_toplevel_set_title,
    xdg_toplevel_set_app_id,
    xdg_toplevel_show_window_menu,
    xdg_toplevel_move,
    xdg_toplevel_resize,
    xdg_toplevel_set_max_size,
    xdg_toplevel_set_min_size,
    xdg_toplevel_set_maximized,
    xdg_toplevel_unset_maximized,
    xdg_toplevel_set_fullscreen,
    xdg_toplevel_unset_fullscreen,
    xdg_toplevel_set_minimized,
};

static void xdg_toplevel_handle_resource_destroy(struct wl_resource *resource) {
    struct lorie_xdg_toplevel *toplevel = wl_resource_get_user_data(resource);
    if (toplevel) {
        if (toplevel->xdg_surface) toplevel->xdg_surface->role = NULL;
        free(toplevel->title);
        free(toplevel->app_id);
        free(toplevel);
    }
}

static void xdg_wm_base_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static void xdg_wm_base_create_positioner(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    struct wl_resource *pos = wl_resource_create(c, &xdg_positioner_interface, 1, id);
    if (!pos) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(pos, NULL, NULL, NULL);
    (void)r;
}

static void xdg_wm_base_get_xdg_surface(struct wl_client *client, struct wl_resource *resource,
                                        uint32_t id, struct wl_resource *surface_resource) {
    struct lorie_surface *surface = wl_resource_get_user_data(surface_resource);
    struct lorie_xdg_surface *xdg_surf = calloc(1, sizeof(*xdg_surf));
    if (!xdg_surf) { wl_client_post_no_memory(client); return; }
    xdg_surf->resource = wl_resource_create(client, &xdg_surface_interface, 1, id);
    if (!xdg_surf->resource) { free(xdg_surf); wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(xdg_surf->resource, &xdg_surface_impl, xdg_surf, NULL);
    xdg_surf->surface = surface;
    (void)resource;
}

static void xdg_wm_base_pong(struct wl_client *c, struct wl_resource *r, uint32_t serial) {
    (void)c; (void)r; (void)serial;
}

static const struct xdg_wm_base_interface xdg_wm_base_impl = {
    xdg_wm_base_destroy,
    xdg_wm_base_create_positioner,
    xdg_wm_base_get_xdg_surface,
    xdg_wm_base_pong,
};

static void xdg_wm_base_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(client, &xdg_wm_base_interface, version, id);
    if (!resource) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(resource, &xdg_wm_base_impl, data, NULL);
}

/* Public API */
struct wl_global *lorie_xdg_shell_create(struct wl_display *display) {
    return wl_global_create(display, &xdg_wm_base_interface, 1, NULL, xdg_wm_base_bind);
}
