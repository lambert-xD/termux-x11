/* wp_viewporter protocol implementation */
#include "viewporter.h"
#include "compositor.h"
#include "stable-viewporter-viewporter.h"
#include <stdlib.h>
#include <string.h>

/* --- Test helpers --- */

void lorie_viewport_set_source(struct lorie_surface *s,
                               double x, double y, double w, double h) {
    if (!s) return;
    s->pending_viewport.src_x = x;
    s->pending_viewport.src_y = y;
    s->pending_viewport.src_w = w;
    s->pending_viewport.src_h = h;
    s->pending_viewport.has_src = 1;
}

void lorie_viewport_set_destination(struct lorie_surface *s,
                                    int32_t w, int32_t h) {
    if (!s) return;
    s->pending_viewport.dst_w = w;
    s->pending_viewport.dst_h = h;
    s->pending_viewport.has_dst = 1;
}

void lorie_viewport_clear(struct lorie_surface *s) {
    if (!s) return;
    memset(&s->pending_viewport, 0, sizeof(s->pending_viewport));
}

int lorie_viewport_validate_source(double x, double y, double w, double h) {
    if (x < 0.0 || y < 0.0 || w <= 0.0 || h <= 0.0)
        return WP_VIEWPORT_ERROR_BAD_VALUE;
    return 0;
}

/* --- wp_viewport implementation --- */

static void viewport_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static void viewport_set_source(struct wl_client *client, struct wl_resource *resource,
                                wl_fixed_t x, wl_fixed_t y, wl_fixed_t w, wl_fixed_t h) {
    struct lorie_surface *s = wl_resource_get_user_data(resource);
    if (!s) {
        wl_resource_post_error(resource, WP_VIEWPORT_ERROR_NO_SURFACE,
                               "surface destroyed");
        return;
    }

    /* Unset: all values are -1.0 in wl_fixed_t */
    if (x == wl_fixed_from_int(-1) && y == wl_fixed_from_int(-1) &&
        w == wl_fixed_from_int(-1) && h == wl_fixed_from_int(-1)) {
        s->pending_viewport.has_src = 0;
        return;
    }

    double dx = wl_fixed_to_double(x);
    double dy = wl_fixed_to_double(y);
    double dw = wl_fixed_to_double(w);
    double dh = wl_fixed_to_double(h);

    if (dx < 0.0 || dy < 0.0 || dw <= 0.0 || dh <= 0.0) {
        wl_resource_post_error(resource, WP_VIEWPORT_ERROR_BAD_VALUE,
                               "negative or zero source values");
        return;
    }

    s->pending_viewport.src_x = dx;
    s->pending_viewport.src_y = dy;
    s->pending_viewport.src_w = dw;
    s->pending_viewport.src_h = dh;
    s->pending_viewport.has_src = 1;
    (void)client;
}

static void viewport_set_destination(struct wl_client *client, struct wl_resource *resource,
                                     int32_t w, int32_t h) {
    struct lorie_surface *s = wl_resource_get_user_data(resource);
    if (!s) {
        wl_resource_post_error(resource, WP_VIEWPORT_ERROR_NO_SURFACE,
                               "surface destroyed");
        return;
    }

    if (w == -1 && h == -1) {
        s->pending_viewport.has_dst = 0;
        return;
    }

    if (w <= 0 || h <= 0) {
        wl_resource_post_error(resource, WP_VIEWPORT_ERROR_BAD_VALUE,
                               "negative or zero destination size");
        return;
    }

    s->pending_viewport.dst_w = w;
    s->pending_viewport.dst_h = h;
    s->pending_viewport.has_dst = 1;
    (void)client;
}

static const struct wp_viewport_interface viewport_impl = {
    viewport_destroy,
    viewport_set_source,
    viewport_set_destination,
};

static void viewport_handle_resource_destroy(struct wl_resource *resource) {
    struct lorie_surface *s = wl_resource_get_user_data(resource);
    if (s) {
        s->viewport_resource = NULL;
        memset(&s->pending_viewport, 0, sizeof(s->pending_viewport));
    }
}

/* --- wp_viewporter implementation --- */

static void viewporter_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static void viewporter_get_viewport(struct wl_client *client, struct wl_resource *resource,
                                    uint32_t id, struct wl_resource *surface_resource) {
    struct lorie_surface *s = wl_resource_get_user_data(surface_resource);
    if (s->viewport_resource) {
        wl_resource_post_error(resource, WP_VIEWPORTER_ERROR_VIEWPORT_EXISTS,
                               "viewport already exists for surface");
        return;
    }

    struct wl_resource *viewport = wl_resource_create(client, &wp_viewport_interface, 1, id);
    if (!viewport) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(viewport, &viewport_impl, s, viewport_handle_resource_destroy);
    s->viewport_resource = viewport;
    (void)resource;
}

static const struct wp_viewporter_interface viewporter_impl = {
    viewporter_destroy,
    viewporter_get_viewport,
};

static void viewporter_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(client, &wp_viewporter_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &viewporter_impl, data, NULL);
}

struct wl_global *lorie_viewporter_create(struct wl_display *display) {
    return wl_global_create(display, &wp_viewporter_interface, 1, NULL, viewporter_bind);
}
