#include "wl-data-device-manager.h"
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "data-device"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

/* Forward declarations */
static void data_device_manager_destroy(struct wl_client *client, struct wl_resource *resource);
static void data_device_manager_create_data_source(struct wl_client *client, struct wl_resource *resource, uint32_t id);
static void data_device_manager_get_data_device(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *seat_resource);

static const struct wl_data_device_manager_interface data_device_manager_impl = {
    .destroy = data_device_manager_destroy,
    .create_data_source = data_device_manager_create_data_source,
    .get_data_device = data_device_manager_get_data_device,
};

static void data_source_destroy(struct wl_client *client, struct wl_resource *resource);
static void data_source_offer(struct wl_client *client, struct wl_resource *resource, const char *mime_type);
static void data_source_set_actions(struct wl_client *client, struct wl_resource *resource, uint32_t dnd_actions);

static const struct wl_data_source_interface data_source_impl = {
    .destroy = data_source_destroy,
    .offer = data_source_offer,
    .set_actions = data_source_set_actions,
};

static void data_device_destroy(struct wl_client *client, struct wl_resource *resource);
static void data_device_start_drag(struct wl_client *client, struct wl_resource *resource, struct wl_resource *source_resource, struct wl_resource *origin_resource, struct wl_resource *icon_resource, uint32_t serial);
static void data_device_set_selection(struct wl_client *client, struct wl_resource *resource, struct wl_resource *source_resource, uint32_t serial);

static const struct wl_data_device_interface data_device_impl = {
    .destroy = data_device_destroy,
    .start_drag = data_device_start_drag,
    .set_selection = data_device_set_selection,
};

static void data_offer_destroy(struct wl_client *client, struct wl_resource *resource);
static void data_offer_accept(struct wl_client *client, struct wl_resource *resource, uint32_t serial, const char *mime_type);
static void data_offer_receive(struct wl_client *client, struct wl_resource *resource, const char *mime_type, int32_t fd);
static void data_offer_finish(struct wl_client *client, struct wl_resource *resource);
static void data_offer_set_actions(struct wl_client *client, struct wl_resource *resource, uint32_t dnd_actions, uint32_t preferred_action);

static const struct wl_data_offer_interface data_offer_impl = {
    .destroy = data_offer_destroy,
    .accept = data_offer_accept,
    .receive = data_offer_receive,
    .finish = data_offer_finish,
    .set_actions = data_offer_set_actions,
};

/* Resource destruction */
static void data_source_resource_destroy(struct wl_resource *resource) {
    struct lorie_data_source *source = wl_resource_get_user_data(resource);
    if (source) {
        /* Free mime types */
        struct wl_list *item, *tmp;
        wl_list_for_each_safe(item, tmp, &source->mime_types, link) {
            free(item);
        }
        free(source);
    }
}

static void data_offer_resource_destroy(struct wl_resource *resource) {
    struct lorie_data_offer *offer = wl_resource_get_user_data(resource);
    if (offer) {
        wl_list_remove(&offer->link);
        free(offer);
    }
}

static void data_device_resource_destroy(struct wl_resource *resource) {
    struct lorie_data_device *device = wl_resource_get_user_data(resource);
    if (device) {
        wl_list_remove(&device->link);
        free(device);
    }
}

