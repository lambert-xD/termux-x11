#include "compositor.h"
#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#define LOG_TAG "LorieWayland"
#define log(prio, ...) __android_log_print(ANDROID_LOG_ ## prio, LOG_TAG, __VA_ARGS__)

/* ============================================================================
 * Mutex helpers (same pattern as lorie.h)
 * ============================================================================ */

void lorie_compositor_lock(struct lorie_compositor* c) {
    struct timespec ts = {0};
    while (true) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_nsec += 33UL * 1000000UL;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec  += ts.tv_nsec / 1000000000L;
            ts.tv_nsec  = ts.tv_nsec % 1000000000L;
        }
        int ret = pthread_mutex_timedlock(&c->lock, &ts);
        if (ret == ETIMEDOUT) {
            if (c->lockingPid == getpid() || c->running)
                continue;
            pthread_mutexattr_t attr;
            pthread_mutex_t initializer = PTHREAD_MUTEX_INITIALIZER;
            pthread_mutexattr_init(&attr);
            pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
            pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
            memcpy(&c->lock, &initializer, sizeof(initializer));
            pthread_mutex_init(&c->lock, &attr);
        } else {
            c->lockingPid = getpid();
            return;
        }
    }
}

void lorie_compositor_unlock(struct lorie_compositor* c) {
    c->lockingPid = 0;
    pthread_mutex_unlock(&c->lock);
}

/* ============================================================================
 * Event loop thread
 * ============================================================================ */

static void* event_loop_thread(void* data) {
    struct lorie_compositor* c = data;
    log(INFO, "Wayland event loop started");

    while (c->running) {
        wl_event_loop_dispatch(c->event_loop, -1);
    }

    log(INFO, "Wayland event loop stopped");
    return NULL;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

struct lorie_compositor* lorie_wayland_init(void) {
    struct lorie_compositor* c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;

    c->display = wl_display_create();
    if (!c->display) {
        log(ERROR, "Failed to create Wayland display");
        free(c);
        return NULL;
    }

    c->event_loop = wl_display_get_event_loop(c->display);
    if (!c->event_loop) {
        log(ERROR, "Failed to get event loop");
        wl_display_destroy(c->display);
        free(c);
        return NULL;
    }

    /* Initialize lists */
    wl_list_init(&c->outputs);
    wl_list_init(&c->surfaces);
    wl_list_init(&c->seats);

    /* Initialize mutex (same pattern as lorie.h) */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&c->lock, &attr);
    pthread_cond_init(&c->cond, NULL);

    /* Create globals */
    c->compositor_global = wl_global_create(c->display, &wl_compositor_interface, 6, c,
                                            compositor_bind);
    c->subcompositor_global = wl_global_create(c->display, &wl_subcompositor_interface, 1, c,
                                               subcompositor_bind);
    c->shell_global = wl_global_create(c->display, &wl_shell_interface, 1, c, shell_bind);

    /* Create default output */
    struct lorie_output* output = calloc(1, sizeof(*output));
    output->compositor = c;
    output->x = 0;
    output->y = 0;
    output->width = 1280;
    output->height = 720;
    output->physical_width = 340;
    output->physical_height = 190;
    output->subpixel = WL_OUTPUT_SUBPIXEL_UNKNOWN;
    output->transform = WL_OUTPUT_TRANSFORM_NORMAL;
    output->scale = 1;
    output->make = "Lorie";
    output->model = "Android";
    output->refresh = 60000; /* 60 Hz in mHz */
    output->flags = WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED;
    wl_list_insert(&c->outputs, &output->link);
    c->output_global = wl_global_create(c->display, &wl_output_interface, 4, output, output_bind);

    /* Create default seat */
    struct lorie_seat* seat = calloc(1, sizeof(*seat));
    seat->compositor = c;
    seat->capabilities = WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD |
                         WL_SEAT_CAPABILITY_TOUCH;
    wl_list_init(&seat->pointer_resources);
    wl_list_init(&seat->keyboard_resources);
    wl_list_init(&seat->touch_resources);
    wl_array_init(&seat->keys);
    wl_list_insert(&c->seats, &seat->link);
    c->seat_global = wl_global_create(c->display, &wl_seat_interface, 7, seat, seat_bind);

    log(INFO, "Wayland compositor initialized");
    return c;
}

