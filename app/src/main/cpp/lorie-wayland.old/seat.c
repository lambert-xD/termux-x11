#include "compositor.h"
#include <android/log.h>

#define LOG_TAG "LorieWayland"
#define log(prio, ...) __android_log_print(ANDROID_LOG_ ## prio, LOG_TAG, __VA_ARGS__)

/* ============================================================================
 * Pointer protocol handlers
 * ============================================================================ */

static void pointer_set_cursor(struct wl_client* client, struct wl_resource* resource,
                               uint32_t serial, struct wl_resource* surface,
                               int32_t hotspot_x, int32_t hotspot_y) {
    /* TODO: cursor surface tracking */
}

static void pointer_release(struct wl_client* client, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

const struct wl_pointer_interface pointer_interface = {
    .set_cursor = pointer_set_cursor,
    .release = pointer_release,
};

/* ============================================================================
 * Keyboard protocol handlers
 * ============================================================================ */

static void keyboard_release(struct wl_client* client, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

const struct wl_keyboard_interface keyboard_interface = {
    .release = keyboard_release,
};

/* ============================================================================
 * Touch protocol handlers
 * ============================================================================ */

static void touch_release(struct wl_client* client, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

const struct wl_touch_interface touch_interface = {
    .release = touch_release,
};

/* ============================================================================
 * Seat protocol handlers
 * ============================================================================ */

static void seat_get_pointer(struct wl_client* client, struct wl_resource* resource,
                             uint32_t id) {
    struct lorie_seat* seat = wl_resource_get_user_data(resource);
    struct wl_resource* pointer = wl_resource_create(client, &wl_pointer_interface,
                                                     wl_resource_get_version(resource), id);
    if (!pointer) {
        wl_resource_post_no_memory(resource);
        return;
    }
    wl_resource_set_implementation(pointer, &pointer_interface, seat, NULL);
    wl_list_insert(&seat->pointer_resources, &pointer->link);
}

static void seat_get_keyboard(struct wl_client* client, struct wl_resource* resource,
                              uint32_t id) {
    struct lorie_seat* seat = wl_resource_get_user_data(resource);
    struct wl_resource* keyboard = wl_resource_create(client, &wl_keyboard_interface,
                                                      wl_resource_get_version(resource), id);
    if (!keyboard) {
        wl_resource_post_no_memory(resource);
        return;
    }
    wl_resource_set_implementation(keyboard, &keyboard_interface, seat, NULL);
    wl_list_insert(&seat->keyboard_resources, &keyboard->link);

    /* Send keymap and repeat info */
    wl_keyboard_send_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP, -1, 0);
    wl_keyboard_send_repeat_info(keyboard, 40, 400);
}

static void seat_get_touch(struct wl_client* client, struct wl_resource* resource,
                           uint32_t id) {
    struct lorie_seat* seat = wl_resource_get_user_data(resource);
    struct wl_resource* touch = wl_resource_create(client, &wl_touch_interface,
                                                   wl_resource_get_version(resource), id);
    if (!touch) {
        wl_resource_post_no_memory(resource);
        return;
    }
    wl_resource_set_implementation(touch, &touch_interface, seat, NULL);
    wl_list_insert(&seat->touch_resources, &touch->link);
}

static void seat_release(struct wl_client* client, struct wl_resource* resource) {
    wl_resource_destroy(resource);
}

const struct wl_seat_interface seat_interface = {
    .get_pointer = seat_get_pointer,
    .get_keyboard = seat_get_keyboard,
    .get_touch = seat_get_touch,
    .release = seat_release,
};

/* ============================================================================
 * Seat bind
 * ============================================================================ */

void seat_bind(struct wl_client* client, void* data,
               uint32_t version, uint32_t id) {
    struct lorie_seat* seat = data;
    struct wl_resource* resource = wl_resource_create(client, &wl_seat_interface,
                                                      version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &seat_interface, seat, NULL);
    wl_seat_send_capabilities(resource, seat->capabilities);
    wl_seat_send_name(resource, "default");
}
