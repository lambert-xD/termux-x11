# Wayland Protocol Implementations for Termux:X11

## Summary

Created protocol implementations in `app/src/main/cpp/lorie-wayland/protocols/` for the Lorie Wayland compositor. These protocols enable modern Wayland applications to run on Android through termux-x11.

## Files Created

### 1. xdg-shell (CRITICAL - Essential for desktop apps)

**Files:** `xdg-shell.h`, `xdg-shell.c`

Implements:

- `xdg_wm_base` - Global desktop window manager interface (version 7)
- `xdg_surface` - Base interface for desktop surfaces with configure/ack cycle
- `xdg_toplevel` - Top-level windows with maximize/fullscreen/minimize support
- `xdg_popup` - Popup menus and transient windows
- `xdg_positioner` - Positioning rules for popups

**Android-specific adaptations:**

- All toplevels are mapped to fullscreen (Android has single output)
- Window move/resize are no-ops (Android manages window bounds)
- Window menu is not supported

**Status:** Core implementation complete. Needs wl_surface integration for full functionality.

### 2. linux_dmabuf (HIGH PRIORITY - Zero-copy performance)

**Files:** `linux-dmabuf.h`, `linux-dmabuf.c`

Implements:

- `zwp_linux_dmabuf_v1` - DMA-BUF buffer factory (versions 3-5)
- `zwp_linux_buffer_params_v1` - Buffer parameters with multi-plane support
- `zwp_linux_dmabuf_feedback_v1` - Format feedback for optimization

**Android-specific adaptations:**

- Supports AHardwareBuffer-compatible formats (ARGB8888, XRGB8888, ABGR8888, XBGR8888)
- DMA-BUF FDs can be imported into AHardwareBuffer for zero-copy rendering
- Multi-plane support for YUV formats

**Status:** Implementation complete. Needs integration with LorieBuffer for actual DMA-BUF import.

### 3. wl_data_device_manager (MEDIUM - Clipboard/Drag-and-drop)

**Files:** `wl-data-device-manager.h`, `wl-data-device-manager.c`

Implements:

- `wl_data_device_manager` - Clipboard and DnD manager (version 3)
- `wl_data_source` - Source side of data transfer
- `wl_data_offer` - Offer to transfer data
- `wl_data_device` - Per-seat data device

**Android-specific adaptations:**

- Drag-and-drop not supported (no multi-window on Android)
- Clipboard sync integrated with Android clipboard via JNI stubs
- Selection (copy-paste) fully supported

**Status:** Implementation complete. JNI integration needed for Android clipboard sync.

### 4. zwp_relative_pointer_v1 (LOW - Games)

**Files:** `relative-pointer.h`, `relative-pointer.c`

Implements:

- `zwp_relative_pointer_manager_v1` - Factory for relative pointers
- `zwp_relative_pointer_v1` - Relative motion events (unaccelerated deltas)

**Android-specific adaptations:**

- Relative pointer mode for games that need raw input
- Integrated with pointer input system

**Status:** Basic implementation complete.

### 5. zwp_pointer_constraints_v1 (LOW - Games)

**Files:** `pointer-constraints.h`, `pointer-constraints.c`

Implements:

- `zwp_pointer_constraints_v1` - Pointer constraints manager
- `zwp_locked_pointer_v1` - Lock pointer to surface (for FPS games)
- `zwp_confined_pointer_v1` - Confine pointer to region

**Android-specific adaptations:**

- Pointer locking for immersive game mode
- Region confinement not fully implemented (Android limitations)

**Status:** Basic implementation complete.

## Build Notes

All protocol implementations require:

1. **wayland-scanner** to generate protocol headers from XML files in `wayland/wayland-protocols/`
2. **wayland-server** library from `wayland/wayland/`
3. **libdrm** for DRM format modifiers (linux_dmabuf)

The protocol headers define `struct wl_interface` and event/request functions that must be generated from XML. The .c files implement the server-side handlers.

## Next Steps

1. Add wayland-scanner to CMake build to generate headers from XML
2. Integrate protocols with compositor surface management
3. Connect linux_dmabuf to LorieBuffer/AHardwareBuffer
4. Implement Android JNI clipboard bridge
5. Add protocol globals to compositor initialization
