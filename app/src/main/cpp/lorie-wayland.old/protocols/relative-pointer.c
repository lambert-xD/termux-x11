#include "relative-pointer.h"
#include <stdlib.h>
#include <android/log.h>

#define LOG_TAG "relative-pointer"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

static void relative_pointer_manager_destroy(struct wl_client *client, struct wl_resource *resource);
static void relative_pointer_manager_get_relative_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *pointer_resource);

static const struct zwp_relative_pointer_manager_v1_interface relative_pointer_manager_impl = {
    .destroy = relative_pointer_manager_destroy,
    .get_relative_pointer = relative_pointer_manager_get_relative_pointer,
};

static void relative_pointer_destroy(struct wl_client *client, struct wl_resource *resource);

static const struct zwp_relative_pointer_v1_interface relative_pointer_impl = {
    .destroy = relative_pointer_destroy,
};

static void relative_pointer_resource_destroy(struct wl_resource *resource) {
    struct zwp_relative_pointer_v1 *rel = wl_resource_get_user_data(resource);
    if (rel) {
        wl_list_remove(&rel->link);
        free(rel);
    }
}

static void relative_pointer_manager_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void relative_pointer_manager_get_relative_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *pointer_resource) {
    struct zwp_relative_pointer_v1 *rel = calloc(1, sizeof(*rel));
    if (!rel) {
        wl_resource_post_no_memory(resource);
        return;
    }

    rel->pointer_resource = pointer_resource;
    rel->resource = wl_resource_create(client, &zwp_relative_pointer_v1_interface, wl_resource_get_version(resource), id);
    if (!rel->resource) {
        free(rel);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(rel->resource, &relative_pointer_impl, rel, relative_pointer_resource_destroy);
    wl_list_insert(&manager->relative_pointers, &rel->link);

    /* Send initial relative motion with zero deltas */
    zwp_relative_pointer_v1_send_relative_motion(rel->resource, 0, 0, wl_fixed_from_double(0), wl_fixed_from_double(0), wl_fixed_from_double(0), wl_fixed_from_double(0));
}

static void relative_pointer_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void relative_pointer_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct zwp_relative_pointer_manager_v1 *manager = data;
    struct wl_resource *resource = wl_resource_create(client, &zwp_relative_pointer_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &relative_pointer_manager_impl, manager, NULL);
    wl_list_insert(&manager->resources, resource);
}

struct zwp_relative_pointer_manager_v1 *zwp_relative_pointer_manager_v1_create(struct wl_display *display) {
    struct zwp_relative_pointer_manager_v1 *manager = calloc(1, sizeof(*manager));
    if (!manager) return NULL;

    wl_list_init(&manager->resources);
    wl_list_init(&manager->relative_pointers);

    manager->global = wl_global_create(display, &zwp_relative_pointer_manager_v1_interface, 1, manager, relative_pointer_manager_bind);
    if (!manager->global) {
        free(manager);
        return NULL;
    }

    LOGD("zwp_relative_pointer_manager_v1 created");
    return manager;
}

void zwp_relative_pointer_manager_v1_destroy(struct zwp_relative_pointer_manager_v1 *manager) {
    if (!manager) return;
    wl_global_destroy(manager->global);
    free(manager);
}

void zwp_relative_pointer_send_motion(struct zwp_relative_pointer_v1 *rel_pointer, uint32_t utime_hi, uint32_t utime_lo,
                                      double dx, double dy, double dx_unaccel, double dy_unaccel) {
    if (rel_pointer && rel_pointer->resource) {
        zwp_relative_pointer_v1_send_relative_motion(rel_pointer->resource, utime_hi, utime_lo,
                                                      wl_fixed_from_double(dx), wl_fixed_from_double(dy),
                                                      wl_fixed_from_double(dx_unaccel), wl_fixed_from_double(dy_unaccel));
    }
}
