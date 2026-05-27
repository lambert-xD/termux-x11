#include "input.h"
#include "compositor.h"
#include "keymap.h"
#include <stdlib.h>
#include <android/log.h>

#define LOG_TAG "LorieInput"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static void seat_bind(struct wl_client*, void*, uint32_t, uint32_t);

struct lorie_input *lorie_input_init(struct lorie_compositor *c) {
    struct lorie_input *in = calloc(1, sizeof(*in));
    if (!in) return NULL;
    in->display = c->display;
    in->compositor = c;
    in->loop = wl_display_get_event_loop(c->display);
    wl_list_init(&in->pointers);
    wl_list_init(&in->keyboards);
    wl_list_init(&in->touches);
    pthread_mutex_init(&in->queue_lock, NULL);
    in->seat_global = wl_global_create(c->display, &wl_seat_interface, 7, in, seat_bind);
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
    struct lorie_pointer *p;
    wl_list_for_each(p, &in->pointers, link)
        if (wl_resource_get_version(p->r) >= WL_POINTER_FRAME_SINCE_VERSION)
            wl_pointer_send_frame(p->r);
    in->pointer_dirty = 0;
}

static void tframe(struct lorie_input *in) {
    if (!in->touch_dirty) return;
    struct lorie_touch *t;
    wl_list_for_each(t, &in->touches, link)
        if (wl_resource_get_version(t->r) >= WL_TOUCH_FRAME_SINCE_VERSION)
            wl_touch_send_frame(t->r);
    in->touch_dirty = 0;
}

static uint32_t ns(struct lorie_input *in) { return wl_display_next_serial(in->display); }

/* ------------------------------------------------------------------ */
/* Focus helpers                                                       */
/* ------------------------------------------------------------------ */

static struct lorie_surface *find_surface_at_point(struct lorie_compositor *c, float x, float y) {
    if (!c) return NULL;
    struct lorie_surface *s;
    wl_list_for_each(s, &c->surfaces, link) {
        if (s->resource && s->logical_width > 0 && s->logical_height > 0 &&
            x >= s->x && x < s->x + s->logical_width &&
            y >= s->y && y < s->y + s->logical_height) {
            return s;
        }
    }
    return NULL;
}

static struct wl_client *focus_client(struct lorie_surface *focus) {
    return (focus && focus->resource) ? wl_resource_get_client(focus->resource) : NULL;
}

static void send_pointer_leave(struct lorie_input *in, struct lorie_surface *old_focus) {
    struct wl_client *client = focus_client(old_focus);
    if (!client) return;
    uint32_t serial = ns(in);
    struct lorie_pointer *p;
    wl_list_for_each(p, &in->pointers, link) {
        if (wl_resource_get_client(p->r) == client)
            wl_pointer_send_leave(p->r, serial, old_focus->resource);
    }
}

static void send_pointer_enter(struct lorie_input *in, struct lorie_surface *new_focus, float x, float y) {
    struct wl_client *client = focus_client(new_focus);
    if (!client) return;
    uint32_t serial = ns(in);
    wl_fixed_t sx = wl_fixed_from_double(x - new_focus->x);
    wl_fixed_t sy = wl_fixed_from_double(y - new_focus->y);
    struct lorie_pointer *p;
    wl_list_for_each(p, &in->pointers, link) {
        if (wl_resource_get_client(p->r) == client)
            wl_pointer_send_enter(p->r, serial, new_focus->resource, sx, sy);
    }
}

static void send_keyboard_leave(struct lorie_input *in, struct lorie_surface *old_focus) {
    struct wl_client *client = focus_client(old_focus);
    if (!client) return;
    uint32_t serial = ns(in);
    struct lorie_keyboard *k;
    wl_list_for_each(k, &in->keyboards, link) {
        if (wl_resource_get_client(k->r) == client)
            wl_keyboard_send_leave(k->r, serial, old_focus->resource);
    }
}

static void send_keyboard_enter(struct lorie_input *in, struct lorie_surface *new_focus) {
    struct wl_client *client = focus_client(new_focus);
    if (!client) return;
    uint32_t serial = ns(in);
    struct wl_array keys;
    wl_array_init(&keys);
    struct lorie_keyboard *k;
    wl_list_for_each(k, &in->keyboards, link) {
        if (wl_resource_get_client(k->r) == client)
            wl_keyboard_send_enter(k->r, serial, new_focus->resource, &keys);
    }
    wl_array_release(&keys);
}

