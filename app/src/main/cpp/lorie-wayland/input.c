#include "input.h"
#include "compositor.h"
#include "keymap.h"
#include <stdlib.h>
#include <android/log.h>

#define LOG_TAG "LorieInput"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static void seat_bind(struct wl_client*, void*, uint32_t, uint32_t);

struct lorie_input *lorie_input_init(struct wl_display *d) {
    struct lorie_input *in = calloc(1, sizeof(*in));
    if (!in) return NULL;
    in->display = d;
    in->loop = wl_display_get_event_loop(d);
    wl_list_init(&in->pointers);
    wl_list_init(&in->keyboards);
    wl_list_init(&in->touches);
    pthread_mutex_init(&in->queue_lock, NULL);
    in->seat_global = wl_global_create(d, &wl_seat_interface, 7, in, seat_bind);
    if (!in->seat_global) { free(in); return NULL; }
    in->timer = wl_event_loop_add_timer(in->loop, lorie_input_dispatch, in);
    wl_event_source_timer_update(in->timer, 16);
    return in;
}

void lorie_input_destroy(struct lorie_input *in) {
    if (!in) return;
    if (in->timer) wl_event_source_remove(in->timer);
    if (in->seat_global) wl_global_destroy(in->seat_global);
    pthread_mutex_destroy(&in->queue_lock);
    free(in);
}

static void enqueue(struct lorie_input *in, struct lorie_input_event *ev) {
    pthread_mutex_lock(&in->queue_lock);
    int n = (in->queue_tail + 1) % LORIE_INPUT_QUEUE_SIZE;
    if (n != in->queue_head) {
        in->queue[in->queue_tail] = *ev;
        in->queue_tail = n;
    }
    pthread_mutex_unlock(&in->queue_lock);
}

void lorie_input_pointer_motion(struct lorie_input *in, float x, float y) {
    struct lorie_input_event ev = {.type = LORIE_INPUT_POINTER_MOTION, .motion = {x, y}};
    enqueue(in, &ev);
}

void lorie_input_pointer_button(struct lorie_input *in, uint32_t btn, uint32_t st) {
    struct lorie_input_event ev = {.type = LORIE_INPUT_POINTER_BUTTON, .button = {btn, st}};
    enqueue(in, &ev);
}

void lorie_input_keyboard_key(struct lorie_input *in, uint32_t kc, uint32_t st) {
    struct lorie_input_event ev = {.type = LORIE_INPUT_KEYBOARD_KEY, .key = {kc, st}};
    enqueue(in, &ev);
}

void lorie_input_touch_down(struct lorie_input *in, uint32_t id, float x, float y) {
    struct lorie_input_event ev = {.type = LORIE_INPUT_TOUCH_DOWN, .touch = {id, x, y}};
    enqueue(in, &ev);
}

void lorie_input_touch_up(struct lorie_input *in, uint32_t id) {
    struct lorie_input_event ev = {.type = LORIE_INPUT_TOUCH_UP, .touch = {id, 0, 0}};
    enqueue(in, &ev);
}

void lorie_input_touch_motion(struct lorie_input *in, uint32_t id, float x, float y) {
    struct lorie_input_event ev = {.type = LORIE_INPUT_TOUCH_MOTION, .touch = {id, x, y}};
    enqueue(in, &ev);
}

static void pframe(struct lorie_input *in) {
    if (!in->pointer_dirty) return;
    struct wl_resource *r;
    wl_list_for_each(r, &in->pointers, link)
        if (wl_resource_get_version(r) >= WL_POINTER_FRAME_SINCE_VERSION)
            wl_pointer_send_frame(r);
    in->pointer_dirty = 0;
}

static void tframe(struct lorie_input *in) {
    if (!in->touch_dirty) return;
    struct wl_resource *r;
    wl_list_for_each(r, &in->touches, link)
        if (wl_resource_get_version(r) >= WL_TOUCH_FRAME_SINCE_VERSION)
            wl_touch_send_frame(r);
    in->touch_dirty = 0;
}

static uint32_t ns(struct lorie_input *in) { return wl_display_next_serial(in->display); }

int lorie_input_dispatch(void *data) {
    struct lorie_input *in = data;
    struct lorie_input_event ev[LORIE_INPUT_QUEUE_SIZE];
    int n = 0;
    pthread_mutex_lock(&in->queue_lock);
    while (in->queue_head != in->queue_tail && n < LORIE_INPUT_QUEUE_SIZE)
        ev[n++] = in->queue[in->queue_head], in->queue_head = (in->queue_head + 1) % LORIE_INPUT_QUEUE_SIZE;
    pthread_mutex_unlock(&in->queue_lock);
    for (int i = 0; i < n; i++) {
        struct lorie_input_event *e = &ev[i];
        struct wl_resource *r;
        switch (e->type) {
        case LORIE_INPUT_POINTER_MOTION:
            wl_list_for_each(r, &in->pointers, link)
                wl_pointer_send_motion(r, ns(in), wl_fixed_from_double(e->motion.x), wl_fixed_from_double(e->motion.y));
            in->pointer_dirty = 1; break;
        case LORIE_INPUT_POINTER_BUTTON:
            wl_list_for_each(r, &in->pointers, link)
                wl_pointer_send_button(r, ns(in), e->button.button, e->button.state);
            in->pointer_dirty = 1; break;
        case LORIE_INPUT_KEYBOARD_KEY: {
            uint32_t kc = e->key.key < 304 ? android_to_linux_keycode[e->key.key] : 0;
            wl_list_for_each(r, &in->keyboards, link)
                wl_keyboard_send_key(r, ns(in), 0, kc, e->key.state);
            break; }
        case LORIE_INPUT_TOUCH_DOWN:
            wl_list_for_each(r, &in->touches, link)
                wl_touch_send_down(r, ns(in), 0, e->touch.id, wl_fixed_from_double(e->touch.x), wl_fixed_from_double(e->touch.y));
            in->touch_dirty = 1; break;
        case LORIE_INPUT_TOUCH_UP:
            wl_list_for_each(r, &in->touches, link)
                wl_touch_send_up(r, ns(in), 0, e->touch.id);
            in->touch_dirty = 1; break;
        case LORIE_INPUT_TOUCH_MOTION:
            wl_list_for_each(r, &in->touches, link)
                wl_touch_send_motion(r, ns(in), e->touch.id, wl_fixed_from_double(e->touch.x), wl_fixed_from_double(e->touch.y));
            in->touch_dirty = 1; break;
        }
    }
    pframe(in); tframe(in);
    wl_event_source_timer_update(in->timer, 16);
    return 0;
}

extern void seat_get_pointer(struct wl_client*, struct wl_resource*, uint32_t);
extern void seat_get_keyboard(struct wl_client*, struct wl_resource*, uint32_t);
extern void seat_get_touch(struct wl_client*, struct wl_resource*, uint32_t);

static const struct wl_seat_interface seat_impl = { seat_get_pointer, seat_get_keyboard, seat_get_touch };

static void seat_bind(struct wl_client *c, void *d, uint32_t v, uint32_t id) {
    struct lorie_input *in = d;
    struct wl_resource *r = wl_resource_create(c, &wl_seat_interface, (int)v, id);
    if (!r) { wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(r, &seat_impl, in, NULL);
    wl_seat_send_capabilities(r, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_TOUCH);
    if (v >= WL_SEAT_NAME_SINCE_VERSION) wl_seat_send_name(r, "default");
}
