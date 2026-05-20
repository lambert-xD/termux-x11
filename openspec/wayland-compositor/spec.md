# Specification: Native Wayland Compositor for Android

## Functional Requirements

### FR-1: Wayland Display Server

The compositor SHALL create a `wl_display` and listen on a Unix socket at `$XDG_RUNTIME_DIR/wayland-0` (or a configurable path).

### FR-2: Core Protocol Support

The compositor SHALL implement these Wayland core globals:

- `wl_compositor` — surface creation and buffer attachment
- `wl_subcompositor` — sub-surface hierarchy
- `wl_shm` — shared memory buffers
- `wl_output` — single Android screen output
- `wl_seat` — input device aggregation

### FR-3: Xdg-Shell Protocol

The compositor SHALL implement `xdg_wm_base` with:

- `xdg_surface` — role assignment
- `xdg_toplevel` — fullscreen window management
- `xdg_popup` — popup windows (basic support)

### FR-4: Rendering

The compositor SHALL composite surfaces to an Android `SurfaceView` using GLES2:

- Full-screen quad per surface with texture mapping
- Alpha blending for transparent surfaces
- Cursor overlay
- Damage tracking for minimal redraw

### FR-5: Input

The compositor SHALL forward Android input events to Wayland clients:

- Touch → `wl_touch` (down, up, motion, frame, cancel)
- Keyboard → `wl_keyboard` (key, modifiers)
- Pointer/Mouse → `wl_pointer` (motion, button, axis)
- Focus management (which surface receives events)

### FR-6: Linux DMA-BUF

The compositor SHALL support `zwp_linux_dmabuf_v1` for zero-copy buffer import via `AHardwareBuffer`.

### FR-7: XWayland Integration

The compositor SHALL support running XWayland as a client for X11 app compatibility.

### FR-8: Coexistence with X11

The existing X11 server SHALL remain functional. Wayland mode SHALL be selectable at app startup.

## Non-Functional Requirements

### NFR-1: Performance

- Compositor render loop SHALL target display refresh rate (via `AChoreographer`)
- Buffer copy SHALL be avoided where possible (DMA-BUF preferred)

### NFR-2: Memory

- No memory leaks on client connect/disconnect cycles
- `LorieBuffer` reference counting SHALL prevent use-after-free

### NFR-3: Thread Safety

- Wayland event loop runs in its own thread
- Renderer runs in a separate thread
- JNI callbacks from Android main thread are thread-safe

### NFR-4: Build

- Must compile with Android NDK r25+
- Must support API level 26+
- Must build for arm64-v8a, armeabi-v7a, x86_64, x86

## Acceptance Criteria

1. `weston-info` (or equivalent) lists all advertised globals
2. A simple `cairo` Wayland client renders a colored rectangle
3. Touch events move a cursor in a test client
4. Keyboard input types text in a test client
5. `glxgears` runs via XWayland
6. Existing X11 mode still works after the changes
