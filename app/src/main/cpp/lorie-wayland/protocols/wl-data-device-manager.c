/* wl_data_device_manager protocol implementation */
#include "compositor.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct lorie_data_device_manager {
    struct wl_list resources;
};

struct lorie_data_source {
    struct wl_resource *resource;
    struct wl_list mime_types;
};

struct lorie_mime_type {
    struct wl_list link;
    char *type;
};

struct lorie_data_offer {
    struct wl_resource *resource;
    struct lorie_data_source *source;
};

struct lorie_data_device {
    struct wl_resource *resource;
    struct wl_client *client;
};

/* --- data_source --- */

static void data_source_offer(struct wl_client *client, struct wl_resource *resource, const char *mime_type) {
    struct lorie_data_source *source = wl_resource_get_user_data(resource);
    struct lorie_mime_type *mt = calloc(1, sizeof(*mt));
    if (!mt) { wl_client_post_no_memory(client); return; }
    mt->type = strdup(mime_type);
    wl_list_insert(&source->mime_types, &mt->link);
    (void)client;
}

static void data_source_set_actions(struct wl_client *c, struct wl_resource *r, uint32_t dnd_actions) {
    (void)c; (void)r; (void)dnd_actions;
}

static void data_source_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static const struct wl_data_source_interface data_source_impl = {
    data_source_offer,
    data_source_set_actions,
    data_source_destroy,
};

static void data_source_handle_destroy(struct wl_resource *resource) {
    struct lorie_data_source *source = wl_resource_get_user_data(resource);
    if (source) {
        struct lorie_mime_type *mt, *tmp;
        wl_list_for_each_safe(mt, tmp, &source->mime_types, link) {
            free(mt->type);
            free(mt);
        }
        free(source);
    }
}

/* --- data_offer --- */

static void data_offer_accept(struct wl_client *c, struct wl_resource *r, uint32_t serial, const char *mime_type) {
    (void)c; (void)r; (void)serial; (void)mime_type;
}

static void data_offer_receive(struct wl_client *client, struct wl_resource *resource, const char *mime_type, int32_t fd) {
    struct lorie_data_offer *offer = wl_resource_get_user_data(resource);
    if (offer && offer->source) {
        wl_data_source_send_send(offer->source->resource, mime_type, fd);
    }
    close(fd);
    (void)client;
}

static void data_offer_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static void data_offer_set_actions(struct wl_client *c, struct wl_resource *r,
                                   uint32_t dnd_actions, uint32_t preferred_action) {
    (void)c; (void)r; (void)dnd_actions; (void)preferred_action;
}

static void data_offer_finish(struct wl_client *c, struct wl_resource *r) {
    (void)c; (void)r;
}

static const struct wl_data_offer_interface data_offer_impl = {
    data_offer_accept,
    data_offer_receive,
    data_offer_destroy,
    data_offer_finish,
    data_offer_set_actions,
};

static void data_offer_handle_destroy(struct wl_resource *resource) {
    struct lorie_data_offer *offer = wl_resource_get_user_data(resource);
    free(offer);
}

/* --- data_device --- */

static void data_device_start_drag(struct wl_client *c, struct wl_resource *r,
                                   struct wl_resource *source, struct wl_resource *origin,
                                   struct wl_resource *icon, uint32_t serial) {
    (void)c; (void)r; (void)source; (void)origin; (void)icon; (void)serial;
}

static void data_device_set_selection(struct wl_client *client, struct wl_resource *resource,
                                      struct wl_resource *source_resource, uint32_t serial) {
    if (!source_resource) return;
    struct lorie_data_source *source = wl_resource_get_user_data(source_resource);
    struct lorie_data_device *device = wl_resource_get_user_data(resource);
    struct lorie_data_offer *offer = calloc(1, sizeof(*offer));
    if (!offer) { wl_client_post_no_memory(client); return; }
    uint32_t id = wl_display_next_serial(wl_client_get_display(device->client));
    offer->resource = wl_resource_create(device->client, &wl_data_offer_interface, 3, id);
    if (!offer->resource) { free(offer); wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(offer->resource, &data_offer_impl, offer, data_offer_handle_destroy);
    offer->source = source;
    wl_data_device_send_selection(device->resource, offer->resource);
    (void)serial;
}

static void data_device_release(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static const struct wl_data_device_interface data_device_impl = {
    data_device_start_drag,
    data_device_set_selection,
    data_device_release,
};

static void data_device_handle_destroy(struct wl_resource *resource) {
    struct lorie_data_device *device = wl_resource_get_user_data(resource);
    free(device);
}

/* --- manager --- */

static void manager_create_data_source(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    struct lorie_data_source *source = calloc(1, sizeof(*source));
    if (!source) { wl_client_post_no_memory(client); return; }
    source->resource = wl_resource_create(client, &wl_data_source_interface, 3, id);
    if (!source->resource) { free(source); wl_client_post_no_memory(client); return; }
    wl_list_init(&source->mime_types);
    wl_resource_set_implementation(source->resource, &data_source_impl, source, data_source_handle_destroy);
    (void)resource;
}

static void manager_get_data_device(struct wl_client *client, struct wl_resource *resource,
                                    uint32_t id, struct wl_resource *seat) {
    struct lorie_data_device *device = calloc(1, sizeof(*device));
    if (!device) { wl_client_post_no_memory(client); return; }
    device->client = client;
    device->resource = wl_resource_create(client, &wl_data_device_interface, 3, id);
    if (!device->resource) { free(device); wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(device->resource, &data_device_impl, device, data_device_handle_destroy);
    (void)resource; (void)seat;
}

static void manager_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static const struct wl_data_device_manager_interface manager_impl = {
    manager_destroy,
    manager_create_data_source,
    manager_get_data_device,
};

static void manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(client, &wl_data_device_manager_interface, version, id);
    if (!resource) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

struct wl_global *lorie_data_device_manager_create(struct wl_display *display) {
    return wl_global_create(display, &wl_data_device_manager_interface, 3, NULL, manager_bind);
}
