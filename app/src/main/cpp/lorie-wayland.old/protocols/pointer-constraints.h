#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <wayland-server-core.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lorie_seat;

/* zwp_pointer_constraints_v1 - pointer constraints for games */
struct zwp_pointer_constraints_v1 {
    struct wl_global *global;
    struct wl_list resources;
};

struct zwp_locked_pointer_v1 {
    struct wl_resource *resource;
    struct wl_list link;
    struct wl_resource *pointer_resource;
    struct wl_surface *surface;
    int32_t x, y;
    uint32_t lifetime;
};

struct zwp_confined_pointer_v1 {
    struct wl_resource *resource;
    struct wl_list link;
    struct wl_resource *pointer_resource;
    struct wl_surface *surface;
    uint32_t lifetime;
};

/* Create/destroy pointer constraints */
struct zwp_pointer_constraints_v1 *zwp_pointer_constraints_v1_create(struct wl_display *display);
void zwp_pointer_constraints_v1_destroy(struct zwp_pointer_constraints_v1 *constraints);

/* Lock/unlock pointer */
struct zwp_locked_pointer_v1 *zwp_pointer_constraints_lock_pointer(struct zwp_pointer_constraints_v1 *constraints,
                                                                    struct wl_client *client,
                                                                    struct wl_resource *pointer_resource,
                                                                    struct wl_surface *surface,
                                                                    uint32_t id,
                                                                    uint32_t lifetime);
void zwp_locked_pointer_v1_unlock(struct zwp_locked_pointer_v1 *locked);

/* Confine/unconfine pointer */
struct zwp_confined_pointer_v1 *zwp_pointer_constraints_confine_pointer(struct zwp_pointer_constraints_v1 *constraints,
                                                                         struct wl_client *client,
                                                                         struct wl_resource *pointer_resource,
                                                                         struct wl_surface *surface,
                                                                         uint32_t id,
                                                                         uint32_t lifetime);
void zwp_confined_pointer_v1_unconfine(struct zwp_confined_pointer_v1 *confined);

#ifdef __cplusplus
}
#endif
