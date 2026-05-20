#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <wayland-server-core.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lorie_compositor;
struct lorie_surface;

/* xdg_wm_base - core desktop window management */
struct xdg_wm_base {
    struct wl_global *global;
    struct lorie_compositor *compositor;
    struct wl_list resources;
};

struct xdg_wm_base_resource {
    struct wl_resource *resource;
    struct wl_list link;
};

struct xdg_surface {
    struct wl_resource *resource;
    struct lorie_surface *lorie_surface;
    struct wl_surface *wl_surface;
    struct wl_list link;
    uint32_t configure_serial;
    int has_role;
};

struct xdg_toplevel {
    struct wl_resource *resource;
    struct xdg_surface *xdg_surface;
    struct wl_list link;
    char *title;
    char *app_id;
    int32_t width, height;
    int maximized;
    int fullscreen;
    int activated;
};

struct xdg_popup {
    struct wl_resource *resource;
    struct xdg_surface *xdg_surface;
    struct xdg_surface *parent;
    struct wl_list link;
    int32_t x, y;
    int32_t width, height;
};

struct xdg_positioner {
    struct wl_resource *resource;
    int32_t width, height;
    int32_t anchor_rect_x, anchor_rect_y;
    int32_t anchor_rect_width, anchor_rect_height;
    uint32_t anchor;
    uint32_t gravity;
    uint32_t constraint_adjustment;
    int32_t offset_x, offset_y;
};

/* Create/destroy xdg_wm_base global */
struct xdg_wm_base *xdg_wm_base_create(struct wl_display *display, struct lorie_compositor *compositor);
void xdg_wm_base_destroy(struct xdg_wm_base *xdg_wm_base);

/* Configure surfaces */
void xdg_surface_send_configure(struct xdg_surface *surface, uint32_t serial);
void xdg_toplevel_send_configure(struct xdg_toplevel *toplevel, int32_t width, int32_t height, uint32_t states);
void xdg_toplevel_send_close(struct xdg_toplevel *toplevel);
void xdg_popup_send_configure(struct xdg_popup *popup, int32_t x, int32_t y, int32_t width, int32_t height);
void xdg_popup_send_done(struct xdg_popup *popup);

/* Toplevel state helpers */
void xdg_toplevel_set_maximized(struct xdg_toplevel *toplevel);
void xdg_toplevel_unset_maximized(struct xdg_toplevel *toplevel);
void xdg_toplevel_set_fullscreen(struct xdg_toplevel *toplevel);
void xdg_toplevel_unset_fullscreen(struct xdg_toplevel *toplevel);
void xdg_toplevel_set_activated(struct xdg_toplevel *toplevel, int activated);

/* xdg_surface lifecycle */
struct xdg_surface *xdg_surface_from_wl_surface(struct wl_surface *surface);
void xdg_surface_map(struct xdg_surface *surface);
void xdg_surface_unmap(struct xdg_surface *surface);

#ifdef __cplusplus
}
#endif
