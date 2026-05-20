#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <wayland-server-core.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lorie_seat;

/* zwp_relative_pointer_v1 - relative pointer for games */
struct zwp_relative_pointer_manager_v1 {
    struct wl_global *global;
    struct wl_list resources;
};

struct zwp_relative_pointer_v1 {
    struct wl_resource *resource;
    struct wl_list link;
    struct wl_resource *pointer_resource;
};

/* Create/destroy relative pointer manager */
struct zwp_relative_pointer_manager_v1 *zwp_relative_pointer_manager_v1_create(struct wl_display *display);
void zwp_relative_pointer_manager_v1_destroy(struct zwp_relative_pointer_manager_v1 *manager);

/* Send relative motion event */
void zwp_relative_pointer_send_motion(struct zwp_relative_pointer_v1 *rel_pointer, uint32_t utime_hi, uint32_t utime_lo,
                                      double dx, double dy, double dx_unaccel, double dy_unaccel);

#ifdef __cplusplus
}
#endif
