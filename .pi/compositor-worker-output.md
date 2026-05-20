# Compositor Worker Output

## Summary

Created a minimal but functional Wayland compositor core in `app/src/main/cpp/lorie-wayland/`.
The compositor uses `libwayland-server` from the `wayland/wayland` submodule and is designed
to integrate with the existing LorieBuffer system and Android-specific rendering pipeline.

## Files Created

### 1. compositor.h (244 lines)

**Purpose:** Main compositor header with all state structures, protocol interface declarations, and public API.

**Key structures:**

- `struct lorie_compositor` - Core compositor state (display, event loop, globals, mutex/cond, ANativeWindow)
- `struct lorie_output` - Wayland output backed by Android ANativeWindow
- `struct lorie_surface` - wl_surface mapped to LorieBuffer with damage tracking and frame callbacks
- `struct lorie_seat` - wl_seat with pointer, keyboard, touch state
- `struct lorie_shell_surface` - Basic wl_shell surface wrapper
- `struct lorie_frame_callback` - Frame callback node for wl_surface::frame

**Public API (JNI-callable):**

- `lorie_wayland_init()` - Initialize compositor (creates display, globals, default output/seat)
- `lorie_wayland_set_window()` - Set Android ANativeWindow for rendering
- `lorie_wayland_start()` - Start event loop in background thread, create Wayland socket
- `lorie_wayland_stop()` - Stop compositor, clean up all resources
- `lorie_wayland_get_display_fd()` - Get display fd for client connections
- `lorie_wayland_send_pointer_event()` - Thread-safe pointer input
- `lorie_wayland_send_touch_event()` - Thread-safe touch input
- `lorie_wayland_send_keyboard_event()` - Thread-safe keyboard input
- `lorie_wayland_request_redraw()` - Trigger compositor redraw

**Thread-safety:** Uses same mutex pattern as `lorie_shared_server_state` (timedlock with recovery for bionic).

### 2. compositor.c (310 lines)

**Purpose:** Core compositor implementation - initialization, lifecycle, event loop, input dispatch.

**Key functions:**

- Mutex helpers (`lorie_compositor_lock/unlock`) - bionic-safe with timeout recovery
- `lorie_wayland_init()` - Creates wl_display, event loop, initializes mutex/cond, creates globals:
  - wl_compositor (v6)
  - wl_subcompositor (v1)
  - wl_shell (v1)
  - wl_output (v4) - default 1280x720 @ 60Hz
  - wl_seat (v7) - pointer + keyboard + touch
- `lorie_wayland_start()` - Creates auto socket, sets WAYLAND_DISPLAY, starts event thread
- `lorie_wayland_stop()` - Terminates display, joins thread, frees all surfaces/outputs/seats
- `lorie_wayland_set_window()` - Sets ANativeWindow, updates output geometry
- Input event dispatchers - lock compositor, find focus surface, send Wayland events

### 3. surface.c (409 lines)

**Purpose:** wl_surface, wl_subsurface, and wl_shell implementation.

**Protocol implementations:**

- `wl_surface_interface` - destroy, attach, damage, frame, set_opaque_region, set_input_region, commit, set_buffer_transform, set_buffer_scale, damage_buffer
- `wl_subsurface_interface` - destroy, set_position, place_above, place_below, set_sync, set_desync
- `wl_shell_surface_interface` - pong, move, resize, set_toplevel, set_transient, set_fullscreen, set_popup, set_maximized, set_title, set_class
- `wl_compositor_interface` - create_surface, create_region
- `wl_subcompositor_interface` - get_subsurface
- `wl_shell_interface` - get_shell_surface

**Surface lifecycle:**

- `surface_handle_resource_destroy()` - Cleans up buffers, damage regions, frame callbacks
- `surface_attach()` - Handles pending buffer attachment (maps to LorieBuffer)
- `surface_commit()` - Applies pending state, moves surface, merges damage, triggers redraw
- `surface_damage()` - Accumulates damage in pixman_region32_t
- `surface_frame()` - Queues frame callbacks for next compositor frame

**Helpers:**

- `lorie_surface_at()` - Hit testing for input focus (scans all mapped surfaces)
- `lorie_surface_send_frame_callbacks()` - Emits all pending frame callbacks

### 4. output.c (50 lines)

**Purpose:** Wayland output global and damage management.

**Key functions:**

- `output_bind()` - Sends output geometry, mode, scale, done events to new clients
- `lorie_output_damage_all()` - Marks all mapped surfaces as fully damaged, triggers redraw

**Default output:** 1280x720, 60Hz, scale=1, transform=normal, make="Lorie", model="Android"

### 5. seat.c (130 lines)

**Purpose:** wl_seat, wl_pointer, wl_keyboard, wl_touch implementation.

**Protocol implementations:**

- `wl_seat_interface` - get_pointer, get_keyboard, get_touch, release
- `wl_pointer_interface` - set_cursor, release
- `wl_keyboard_interface` - release
- `wl_touch_interface` - release

**Seat features:**

- Pointer: cursor tracking, enter/leave events, motion, button
- Keyboard: NO_KEYMAP format (for now), repeat info (40 cps, 400ms delay)
- Touch: Multi-touch support (up to 20 slots), down/up/motion/frame events

## Architecture Decisions

1. **No xdg-shell yet** - wl_shell only. xdg-shell is more complex and will be added by another worker.
2. **LorieBuffer integration** - Surfaces store `LorieBuffer*` for their content. The actual buffer import from wl_buffer (shm/dma-buf) is a TODO for the renderer worker.
3. **Damage tracking** - Uses pixman_region32_t for efficient damage regions.
4. **Frame callbacks** - Per-surface callback queue, emitted after compositor frame.
5. **Input focus** - Simple top-most hit testing. Z-ordering via subsurface list (TODO).
6. **Thread model** - Wayland event loop runs in dedicated thread. Android input comes from JNI on UI thread. Mutex + condvar synchronizes.

## Compilation Notes

The code requires:

- `wayland/wayland` submodule (provides `<wayland-server.h>`)
- `pixman` submodule (provides `<pixman.h>`)
- Android NDK headers (`<android/native_window.h>`, `<android/log.h>`)

CMake integration is pending (another worker's task). The files should be added to the build with:

```cmake
add_library(lorie-wayland STATIC
    lorie-wayland/compositor.c
    lorie-wayland/surface.c
    lorie-wayland/output.c
    lorie-wayland/seat.c
)
target_include_directories(lorie-wayland PRIVATE
    ${WAYLAND_SRC_DIR}/src
    ${PIXMAN_SRC_DIR}/pixman
)
```

## Next Steps for Integration

1. **Build system** - Create CMake recipe for wayland-server, link everything
2. **Renderer** - Port renderer.c to composite Wayland surfaces to ANativeWindow
3. **Buffer import** - Implement wl_shm and dma-buf buffer import to LorieBuffer
4. **xdg-shell** - Add xdg_wm_base support for modern Wayland clients
5. **JNI wiring** - Connect Java WaylandEntryPoint to lorie*wayland*\* API
6. **XWayland** - Start XWayland process, manage its surfaces as Wayland subsurfaces
