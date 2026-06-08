/* xwayland-shell protocol implementation */
#include "compositor.h"
#include "xwayland-shell.h"
#include "staging-xwayland-shell-xwayland-shell-v1.h"
#include <stdlib.h>
#include <string.h>

/* --- xwayland_surface_v1 implementation --- */

static void xwayland_surface_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static void xwayland_surface_set_serial(struct wl_client *client, struct wl_resource *resource,
                                         uint32_t serial_lo, uint32_t serial_hi) {
    struct lorie_xwayland_surface *xw_surf = wl_resource_get_user_data(resource);
    if (!xw_surf || !xw_surf->surface) {
        wl_resource_post_error(resource, XWAYLAND_SURFACE_V1_ERROR_ALREADY_ASSOCIATED,
                               "surface destroyed");
        return;
    }

    uint64_t serial = ((uint64_t)serial_hi << 32) | serial_lo;
    if (serial == 0) {
        wl_resource_post_error(resource, XWAYLAND_SURFACE_V1_ERROR_INVALID_SERIAL,
                               "serial must be non-zero");
        return;
    }

    if (xw_surf->serial != 0) {
        wl_resource_post_error(resource, XWAYLAND_SURFACE_V1_ERROR_ALREADY_ASSOCIATED,
                               "surface already associated");
        return;
    }

    xw_surf->serial = serial;
    (void)client;
}

static const struct xwayland_surface_v1_interface xwayland_surface_impl = {
    xwayland_surface_set_serial,
    xwayland_surface_destroy,
};

static void xwayland_surface_handle_resource_destroy(struct wl_resource *resource) {
    struct lorie_xwayland_surface *xw_surf = wl_resource_get_user_data(resource);
    if (xw_surf) {
        if (xw_surf->surface)
            xw_surf->surface->xwayland_surface = NULL;
        free(xw_surf);
    }
}

/* --- xwayland_shell_v1 implementation --- */

static void xwayland_shell_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static void xwayland_shell_get_xwayland_surface(struct wl_client *client, struct wl_resource *resource,
                                                 uint32_t id, struct wl_resource *surface_resource) {
    struct lorie_surface *s = wl_resource_get_user_data(surface_resource);
    if (!s) {
        wl_resource_post_error(resource, XWAYLAND_SHELL_V1_ERROR_ROLE,
                               "surface destroyed");
        return;
    }

    /* Reject if surface already has an incompatible role */
    if (s->xdg_surface || s->xwayland_surface) {
        wl_resource_post_error(resource, XWAYLAND_SHELL_V1_ERROR_ROLE,
                               "surface already has a role");
        return;
    }

    struct lorie_xwayland_surface *xw_surf = calloc(1, sizeof(*xw_surf));
    if (!xw_surf) {
        wl_client_post_no_memory(client);
        return;
    }

    xw_surf->resource = wl_resource_create(client, &xwayland_surface_v1_interface, 1, id);
    if (!xw_surf->resource) {
        free(xw_surf);
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(xw_surf->resource, &xwayland_surface_impl, xw_surf,
                                   xwayland_surface_handle_resource_destroy);
    xw_surf->surface = s;
    s->xwayland_surface = xw_surf;
    (void)resource;
}

static const struct xwayland_shell_v1_interface xwayland_shell_impl = {
    xwayland_shell_destroy,
    xwayland_shell_get_xwayland_surface,
};

static void xwayland_shell_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(client, &xwayland_shell_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &xwayland_shell_impl, data, NULL);
}

struct wl_global *lorie_xwayland_shell_create(struct wl_display *display) {
    return wl_global_create(display, &xwayland_shell_v1_interface, 1, NULL, xwayland_shell_bind);
}
