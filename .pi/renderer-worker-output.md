# Wayland Renderer Adapter - Implementation Summary

## Files Created

- `app/src/main/cpp/lorie-wayland/renderer.h` — Public API header
- `app/src/main/cpp/lorie-wayland/renderer.c` — Implementation (~500 lines)

## Architecture

### Surface-Centric Design

Unlike the X11 renderer which tracks a single `rootWindowTextureID`, the Wayland renderer maintains an **intrusive doubly-linked list** of `lorie_wl_surface_entry` structs. Each entry represents one `wl_surface` and carries:

- A `LorieBuffer*` (the surface's pixel data)
- A `lorie_wl_transform` (x, y, scale_x, scale_y)
- A damage bounding box
- A list of pending frame callbacks
- A `buffer_attached` guard to prevent leaking GL texture IDs

### EGL/GLES2 Reuse

The EGL initialization, shader compilation, and error-checking code are **directly adapted** from `lorie/renderer.c`:

- Same vertex shader (`attribute vec4 position` + `attribute vec2 texCoords`)
- Same fragment shaders (RGBA and BGRA variants using `.bgra` GLSL extension)
- Same EGL config selection with `EGL_OPENGL_ES2_BIT`
- Same default 1×1 `AImageReader` surface for context validity when no Android window is attached
- Same `eglSwapInterval(dpy, 0)` for non-blocking present

Key difference: the Wayland config requests `EGL_ALPHA_SIZE, 8` because surfaces need alpha blending for the over-compositing model.

### Multi-Surface Compositing

`lorie_wl_renderer_commit()` executes the following pipeline:

1. Clear framebuffer to opaque black
2. Lock surface list
3. Iterate all surfaces in list order (painter's algorithm — later surfaces paint over earlier ones)
4. For each damaged surface with a buffer:
   - Call `LorieBuffer_attachToGL()` exactly once (guarded by `buffer_attached`)
   - Call `LorieBuffer_bindTexture()` to bind the surface's texture
   - Compute NDC quad coordinates from `(x, y, scale)` and output dimensions
   - Draw a TRIANGLE_STRIP quad with the appropriate shader (BGRA vs RGBA)
5. `eglSwapBuffers()`
6. Fire all pending frame callbacks
7. Reset damage flags
8. Unlock surface list

### Transform Math

Surface-local coordinates are converted to OpenGL normalized device coordinates (`-1..+1`) with Y-flip to match Wayland's top-left origin:

```
x0 = 2*x/output_w - 1
y0 = 1 - 2*(y + surf_h)/output_h   // bottom edge
x1 = 2*(x + surf_w)/output_w - 1
y1 = 1 - 2*y/output_h              // top edge
```

### Damage Tracking

Each surface accumulates damage in a **bounding box** (`damage_x/y/w/h`). `lorie_wl_renderer_damage_surface()` grows the box to include new rectangles. On commit, only surfaces with `damaged == true` are redrawn.

> Note: For simplicity v1 does **not** use GL_SCISSOR_TEST for partial redraws. Full-surface quads are drawn for each damaged surface. This is acceptable because the dominant cost on Android GLES2 is usually buffer upload, not fill-rate.

### Frame Callbacks

Each surface keeps a singly-linked list of `cb_node` structs. `lorie_wl_renderer_add_frame_callback()` pushes a new node. After `eglSwapBuffers()` in `commit()`, all callbacks are drained and invoked. This matches the Wayland protocol semantics where `wl_callback.done` is sent after the frame is presented.

### Thread Safety

The surface list is protected by a `pthread_mutex_t`. The renderer is designed to be called from the **Wayland compositor's single event-loop thread**, but the mutex allows safe cross-thread surface add/remove if needed in the future.

### Buffer Lifecycle

- `add_surface()` acquires a reference on the initial buffer
- `set_surface_buffer()` releases the old buffer, acquires the new one, resets `buffer_attached`
- `remove_surface()` releases the buffer and fires any pending callbacks
- `LorieBuffer` reference counting ensures zero-copy sharing between the compositor and clients

## Key Design Decisions

1. **No direct `buffer->id` access** — the renderer uses only public `LorieBuffer` APIs (`attachToGL`, `bindTexture`, `description`, `acquire`, `release`) to remain decoupled from internal buffer struct layout.

2. **`buffer_attached` guard** — `LorieBuffer_attachToGL()` unconditionally calls `glGenTextures()`. Without the guard, repeated calls would leak texture IDs. The guard is reset whenever the buffer changes.

3. **`GL_LINEAR` filtering by default** — unlike the X11 renderer which uses `GL_NEAREST` for pixel-perfect root-window rendering, Wayland surfaces are often scaled (e.g., HiDPI). Linear filtering produces smoother results.

4. **Over-blending enabled globally** — `glEnable(GL_BLEND)` with `GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA` is set once at init time. All surfaces are drawn with alpha blending. This is correct for the standard Wayland "over" compositing model.

## Integration Points

The compositor code (not yet written) will:

1. Call `lorie_wl_renderer_init()` at startup
2. Call `lorie_wl_renderer_set_window(anw)` when the Android SurfaceView is ready
3. Create entries via `add_surface(wl_surface_ptr, buffer)` for each new `wl_surface`
4. Update buffers on `wl_surface.commit`
5. Call `damage_surface()` when the compositor receives `wl_surface.damage`
6. Queue frame callbacks and call `commit()` in the display loop
