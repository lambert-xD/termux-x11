#ifndef LORIE_INPUT_H
#define LORIE_INPUT_H

#include <wayland-util.h>
#include <wayland-server-core.h>
#include <pthread.h>
#include <stdbool.h>

struct lorie_surface;

#define LORIE_INPUT_QUEUE_SIZE 256

enum {
    LORIE_INPUT_POINTER_MOTION = 1,
    LORIE_INPUT_POINTER_BUTTON,
    LORIE_INPUT_KEYBOARD_KEY,
    LORIE_INPUT_TOUCH_DOWN,
    LORIE_INPUT_TOUCH_UP,
    LORIE_INPUT_TOUCH_MOTION,
};

struct lorie_input_event {
    uint8_t type;
    union {
        struct { float x, y; } motion;
        struct { uint32_t button, state; } button;
        struct { uint32_t key, state; } key;
        struct { uint32_t id; float x, y; } touch;
    };
};

struct lorie_input {
    struct wl_display *display;
    struct wl_event_loop *loop;
    struct wl_global *seat_global;
    struct wl_event_source *timer;
    struct lorie_surface *pointer_focus;
    struct lorie_surface *keyboard_focus;
    struct lorie_surface *touch_focus;
    struct wl_list pointers;
    struct wl_list keyboards;
    struct wl_list touches;
    pthread_mutex_t queue_lock;
    struct lorie_input_event queue[LORIE_INPUT_QUEUE_SIZE];
    int queue_head;
    int queue_tail;
    bool pointer_dirty;
    bool touch_dirty;
};

struct lorie_input *lorie_input_init(struct wl_display *display);
void lorie_input_destroy(struct lorie_input *input);
void lorie_input_pointer_motion(struct lorie_input *input, float x, float y);
void lorie_input_pointer_button(struct lorie_input *input, uint32_t button, uint32_t pressed);
void lorie_input_keyboard_key(struct lorie_input *input, uint32_t keycode, uint32_t pressed);
void lorie_input_touch_down(struct lorie_input *input, uint32_t id, float x, float y);
void lorie_input_touch_up(struct lorie_input *input, uint32_t id);
void lorie_input_touch_motion(struct lorie_input *input, uint32_t id, float x, float y);
int lorie_input_dispatch(void *data);

#endif