void lorie_wayland_set_window(struct lorie_compositor* c, ANativeWindow* window) {
    if (!c)
        return;

    lorie_compositor_lock(c);
    if (c->native_window)
        ANativeWindow_release(c->native_window);
    c->native_window = window;
    if (window) {
        c->width = ANativeWindow_getWidth(window);
        c->height = ANativeWindow_getHeight(window);
        c->format = ANativeWindow_getFormat(window);
        ANativeWindow_acquire(window);
        log(INFO, "Window set: %dx%d fmt=%d", c->width, c->height, c->format);
    }
    lorie_output_damage_all(wl_container_of(c->outputs.next, (struct lorie_output*)NULL, link));
    lorie_compositor_unlock(c);
}

int lorie_wayland_start(struct lorie_compositor* c) {
    if (!c || c->running)
        return -1;

    /* Add socket */
    const char* socket_name = wl_display_add_socket_auto(c->display);
    if (!socket_name) {
        log(ERROR, "Failed to add Wayland socket");
        return -1;
    }
    log(INFO, "Wayland socket: %s", socket_name);

    /* Set WAYLAND_DISPLAY env var */
    setenv("WAYLAND_DISPLAY", socket_name, 1);

    c->running = 1;
    if (pthread_create(&c->event_thread, NULL, event_loop_thread, c) != 0) {
        log(ERROR, "Failed to create event thread");
        c->running = 0;
        return -1;
    }

    return 0;
}

void lorie_wayland_stop(struct lorie_compositor* c) {
    if (!c || !c->running)
        return;

    c->running = 0;
    wl_display_terminate(c->display);
    pthread_join(c->event_thread, NULL);

    /* Clean up surfaces */
    struct lorie_surface* surface, *stmp;
    wl_list_for_each_safe(surface, stmp, &c->surfaces, link) {
        wl_list_remove(&surface->link);
        if (surface->pending_buffer)
            LorieBuffer_release(surface->pending_buffer);
        if (surface->current_buffer)
            LorieBuffer_release(surface->current_buffer);
        pixman_region32_fini(&surface->pending_damage);
        pixman_region32_fini(&surface->current_damage);
        pixman_region32_fini(&surface->input_region);
        free(surface);
    }

    /* Clean up outputs */
    struct lorie_output* output, *otmp;
    wl_list_for_each_safe(output, otmp, &c->outputs, link) {
        wl_list_remove(&output->link);
        free(output);
    }

    /* Clean up seats */
    struct lorie_seat* seat, *setmp;
    wl_list_for_each_safe(seat, setmp, &c->seats, link) {
        wl_array_release(&seat->keys);
        wl_list_remove(&seat->link);
        free(seat);
    }

    if (c->native_window)
        ANativeWindow_release(c->native_window);

    wl_display_destroy(c->display);
    pthread_mutex_destroy(&c->lock);
    pthread_cond_destroy(&c->cond);
    free(c);

    log(INFO, "Wayland compositor stopped");
}

int lorie_wayland_get_display_fd(struct lorie_compositor* c) {
    if (!c)
        return -1;
    return wl_event_loop_get_fd(c->event_loop);
}

void lorie_wayland_request_redraw(struct lorie_compositor* c) {
    if (!c)
        return;
    lorie_compositor_lock(c);
    c->redraw_needed = 1;
    pthread_cond_broadcast(&c->cond);
    lorie_compositor_unlock(c);
}

/* ============================================================================
 * Input events (thread-safe)
 * ============================================================================ */

