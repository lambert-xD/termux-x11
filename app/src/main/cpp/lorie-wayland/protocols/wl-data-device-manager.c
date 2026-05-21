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
    struct lorie_clipboard *clipboard;
};

struct lorie_mime_type {
    struct wl_list link;
    char *type;
};

struct lorie_data_offer {
    struct wl_resource *resource;
    struct lorie_data_source *source;
    struct lorie_clipboard *clipboard;
    int is_android_source;
};

struct lorie_data_device {
    struct wl_list link;
    struct wl_resource *resource;
    struct wl_client *client;
    struct lorie_compositor *compositor;
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
    data_source_destroy,
    data_source_set_actions,
};

static void data_source_handle_destroy(struct wl_resource *resource) {
    struct lorie_data_source *source = wl_resource_get_user_data(resource);
    if (source) {
        if (source->clipboard) {
            lorie_clipboard_clear_source(source->clipboard, resource);
        }
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
    if (offer && offer->is_android_source && offer->clipboard) {
        if (lorie_clipboard_mime_type_supported(mime_type)) {
            const char *text = NULL;
            size_t len = 0;
            text = lorie_clipboard_get_android_text(offer->clipboard, &len);
            if (text && len > 0) {
                write(fd, text, len);
            }
        }
        close(fd);
        (void)client;
        return;
    }
    if (offer && offer->source && lorie_clipboard_mime_type_supported(mime_type)) {
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
    struct lorie_data_device *device = wl_resource_get_user_data(resource);

    /* Forward to clipboard for Wayland→Android text transfer */
    if (device->compositor && device->compositor->clipboard) {
        lorie_clipboard_set_selection(device->compositor->clipboard, source_resource);
    }

    if (!source_resource) {
        wl_data_device_send_selection(device->resource, NULL);
        return;
    }

    struct lorie_data_source *source = wl_resource_get_user_data(source_resource);
    if (source) source->clipboard = device->compositor->clipboard;
    struct lorie_data_offer *offer = calloc(1, sizeof(*offer));
    if (!offer) { wl_client_post_no_memory(client); return; }
    uint32_t id = wl_display_next_serial(wl_client_get_display(device->client));
    offer->resource = wl_resource_create(device->client, &wl_data_offer_interface, 3, id);
    if (!offer->resource) { free(offer); wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(offer->resource, &data_offer_impl, offer, data_offer_handle_destroy);
    offer->source = source;
    wl_data_device_send_data_offer(device->resource, offer->resource);
    wl_data_offer_send_offer(offer->resource, "text/plain;charset=utf-8");
    wl_data_offer_send_offer(offer->resource, "text/plain");
    wl_data_device_send_selection(device->resource, offer->resource);
    (void)serial;
}

/* Send Android clipboard text as selection to all data devices */
void lorie_clipboard_send_android_selection(struct lorie_compositor *c) {
    if (!c || !c->clipboard) return;

    size_t len = 0;
    const char *text = lorie_clipboard_get_android_text(c->clipboard, &len);
    if (!text || len == 0) return;

    struct lorie_data_device *device;
    wl_list_for_each(device, &c->data_devices, link) {
        struct lorie_data_offer *offer = calloc(1, sizeof(*offer));
        if (!offer) continue;
        uint32_t id = wl_display_next_serial(wl_client_get_display(device->client));
        offer->resource = wl_resource_create(device->client, &wl_data_offer_interface, 3, id);
        if (!offer->resource) { free(offer); continue; }
        wl_resource_set_implementation(offer->resource, &data_offer_impl, offer, data_offer_handle_destroy);
        offer->clipboard = c->clipboard;
        offer->is_android_source = 1;
        wl_data_device_send_data_offer(device->resource, offer->resource);
        wl_data_offer_send_offer(offer->resource, "text/plain;charset=utf-8");
        wl_data_offer_send_offer(offer->resource, "text/plain");
        wl_data_device_send_selection(device->resource, offer->resource);
    }
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
    if (device && device->compositor) {
        wl_list_remove(&device->link);
    }
    free(device);
}

/* --- manager --- */

static void manager_create_data_source(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    struct lorie_data_source *source = calloc(1, sizeof(*source));
    if (!source) { wl_client_post_no_memory(client); return; }
    source->resource = wl_resource_create(client, &wl_data_source_interface, 3, id);
    if (!source->resource) { free(source); wl_client_post_no_memory(client); return; }
    wl_list_init(&source->mime_types);
    source->clipboard = NULL;
    wl_resource_set_implementation(source->resource, &data_source_impl, source, data_source_handle_destroy);
    (void)resource;
}

static void manager_get_data_device(struct wl_client *client, struct wl_resource *resource,
                                    uint32_t id, struct wl_resource *seat) {
    struct lorie_compositor *compositor = wl_resource_get_user_data(resource);
    struct lorie_data_device *device = calloc(1, sizeof(*device));
    if (!device) { wl_client_post_no_memory(client); return; }
    device->client = client;
    device->compositor = compositor;
    wl_list_init(&device->link);
    device->resource = wl_resource_create(client, &wl_data_device_interface, 3, id);
    if (!device->resource) { free(device); wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(device->resource, &data_device_impl, device, data_device_handle_destroy);
    wl_list_insert(&compositor->data_devices, &device->link);
    (void)seat;
}

static void manager_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static const struct wl_data_device_manager_interface manager_impl = {
    manager_create_data_source,
    manager_get_data_device,
    manager_destroy,
};

static void manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(client, &wl_data_device_manager_interface, version, id);
    if (!resource) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

struct wl_global *lorie_data_device_manager_create(struct wl_display *display, struct lorie_compositor *c) {
    return wl_global_create(display, &wl_data_device_manager_interface, 3, c, manager_bind);
}