static void update_pointer_focus(struct lorie_input *in, float x, float y) {
    struct lorie_surface *new_focus = find_surface_at_point(in->compositor, x, y);
    if (in->pointer_focus == new_focus) return;
    if (in->pointer_focus) send_pointer_leave(in, in->pointer_focus);
    in->pointer_focus = new_focus;
    if (new_focus) send_pointer_enter(in, new_focus, x, y);

    /* Keyboard focus follows pointer focus for MVP */
    if (in->keyboard_focus != new_focus) {
        if (in->keyboard_focus) send_keyboard_leave(in, in->keyboard_focus);
        in->keyboard_focus = new_focus;
        if (new_focus) send_keyboard_enter(in, new_focus);
    }
}

static void update_touch_focus(struct lorie_input *in, float x, float y) {
    struct lorie_surface *new_focus = find_surface_at_point(in->compositor, x, y);
    if (new_focus) in->touch_focus = new_focus;
}

/* ------------------------------------------------------------------ */
/* Public: clear focus when a surface is destroyed                    */
/* ------------------------------------------------------------------ */

void lorie_input_clear_focus_for_surface(struct lorie_input *in, struct lorie_surface *s) {
    if (!in || !s) return;
    if (in->pointer_focus == s) {
        send_pointer_leave(in, s);
        in->pointer_focus = NULL;
    }
    if (in->keyboard_focus == s) {
        send_keyboard_leave(in, s);
        in->keyboard_focus = NULL;
    }
    if (in->touch_focus == s) in->touch_focus = NULL;
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

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
        switch (e->type) {
        case LORIE_INPUT_POINTER_MOTION:
            update_pointer_focus(in, e->motion.x, e->motion.y);
            if (in->pointer_focus) {
                struct wl_client *client = focus_client(in->pointer_focus);
                struct lorie_pointer *p;
                wl_list_for_each(p, &in->pointers, link) {
                    if (wl_resource_get_client(p->r) == client)
                        wl_pointer_send_motion(p->r, 0, wl_fixed_from_double(e->motion.x - in->pointer_focus->x), wl_fixed_from_double(e->motion.y - in->pointer_focus->y));
                }
            }
            in->pointer_dirty = 1; break;
        case LORIE_INPUT_POINTER_BUTTON:
            if (in->pointer_focus) {
                struct wl_client *client = focus_client(in->pointer_focus);
                struct lorie_pointer *p;
                wl_list_for_each(p, &in->pointers, link) {
                    if (wl_resource_get_client(p->r) == client)
                        wl_pointer_send_button(p->r, ns(in), 0, e->button.button, e->button.state);
                }
            }
            in->pointer_dirty = 1; break;
        case LORIE_INPUT_KEYBOARD_KEY: {
            if (e->key.key >= 304) break;
            uint32_t kc = android_to_linux_keycode[e->key.key];
            if (kc == 0) break;
            if (in->keyboard_focus) {
                struct wl_client *client = focus_client(in->keyboard_focus);
                struct lorie_keyboard *k;
                wl_list_for_each(k, &in->keyboards, link) {
                    if (wl_resource_get_client(k->r) == client)
                        wl_keyboard_send_key(k->r, ns(in), 0, kc, e->key.state);
                }
            }
            break; }
        case LORIE_INPUT_TOUCH_DOWN:
            update_touch_focus(in, e->touch.x, e->touch.y);
            if (in->touch_focus) {
                struct wl_client *client = focus_client(in->touch_focus);
                struct lorie_touch *t;
                wl_list_for_each(t, &in->touches, link) {
                    if (wl_resource_get_client(t->r) == client)
                        wl_touch_send_down(t->r, ns(in), 0, in->touch_focus->resource, e->touch.id, wl_fixed_from_double(e->touch.x), wl_fixed_from_double(e->touch.y));
                }
            }
            in->touch_dirty = 1; break;
        case LORIE_INPUT_TOUCH_UP:
            if (in->touch_focus) {
                struct wl_client *client = focus_client(in->touch_focus);
                struct lorie_touch *t;
                wl_list_for_each(t, &in->touches, link) {
                    if (wl_resource_get_client(t->r) == client)
                        wl_touch_send_up(t->r, ns(in), 0, e->touch.id);
                }
            }
            in->touch_dirty = 1; break;
        case LORIE_INPUT_TOUCH_MOTION:
            if (in->touch_focus) {
                struct wl_client *client = focus_client(in->touch_focus);
                struct lorie_touch *t;
                wl_list_for_each(t, &in->touches, link) {
                    if (wl_resource_get_client(t->r) == client)
                        wl_touch_send_motion(t->r, 0, e->touch.id, wl_fixed_from_double(e->touch.x), wl_fixed_from_double(e->touch.y));
                }
            }
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