void lorie_wayland_send_pointer_event(struct lorie_compositor* c,
                                       int32_t x, int32_t y,
                                       uint32_t button, bool down) {
    if (!c)
        return;

    lorie_compositor_lock(c);
    struct lorie_seat* seat = wl_container_of(c->seats.next, (struct lorie_seat*)NULL, link);
    if (!seat) {
        lorie_compositor_unlock(c);
        return;
    }

    seat->pointer_x = x;
    seat->pointer_y = y;

    struct lorie_surface* focus = lorie_surface_at(c, x, y);
    struct wl_resource* focus_resource = focus ? focus->resource : NULL;

    /* Update focus */
    if (focus != seat->pointer_focus) {
        struct wl_resource* res;
        wl_list_for_each(res, &seat->pointer_resources, link) {
            if (seat->pointer_focus_resource) {
                wl_pointer_send_leave(res, 0, seat->pointer_focus_resource);
            }
            if (focus_resource) {
                wl_fixed_t sx = wl_fixed_from_int(x - focus->x);
                wl_fixed_t sy = wl_fixed_from_int(y - focus->y);
                wl_pointer_send_enter(res, 0, focus_resource, sx, sy);
            }
        }
        seat->pointer_focus = focus;
        seat->pointer_focus_resource = focus_resource;
    }

    /* Send motion */
    struct wl_resource* res;
    wl_list_for_each(res, &seat->pointer_resources, link) {
        wl_fixed_t sx = wl_fixed_from_int(x - (focus ? focus->x : 0));
        wl_fixed_t sy = wl_fixed_from_int(y - (focus ? focus->y : 0));
        wl_pointer_send_motion(res, 0, sx, sy);
    }

    /* Send button */
    if (button != 0) {
        uint32_t serial = wl_display_next_serial(c->display);
        wl_list_for_each(res, &seat->pointer_resources, link) {
            wl_pointer_send_button(res, serial, 0, button, down ? 1 : 0);
        }
    }

    lorie_compositor_unlock(c);
}

void lorie_wayland_send_touch_event(struct lorie_compositor* c,
                                     int32_t id, int32_t x, int32_t y,
                                     uint32_t type) {
    if (!c || id < 0 || id >= 20)
        return;

    lorie_compositor_lock(c);
    struct lorie_seat* seat = wl_container_of(c->seats.next, (struct lorie_seat*)NULL, link);
    if (!seat) {
        lorie_compositor_unlock(c);
        return;
    }

    struct lorie_surface* focus = lorie_surface_at(c, x, y);
    struct wl_resource* focus_resource = focus ? focus->resource : NULL;
    uint32_t serial = wl_display_next_serial(c->display);

    struct wl_resource* res;
    wl_list_for_each(res, &seat->touch_resources, link) {
        wl_fixed_t sx = wl_fixed_from_int(x - (focus ? focus->x : 0));
        wl_fixed_t sy = wl_fixed_from_int(y - (focus ? focus->y : 0));

        switch (type) {
            case 0: /* down */
                seat->touch_slots[id] = 1;
                wl_touch_send_down(res, serial, 0, focus_resource, id, sx, sy);
                break;
            case 1: /* up */
                seat->touch_slots[id] = 0;
                wl_touch_send_up(res, serial, 0, id);
                break;
            case 2: /* motion */
                wl_touch_send_motion(res, 0, id, sx, sy);
                break;
        }
    }

    if (type == 0 || type == 1) {
        wl_list_for_each(res, &seat->touch_resources, link) {
            wl_touch_send_frame(res);
        }
    }

    lorie_compositor_unlock(c);
}

void lorie_wayland_send_keyboard_event(struct lorie_compositor* c,
                                        uint32_t key, bool down) {
    if (!c)
        return;

    lorie_compositor_lock(c);
    struct lorie_seat* seat = wl_container_of(c->seats.next, (struct lorie_seat*)NULL, link);
    if (!seat) {
        lorie_compositor_unlock(c);
        return;
    }

    uint32_t serial = wl_display_next_serial(c->display);
    struct wl_resource* res;
    wl_list_for_each(res, &seat->keyboard_resources, link) {
        wl_keyboard_send_key(res, serial, 0, key, down ? 1 : 0);
    }

    lorie_compositor_unlock(c);
}
