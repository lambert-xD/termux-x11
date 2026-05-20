#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wl_display;
struct wl_surface;

/**
 * Initialize Wayland input subsystem.
 * Creates wl_seat global with pointer, keyboard and touch capabilities.
 * Must be called from the Wayland display thread.
 */
int lorie_wl_input_init(struct wl_display* display);

/**
 * Shutdown Wayland input subsystem.
 * Destroys the seat global and releases resources.
 */
void lorie_wl_input_shutdown(void);

/* Pointer events - called from Android main thread */

/**
 * Notify pointer motion.
 * @param x surface-local x coordinate
 * @param y surface-local y coordinate
 */
void lorie_wl_input_pointer_motion(double x, double y);

/**
 * Notify pointer button press/release.
 * @param button Linux input event code (BTN_LEFT, BTN_RIGHT, etc.)
 * @param pressed true for pressed, false for released
 */
void lorie_wl_input_pointer_button(uint32_t button, bool pressed);

/**
 * Notify pointer axis (scroll).
 * @param axis 0 = vertical_scroll, 1 = horizontal_scroll
 * @param value scroll distance in surface-local coordinate space
 */
void lorie_wl_input_pointer_axis(uint32_t axis, double value);

/* Keyboard events - called from Android main thread */

/**
 * Notify key press/release.
 * @param keycode Linux input event code (KEY_*, from lorie.h mapping)
 * @param pressed true for pressed, false for released
 */
void lorie_wl_input_keyboard_key(uint32_t keycode, bool pressed);

/* Touch events - called from Android main thread */

/**
 * Notify touch down.
 * @param id touch point identifier
 * @param x surface-local x coordinate
 * @param y surface-local y coordinate
 */
void lorie_wl_input_touch_down(int id, double x, double y);

/**
 * Notify touch up.
 * @param id touch point identifier
 */
void lorie_wl_input_touch_up(int id);

/**
 * Notify touch motion.
 * @param id touch point identifier
 * @param x surface-local x coordinate
 * @param y surface-local y coordinate
 */
void lorie_wl_input_touch_motion(int id, double x, double y);

/* Focus management */

/**
 * Set keyboard focus surface.
 * Called when a surface should receive keyboard events.
 * @param surface the surface to focus, or NULL to unfocus
 */
void lorie_wl_input_set_focus(struct wl_surface* surface);

/**
 * Set pointer focus surface.
 * Called when the pointer enters a surface.
 * @param surface the surface to focus, or NULL to unfocus
 * @param x surface-local x coordinate
 * @param y surface-local y coordinate
 */
void lorie_wl_input_set_pointer_focus(struct wl_surface* surface, double x, double y);

/**
 * Get current keyboard focus surface.
 */
struct wl_surface* lorie_wl_input_get_focus(void);

/**
 * Get current pointer focus surface.
 */
struct wl_surface* lorie_wl_input_get_pointer_focus(void);

#ifdef __cplusplus
}
#endif
