#pragma once

#include <wayland-server.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lorie_compositor;
struct lorie_xwayland;
struct lorie_xwayland_wm;
struct lorie_xwayland_surface;

/**
 * XWayland initialization configuration
 */
struct lorie_xwayland_config {
    /** Path to the XWayland binary. If NULL, searches PATH. */
    const char* xwayland_path;
    
    /** Path to the Xorg binary (fallback). If NULL, searches PATH. */
    const char* xorg_path;
    
    /** Display number to use (e.g., 0 for :0). -1 for auto. */
    int display;
    
    /** Enable lazy start (only start when X11 client connects). */
    bool lazy;
    
    /** Termux prefix path for tmp directories. If NULL, uses /tmp. */
    const char* termux_prefix;
};

/**
 * Initialize XWayland support for the compositor.
 * 
 * Creates the X11 socket infrastructure but does not launch XWayland yet
 * (unless lazy=false).
 * 
 * @param display The Wayland display
 * @param config XWayland configuration
 * @return XWayland handle, or NULL on error
 */
struct lorie_xwayland* lorie_xwayland_init(struct wl_display* display,
                                            const struct lorie_xwayland_config* config);

/**
 * Launch the XWayland process.
 * 
 * For lazy mode, this is called automatically when an X11 client connects.
 * For non-lazy mode, call this after init to start XWayland immediately.
 * 
 * @param xwayland The XWayland handle
 * @return 0 on success, -1 on error
 */
int lorie_xwayland_launch(struct lorie_xwayland* xwayland);

/**
 * Shutdown XWayland.
 * 
 * Terminates the XWayland process and cleans up resources.
 * 
 * @param xwayland The XWayland handle
 */
void lorie_xwayland_shutdown(struct lorie_xwayland* xwayland);

/**
 * Destroy XWayland resources.
 * 
 * @param xwayland The XWayland handle
 */
void lorie_xwayland_destroy(struct lorie_xwayland* xwayland);

/**
 * Check if XWayland is running.
 * 
 * @param xwayland The XWayland handle
 * @return true if XWayland process is active
 */
bool lorie_xwayland_is_running(struct lorie_xwayland* xwayland);

/**
 * Get the DISPLAY string (e.g., ":0").
 * 
 * @param xwayland The XWayland handle
 * @return DISPLAY string, or NULL if not initialized
 */
const char* lorie_xwayland_get_display(struct lorie_xwayland* xwayland);

/**
 * Set the environment DISPLAY variable.
 * 
 * @param xwayland The XWayland handle
 * @return 0 on success, -1 on error
 */
int lorie_xwayland_set_env_display(struct lorie_xwayland* xwayland);

/**
 * Get the XWayland process PID.
 * 
 * @param xwayland The XWayland handle
 * @return PID, or -1 if not running
 */
pid_t lorie_xwayland_get_pid(struct lorie_xwayland* xwayland);

/* Internal: Window manager functions */
struct lorie_xwayland_wm* lorie_xwayland_wm_create(struct lorie_xwayland* xwayland, int wm_fd);
void lorie_xwayland_wm_destroy(struct lorie_xwayland_wm* wm);

#ifdef __cplusplus
}
#endif
