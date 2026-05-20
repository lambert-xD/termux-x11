#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <wayland-server-core.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lorie_compositor;
struct lorie_seat;

/* wl_data_device_manager - clipboard and DnD */
struct lorie_data_device_manager {
    struct wl_global *global;
    struct lorie_compositor *compositor;
    struct wl_list resources;
};

struct lorie_data_source {
    struct wl_resource *resource;
    struct wl_list mime_types;
    uint32_t actions;
    int accepted;
};

struct lorie_data_offer {
    struct wl_resource *resource;
    struct lorie_data_source *source;
    struct wl_list link;
    uint32_t actions;
    uint32_t preferred_action;
};

struct lorie_data_device {
    struct wl_resource *resource;
    struct lorie_seat *seat;
    struct lorie_data_source *selection_source;
    struct lorie_data_source *drag_source;
    struct wl_surface *focus_surface;
    struct wl_list link;
};

/* Create/destroy data_device_manager global */
struct lorie_data_device_manager *lorie_data_device_manager_create(struct wl_display *display, struct lorie_compositor *compositor);
void lorie_data_device_manager_destroy(struct lorie_data_device_manager *manager);

/* Clipboard operations - integrate with Android clipboard */
void lorie_data_device_set_selection(struct lorie_data_device *device, struct lorie_data_source *source, uint32_t serial);
void lorie_data_device_send_selection(struct lorie_data_device *device);
void lorie_data_device_clear_selection(struct lorie_data_device *device);

/* Android clipboard integration */
void lorie_clipboard_set_text(const char *text);
char *lorie_clipboard_get_text(void);

#ifdef __cplusplus
}
#endif
