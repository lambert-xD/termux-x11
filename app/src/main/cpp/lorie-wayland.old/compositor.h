#pragma once

#include <wayland-server.h>
#include <wayland-server-protocol.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include "../lorie/buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lorie Wayland Compositor - Minimal but functional Wayland compositor for Android
 * Based on libwayland-server, integrated with LorieBuffer system
 */

/* Forward declarations */
struct lorie_compositor;
struct lorie_output;
struct lorie_surface;
struct lorie_seat;

/* ============================================================================
 * Core compositor state
 * ============================================================================ */

struct lorie_compositor {
    struct wl_display* display;
    struct wl_event_loop* event_loop;
    struct wl_list outputs;
    struct wl_list surfaces;
    struct wl_list seats;

    /* Global Wayland objects */
    struct wl_global* compositor_global;
    struct wl_global* subcompositor_global;
    struct wl_global* shell_global;      /* wl_shell (basic) */
    struct wl_global* seat_global;
    struct wl_global* output_global;

    /* Threading */
    pthread_t event_thread;
    volatile bool running;
    volatile bool stopped;

    /* Synchronization (same pattern as lorie_shared_server_state) */
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pid_t lockingPid;

    /* Rendering state */
    volatile uint8_t redraw_needed;
    volatile uint8_t wait_for_frame;

    /* JNI / Android surface */
    ANativeWindow* native_window;
    int32_t width;
    int32_t height;
    int32_t stride;
    int8_t format;  /* AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM or R8G8B8X8_UNORM */
};

/* ============================================================================
 * Output (Android ANativeWindow as Wayland output)
 * ============================================================================ */

struct lorie_output {
    struct wl_list link;
    struct lorie_compositor* compositor;
    struct wl_resource* resource;

    /* Geometry */
    int32_t x, y;
    int32_t width, height;
    int32_t physical_width, physical_height;
    int32_t subpixel;
    int32_t transform;
    int32_t scale;
    const char* make;
    const char* model;

    /* Mode */
    int32_t refresh; /* mHz */
    int32_t flags;
};

/* ============================================================================
 * Surface (wl_surface mapped to LorieBuffer)
 * ============================================================================ */

struct lorie_surface {
    struct wl_list link;
    struct lorie_compositor* compositor;
    struct wl_resource* resource;
    struct wl_resource* shell_surface;

    /* Buffer management */
    LorieBuffer* pending_buffer;
    LorieBuffer* current_buffer;
    struct wl_resource* pending_buffer_resource;
    struct wl_resource* current_buffer_resource;

    /* Geometry */
    int32_t x, y;
    int32_t width, height;
    int32_t dx, dy; /* pending surface offset */

    /* Damage tracking */
    pixman_region32_t pending_damage;
    pixman_region32_t current_damage;

    /* Frame callbacks */
    struct wl_list frame_callbacks;

    /* State flags */
    uint32_t pending_attached : 1;
    uint32_t pending_commit : 1;
    uint32_t mapped : 1;
    uint32_t input_region_set : 1;

    /* Input region (optional) */
    pixman_region32_t input_region;

    /* Subsurface support */
    struct wl_list subsurfaces;
    struct wl_list parent_link; /* if this is a subsurface */
    struct lorie_surface* parent;
    int32_t subsurface_z; /* ordering */
};

/* Frame callback node */
struct lorie_frame_callback {
    struct wl_list link;
    struct wl_resource* resource;
};

/* ============================================================================
 * Seat (pointer, keyboard, touch)
 * ============================================================================ */

struct lorie_seat {
    struct wl_list link;
    struct lorie_compositor* compositor;
    struct wl_resource* resource;

    /* Capabilities */
    uint32_t capabilities;

    /* Pointer state */
    struct wl_list pointer_resources;
    int32_t pointer_x;
    int32_t pointer_y;
    uint32_t pointer_buttons;
    struct lorie_surface* pointer_focus;
    struct wl_resource* pointer_focus_resource;

    /* Keyboard state */
    struct wl_list keyboard_resources;
    struct wl_array keys;
    uint32_t keyboard_mods_depressed;
    uint32_t keyboard_mods_latched;
    uint32_t keyboard_mods_locked;
    uint32_t keyboard_group;
    struct lorie_surface* keyboard_focus;
    struct wl_resource* keyboard_focus_resource;