/* Handlers */
static void data_device_manager_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void data_device_manager_create_data_source(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    struct lorie_data_source *source = calloc(1, sizeof(*source));
    if (!source) {
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_list_init(&source->mime_types);

    source->resource = wl_resource_create(client, &wl_data_source_interface, wl_resource_get_version(resource), id);
    if (!source->resource) {
        free(source);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(source->resource, &data_source_impl, source, data_source_resource_destroy);
}

static void data_device_manager_get_data_device(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *seat_resource) {
    struct lorie_data_device_manager *manager = wl_resource_get_user_data(resource);
    struct lorie_seat *seat = wl_resource_get_user_data(seat_resource);

    struct lorie_data_device *device = calloc(1, sizeof(*device));
    if (!device) {
        wl_resource_post_no_memory(resource);
        return;
    }

    device->seat = seat;
    device->resource = wl_resource_create(client, &wl_data_device_interface, wl_resource_get_version(resource), id);
    if (!device->resource) {
        free(device);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(device->resource, &data_device_impl, device, data_device_resource_destroy);
    wl_list_insert(&manager->compositor->data_devices, &device->link);
}

/* Data source handlers */
static void data_source_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void data_source_offer(struct wl_client *client, struct wl_resource *resource, const char *mime_type) {
    struct lorie_data_source *source = wl_resource_get_user_data(resource);
    /* Store mime type */
    LOGD("Source offers: %s", mime_type);
    /* TODO: Add to list */
}

static void data_source_set_actions(struct wl_client *client, struct wl_resource *resource, uint32_t dnd_actions) {
    struct lorie_data_source *source = wl_resource_get_user_data(resource);
    source->actions = dnd_actions;
}

/* Data offer handlers */
static void data_offer_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void data_offer_accept(struct wl_client *client, struct wl_resource *resource, uint32_t serial, const char *mime_type) {
    struct lorie_data_offer *offer = wl_resource_get_user_data(resource);
    offer->accepted = (mime_type != NULL);
    wl_data_source_send_send(offer->source->resource, mime_type);
}

static void data_offer_receive(struct wl_client *client, struct wl_resource *resource, const char *mime_type, int32_t fd) {
    struct lorie_data_offer *offer = wl_resource_get_user_data(resource);
    /* Send data to the fd */
    wl_data_source_send_send(offer->source->resource, mime_type);
    close(fd);
}

static void data_offer_finish(struct wl_client *client, struct wl_resource *resource) {
    struct lorie_data_offer *offer = wl_resource_get_user_data(resource);
    wl_data_source_send_dnd_finished(offer->source->resource);
}

static void data_offer_set_actions(struct wl_client *client, struct wl_resource *resource, uint32_t dnd_actions, uint32_t preferred_action) {
    struct lorie_data_offer *offer = wl_resource_get_user_data(resource);
    offer->actions = dnd_actions;
    offer->preferred_action = preferred_action;
    wl_data_source_send_action(offer->source->resource, preferred_action);
}

/* Data device handlers */
static void data_device_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void data_device_start_drag(struct wl_client *client, struct wl_resource *resource, struct wl_resource *source_resource, struct wl_resource *origin_resource, struct wl_resource *icon_resource, uint32_t serial) {
    /* Drag and drop not supported on Android for now */
}

static void data_device_set_selection(struct wl_client *client, struct wl_resource *resource, struct wl_resource *source_resource, uint32_t serial) {
    struct lorie_data_device *device = wl_resource_get_user_data(resource);
    struct lorie_data_source *source = source_resource ? wl_resource_get_user_data(source_resource) : NULL;

    if (device->selection_source && device->selection_source != source) {
        wl_data_source_send_cancelled(device->selection_source->resource);
    }

    device->selection_source = source;

    if (source) {
        /* Create offer for all data devices */
        /* TODO: Iterate and send data_offer */
        LOGD("New selection set");
    }

    /* Sync with Android clipboard */
    lorie_clipboard_announce();
}

/* Binding */
static void data_device_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct lorie_data_device_manager *manager = data;
    struct wl_resource *resource = wl_resource_create(client, &wl_data_device_manager_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &data_device_manager_impl, manager, NULL);
    wl_list_insert(&manager->resources, resource);
}

/* Public API */
struct lorie_data_device_manager *lorie_data_device_manager_create(struct wl_display *display, struct lorie_compositor *compositor) {
    struct lorie_data_device_manager *manager = calloc(1, sizeof(*manager));
    if (!manager) {
        return NULL;
    }

    manager->compositor = compositor;
    wl_list_init(&manager->resources);

    manager->global = wl_global_create(display, &wl_data_device_manager_interface, 3, manager, data_device_manager_bind);
    if (!manager->global) {
        free(manager);
        return NULL;
    }

    LOGD("wl_data_device_manager created");
    return manager;
}

void lorie_data_device_manager_destroy(struct lorie_data_device_manager *manager) {
    if (!manager) return;
    wl_global_destroy(manager->global);
    free(manager);
}

void lorie_data_device_set_selection(struct lorie_data_device *device, struct lorie_data_source *source, uint32_t serial) {
    device->selection_source = source;
}

void lorie_data_device_send_selection(struct lorie_data_device *device) {
    if (device->selection_source) {
        struct lorie_data_offer *offer = calloc(1, sizeof(*offer));
        offer->source = device->selection_source;

        struct wl_resource *offer_resource = wl_resource_create(
            wl_resource_get_client(device->resource),
            &wl_data_offer_interface,
            wl_resource_get_version(device->resource),
            0
        );
        wl_resource_set_implementation(offer_resource, &data_offer_impl, offer, data_offer_resource_destroy);

        wl_data_device_send_data_offer(device->resource, offer_resource);
        /* Send mime types */
        wl_data_offer_send_offer(offer_resource, "text/plain");
        wl_data_offer_offer(offer_resource, "text/plain;charset=utf-8");
        wl_data_device_send_selection(device->resource, offer_resource);
    } else {
        wl_data_device_send_selection(device->resource, NULL);
    }
}

void lorie_data_device_clear_selection(struct lorie_data_device *device) {
    device->selection_source = NULL;
    wl_data_device_send_selection(device->resource, NULL);
}

/* Android clipboard integration stubs */
void lorie_clipboard_set_text(const char *text) {
    LOGD("Setting clipboard text: %s", text ? text : "(null)");
    /* TODO: Call Android JNI to set clipboard */
}

char *lorie_clipboard_get_text(void) {
    /* TODO: Call Android JNI to get clipboard */
    return NULL;
}

void lorie_clipboard_announce(void) {
    /* TODO: Notify all data devices about new clipboard content */
}
