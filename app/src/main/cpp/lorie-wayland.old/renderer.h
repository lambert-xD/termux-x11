#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/native_window.h>
#include <android/log.h>
#include <stdbool.h>
#include <pthread.h>
#include "../lorie/buffer.h"

#define log_wl(...) __android_log_print(ANDROID_LOG_DEBUG, "lorie-wl-renderer", __VA_ARGS__)
#define loge_wl(...) __android_log_print(ANDROID_LOG_ERROR, "lorie-wl-renderer", __VA_ARGS__)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Wayland renderer adapter for termux-x11.
 *
 * This renderer composites multiple wl_surfaces to a single Android
 * ANativeWindow using GLES2.  It reuses the LorieBuffer infrastructure
 * for zero-copy GPU buffer sharing.
 */

/* Opaque handle to a rendered surface */
typedef struct lorie_wl_surface_entry lorie_wl_surface_entry;

/* Frame callback - invoked after the frame has been presented */
typedef void (*lorie_wl_frame_callback)(void* user_data);

typedef struct {
    float x, y;           /* translation in surface-local coordinates */
    float scale_x, scale_y;
} lorie_wl_transform;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/**
 * Initialize EGL/GLES2 and the internal surface list.
 * Must be called once before any other renderer function.
 */
int lorie_wl_renderer_init(void);

/**
 * Tear down EGL and free all surface state.
 */
void lorie_wl_renderer_fini(void);

/* ------------------------------------------------------------------ */
/* Output window                                                       */
/* ------------------------------------------------------------------ */

/**
 * Set (or replace) the Android output window.
 * The renderer takes ownership of the ANativeWindow reference.
 */
void lorie_wl_renderer_set_window(ANativeWindow* window);

/* ------------------------------------------------------------------ */
/* Surface management                                                  */
/* ------------------------------------------------------------------ */

/**
 * Register a new wl_surface with the renderer.
 *
 * @param wl_surface   opaque pointer to the wayland surface object
 * @param buffer       initial LorieBuffer (may be NULL)
 * @return             surface entry handle, NULL on error
 */
lorie_wl_surface_entry* lorie_wl_renderer_add_surface(void* wl_surface,
                                                       LorieBuffer* buffer);

/**
 * Remove a surface and release all renderer-side resources.
 */
void lorie_wl_renderer_remove_surface(lorie_wl_surface_entry* entry);

/**
 * Update the buffer attached to a surface.
 * The old buffer is released; the new buffer is referenced.
 */
void lorie_wl_renderer_set_surface_buffer(lorie_wl_surface_entry* entry,
                                          LorieBuffer* buffer);

/**
 * Update the surface transform (translation + scale).
 */
void lorie_wl_renderer_set_surface_transform(lorie_wl_surface_entry* entry,
                                             const lorie_wl_transform* transform);

/* ------------------------------------------------------------------ */
/* Damage & frame callbacks                                            */
/* ------------------------------------------------------------------ */

/**
 * Mark a rectangular damage region on the surface.
 * The region is accumulated until the next commit().
 */
void lorie_wl_renderer_damage_surface(lorie_wl_surface_entry* entry,
                                      int x, int y, int w, int h);

/**
 * Request a frame callback for this surface.
 * The callback will be fired once the next frame is rendered.
 */
void lorie_wl_renderer_add_frame_callback(lorie_wl_surface_entry* entry,
                                          lorie_wl_frame_callback cb,
                                          void* user_data);

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

/**
 * Composite all damaged surfaces to the output window and present.
 *
 * This function:
 *   1. attaches new buffers to GL textures
 *   2. clears the framebuffer
 *   3. draws all surfaces in z-order (painter's algorithm, over-blending)
 *   4. swaps buffers
 *   5. fires all pending frame callbacks
 *   6. resets damage regions
 */
void lorie_wl_renderer_commit(void);

/* ------------------------------------------------------------------ */
/* Internal query helpers                                              */
/* ------------------------------------------------------------------ */

/** Return the wl_surface pointer stored in the entry */
void* lorie_wl_surface_get_wl_surface(lorie_wl_surface_entry* entry);

/** Return the currently attached buffer (does NOT increment refcount) */
LorieBuffer* lorie_wl_surface_get_buffer(lorie_wl_surface_entry* entry);

#ifdef __cplusplus
}
#endif
