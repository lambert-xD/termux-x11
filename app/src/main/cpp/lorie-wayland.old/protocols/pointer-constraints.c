#include "pointer-constraints.h"
#include <stdlib.h>
#include <android/log.h>

#define LOG_TAG "pointer-constraints"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

static void pointer_constraints_destroy(struct wl_client *client, struct wl_resource *resource);
static void pointer_constraints_lock_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *pointer_resource, struct wl_resource *region_resource, uint32_t lifetime);
static void pointer_constraints_confine_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *pointer_resource, struct wl_resource *region_resource, uint32_t lifetime);

static const struct zwp_pointer_constraints_v1_interface pointer_constraints_impl = {
    .destroy = pointer_constraints_destroy,
    .lock_pointer = pointer_constraints_lock_pointer,
    .confine_pointer = pointer_constraints_confine_pointer,
};

static void locked_pointer_destroy(struct wl_client *client, struct wl_resource *resource);
static void locked_pointer_set_cursor_position_hint(struct wl_client *client, struct wl_resource *resource, wl_fixed_t surface_x, wl_fixed_t surface_y);
static void locked_pointer_set_region(struct wl_client *client, struct wl_resource *resource, struct wl_resource *region_resource);

static const struct zwp_locked_pointer_v1_interface locked_pointer_impl = {
    .destroy = locked_pointer_destroy,
    .set_cursor_position_hint = locked_pointer_set_cursor_position_hint,
    .set_region = locked_pointer_set_region,
};

static void confined_pointer_destroy(struct wl_client *client, struct wl_resource *resource);
static void confined_pointer_set_region(struct wl_client *client, struct wl_resource *resource, struct wl_resource *region_resource);

static const struct zwp_confined_pointer_v1_interface confined_pointer_impl = {
    .destroy = confined_pointer_destroy,
    .set_region = confined_pointer_set_region,
};

static void locked_pointer_resource_destroy(struct wl_resource *resource) {
    struct zwp_locked_pointer_v1 *locked = wl_resource_get_user_data(resource);
    if (locked) {
        wl_list_remove(&locked->link);
        free(locked);
    }
}

static void confined_pointer_resource_destroy(struct wl_resource *resource) {
    struct zwp_confined_pointer_v1 *confined = wl_resource_get_user_data(resource);
    if (confined) {
        wl_list_remove(&confined->link);
        free(confined);
    }
}

static void pointer_constraints_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void pointer_constraints_lock_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *pointer_resource, struct wl_resource *region_resource, uint32_t lifetime) {
    struct zwp_pointer_constraints_v1 *constraints = wl_resource_get_user_data(resource);
    struct wl_surface *surface = wl_resource_get_user_data(surface_resource);

    struct zwp_locked_pointer_v1 *locked = calloc(1, sizeof(*locked));
    if (!locked) {
        wl_resource_post_no_memory(resource);
        return;
    }

    locked->surface = surface;
    locked->pointer_resource = pointer_resource;
    locked->lifetime = lifetime;
    locked->resource = wl_resource_create(client, &zwp_locked_pointer_v1_interface, wl_resource_get_version(resource), id);
    if (!locked->resource) {
        free(locked);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(locked->resource, &locked_pointer_impl, locked, locked_pointer_resource_destroy);
    wl_list_insert(&constraints->locked_pointers, &locked->link);

    /* On Android, locking the pointer means keeping it within the surface bounds */
    zwp_locked_pointer_v1_send_locked(locked->resource);
    LOGD("Pointer locked on surface");
}

static void pointer_constraints_confine_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *pointer_resource, struct wl_resource *region_resource, uint32_t lifetime) {
    struct zwp_pointer_constraints_v1 *constraints = wl_resource_get_user_data(resource);
    struct wl_surface *surface = wl_resource_get_user_data(surface_resource);

    struct zwp_confined_pointer_v1 *confined = calloc(1, sizeof(*confined));
    if (!confined) {
        wl_resource_post_no_memory(resource);
        return;
    }

    confined->surface = surface;
    confined->pointer_resource = pointer_resource;
    confined->lifetime = lifetime;
    confined->resource = wl_resource_create(client, &zwp_confined_pointer_v1_interface, wl_resource_get_version(resource), id);
    if (!confined->resource) {
        free(confined);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(confined->resource, &confined_pointer_impl, confined, confined_pointer_resource_destroy);
    wl_list_insert(&constraints->confined_pointers, &confined->link);

    zwp_confined_pointer_v1_send_confined(confined->resource);
    LOGD("Pointer confined to surface");
}

static void locked_pointer_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void locked_pointer_set_cursor_position_hint(struct wl_client *client, struct wl_resource *resource, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    struct zwp_locked_pointer_v1 *locked = wl_resource_get_user_data(resource);
    locked->x = wl_fixed_to_int(surface_x);
    locked->y = wl_fixed_to_int(surface_y);
}

static void locked_pointer_set_region(struct wl_client *client, struct wl_resource *resource, struct wl_resource *region_resource) {
    /* Region-based locking not implemented for Android */
}

static void confined_pointer_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void confined_pointer_set_region(struct wl_client *client, struct wl_resource *resource, struct wl_resource *region_resource) {
    /* Region-based confinement not implemented for Android */
}

static void pointer_constraints_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct zwp_pointer_constraints_v1 *constraints = data;
    struct wl_resource *resource = wl_resource_create(client, &zwp_pointer_constraints_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &pointer_constraints_impl, constraints, NULL);
    wl_list_insert(&constraints->resources, resource);
}

struct zwp_pointer_constraints_v1 *zwp_pointer_constraints_v1_create(struct wl_display *display) {
    struct zwp_pointer_constraints_v1 *constraints = calloc(1, sizeof(*constraints));
    if (!constraints) return NULL;

    wl_list_init(&constraints->resources);
    wl_list_init(&constraints->locked_pointers);
    wl_list_init(&constraints->confined_pointers);

    constraints->global = wl_global_create(display, &zwp_pointer_constraints_v1_interface, 1, constraints, pointer_constraints_bind);
    if (!constraints->global) {
        free(constraints);
        return NULL;
    }

    LOGD("zwp_pointer_constraints_v1 created");
    return constraints;
}

void zwp_pointer_constraints_v1_destroy(struct zwp_pointer_constraints_v1 *constraints) {
    if (!constraints) return;
    wl_global_destroy(constraints->global);
    free(constraints);
}

struct zwp_locked_pointer_v1 *zwp_pointer_constraints_lock_pointer(struct zwp_pointer_constraints_v1 *constraints,
                                                                    struct wl_client *client,
                                                                    struct wl_resource *pointer_resource,
                                                                    struct wl_surface *surface,
                                                                    uint32_t id,
                                                                    uint32_t lifetime) {
    /* This would be called internally to lock the pointer */
    return NULL;
}

void zwp_locked_pointer_v1_unlock(struct zwp_locked_pointer_v1 *locked) {
    if (locked && locked->resource) {
        zwp_locked_pointer_v1_send_unlocked(locked->resource);
    }
}

struct zwp_confined_pointer_v1 *zwp_pointer_constraints_confine_pointer(struct zwp_pointer_constraints_v1 *constraints,
                                                                         struct wl_client *client,
                                                                         struct wl_resource *pointer_resource,
                                                                         struct wl_surface *surface,
                                                                         uint32_t id,
                                                                         uint32_t lifetime) {
    return NULL;
}

void zwp_confined_pointer_v1_unconfine(struct zwp_confined_pointer_v1 *confined) {
    if (confined && confined->resource) {
        zwp_confined_pointer_v1_send_unconfined(confined->resource);
    }
}