    /* Touch state */
    struct wl_list touch_resources;
    struct lorie_surface* touch_focus;
    struct wl_resource* touch_focus_resource;
    int32_t touch_slots[20];
};

/* Shell surface (basic wl_shell implementation) */
struct lorie_shell_surface {
    struct wl_resource* resource;
    struct lorie_surface* surface;
    struct wl_list link;
    uint32_t ping_serial;
};

/* ============================================================================
 * External protocol interface tables (defined in respective .c files)
 * ============================================================================ */

extern const struct wl_compositor_interface compositor_impl;
extern const struct wl_subcompositor_interface subcompositor_impl;
extern const struct wl_shell_interface shell_impl;
extern const struct wl_shell_surface_interface shell_surface_interface;
extern const struct wl_seat_interface seat_interface;
extern const struct wl_pointer_interface pointer_interface;
extern const struct wl_keyboard_interface keyboard_interface;
extern const struct wl_touch_interface touch_interface;
extern const struct wl_surface_interface surface_interface;
extern const struct wl_subsurface_interface subsurface_interface;

/* ============================================================================
 * Bind callbacks (defined in respective .c files)
 * ============================================================================ */

void compositor_bind(struct wl_client* client, void* data,
                     uint32_t version, uint32_t id);
void subcompositor_bind(struct wl_client* client, void* data,
                        uint32_t version, uint32_t id);
void shell_bind(struct wl_client* client, void* data,
                uint32_t version, uint32_t id);
void seat_bind(struct wl_client* client, void* data,
               uint32_t version, uint32_t id);
void output_bind(struct wl_client* client, void* data,
                 uint32_t version, uint32_t id);

/* ============================================================================
 * Public API (callable from JNI)
 * ============================================================================ */

/**
 * Initialize the Wayland compositor.
 * Does NOT start the event loop yet.
 *
 * @return compositor instance or NULL on failure
 */
struct lorie_compositor* lorie_wayland_init(void);

/**
 * Set the Android native window for rendering.
 * Must be called before or after lorie_wayland_start().
 *
 * @param compositor compositor instance
 * @param window ANativeWindow from Java Surface
 */
void lorie_wayland_set_window(struct lorie_compositor* compositor, ANativeWindow* window);

/**
 * Start the compositor event loop in a background thread.
 *
 * @param compositor compositor instance
 * @return 0 on success, -1 on failure
 */
int lorie_wayland_start(struct lorie_compositor* compositor);

/**
 * Stop the compositor and clean up.
 * Blocks until the event thread exits.
 *
 * @param compositor compositor instance
 */
void lorie_wayland_stop(struct lorie_compositor* compositor);

/**
 * Get the Wayland display file descriptor.
 * Clients can connect to this.
 *
 * @param compositor compositor instance
 * @return fd or -1
 */
int lorie_wayland_get_display_fd(struct lorie_compositor* compositor);

/**
 * Add an input event (called from JNI/Android thread).
 * Thread-safe.
 *
 * @param compositor compositor instance
 * @param type event type (0=mouse, 1=touch, 2=key)
 * @param ... event-specific parameters
 */
void lorie_wayland_send_pointer_event(struct lorie_compositor* compositor,
                                       int32_t x, int32_t y,
                                       uint32_t button, bool down);
void lorie_wayland_send_touch_event(struct lorie_compositor* compositor,
                                     int32_t id, int32_t x, int32_t y,
                                     uint32_t type); /* 0=down,1=up,2=motion */
void lorie_wayland_send_keyboard_event(struct lorie_compositor* compositor,
                                        uint32_t key, bool down);

/**
 * Trigger a redraw (called when Android surface changes).
 * Thread-safe.
 */
void lorie_wayland_request_redraw(struct lorie_compositor* compositor);

/* ============================================================================
 * Internal helpers
 * ============================================================================ */

/* Lock/unlock compositor state (uses same pattern as lorie_mutex_lock) */
void lorie_compositor_lock(struct lorie_compositor* c);
void lorie_compositor_unlock(struct lorie_compositor* c);

/* Find surface at given coordinates */
struct lorie_surface* lorie_surface_at(struct lorie_compositor* c, int32_t x, int32_t y);

/* Damage entire output */
void lorie_output_damage_all(struct lorie_output* output);

/* Post frame callbacks for a surface */
void lorie_surface_send_frame_callbacks(struct lorie_surface* surface);

#ifdef __cplusplus
}
#endif
