# Design: Wayland Compositor Pending Features

## Document Information

| Field    | Value                                                 |
| -------- | ----------------------------------------------------- |
| Change   | wayland-pending-features                              |
| Phase    | design                                                |
| Proposal | openspec/changes/wayland-pending-features/proposal.md |
| Date     | 2026-05-21                                            |

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Design Principles](#design-principles)
3. [Feature 1: NDK Compilation Verification](#feature-1-ndk-compilation-verification)
4. [Feature 2: Real Damage Tracking](#feature-2-real-damage-tracking)
5. [Feature 3: Texture Binding + Transforms/Scales + Viewporter](#feature-3-texture-binding--transformsscales--viewporter)
6. [Feature 4: DMA-BUF Import](#feature-4-dma-buf-import)
7. [Feature 5: Bidirectional Clipboard](#feature-5-bidirectional-clipboard)
8. [Threading and Synchronization](#threading-and-synchronization)
9. [Error Handling Strategy](#error-handling-strategy)
10. [Performance Budget](#performance-budget)
11. [File Layout](#file-layout)
12. [Interface Contracts](#interface-contracts)
13. [Data Flow Diagrams](#data-flow-diagrams)
14. [Risks and Mitigations](#risks-and-mitigations)

---

## Architecture Overview

The Lorie Wayland compositor is a GLES2-based compositor running on Android. It consists of:

- **Compositor core** (`compositor.c`, `surface.c`, `output.c`) — Wayland protocol dispatch, surface state management
- **Renderer** (`renderer.c`) — GLES2 compositing thread, EGL context management
- **Protocols** (`protocols/*.c`) — Wayland protocol implementations (xdg-shell, linux-dmabuf, data-device, viewporter)
- **JNI Bridge** (`wayland-activity.c`) — Java ↔ C bridge for surface lifecycle, input events, clipboard
- **Input** (`input.c`, `seat.c`, `keymap.c`) — Touch, pointer, keyboard event translation

```
┌─────────────────────────────────────────────────────────────┐
│                    Android App (Java/Kotlin)                 │
│  ┌─────────────┐  ┌─────────────────────────────────────┐   │
│  │ LorieWayland│  │ ClipboardManager (Android framework)│   │
│  │    View     │  └─────────────────────────────────────┘   │
│  └──────┬──────┘                                             │
│         │ JNI (wayland-activity.c)                           │
└─────────┼────────────────────────────────────────────────────┘
          ▼
┌─────────────────────────────────────────────────────────────┐
│                   Native Layer (C)                           │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────────────┐ │
│  │ compositor  │  │   renderer   │  │  protocols/         │ │
│  │   core      │◄─┤   (GLES2)    │◄─┤  xdg-shell.c        │ │
│  │             │  │              │  │  linux-dmabuf.c     │ │
│  │ surface.c   │  │ egl_context  │  │  wl-data-device-    │ │
│  │ output.c    │  │ gl_scissor   │  │    manager.c        │ │
│  │ compositor.c│  │ transform    │  │  viewporter.c       │ │
│  └──────┬──────┘  └──────────────┘  └─────────────────────┘ │
│         │                                                    │
│  ┌──────▼──────┐  ┌──────────────┐  ┌─────────────────────┐ │
│  │   input     │  │  wayland-    │  │  LorieBuffer        │ │
│  │  (seat.c)   │  │  activity.c  │  │  (buffer.c/h)       │ │
│  └─────────────┘  └──────────────┘  └─────────────────────┘ │
│                                                              │
│  libwayland-server  ──►  wl_display, wl_event_loop          │
│  libpixman          ──►  pixman_region32_t damage tracking    │
│  libEGL/libGLESv2   ──►  EGL context, DMA-BUF import         │
└─────────────────────────────────────────────────────────────┘
```

---

## Design Principles

1. **Minimal change to existing X11 code** — The `lorie/` directory (X11 renderer, buffer) is reused but not modified. Wayland compositor lives in `lorie-wayland/`.
2. **LorieBuffer reuse** — DMA-BUF buffers are imported into the existing `LorieBuffer` infrastructure via a new `LORIEBUFFER_DMABUF` type, enabling uniform texture binding in `renderer.c`.
3. **pixman_region32_t for damage** — Already used in `surface.c` for `wl_surface.damage`; extend to renderer scissor propagation.
4. **Thread-safe JNI** — All JNI callbacks hold no locks when calling into Java; clipboard uses a dedicated worker thread for fd I/O.
5. **Graceful degradation** — If DMA-BUF import fails, fall back to shm copy. If EGL extension is missing, disable dmabuf advertising.

---

## Feature 1: NDK Compilation Verification

### Problem

- `renderer.c` contains EGL/GLES type stubs that conflict with real NDK headers when linked into the APK.
- `shm_create_pool` in `compositor.c` is a no-op — shm buffers cannot be created.
- `lorie-wayland/` sources are not linked into `libXlorie.so` production build.

### Data Structures

No new structures. Changes to existing:

| File           | Change                                                                                                          |
| -------------- | --------------------------------------------------------------------------------------------------------------- |
| `renderer.c`   | Remove `#define` stubs and `extern` declarations for EGL/GLES functions; include `<EGL/egl.h>`, `<GLES2/gl2.h>` |
| `compositor.c` | Replace `shm_create_pool` stub with real `mmap`-based implementation                                            |

### Algorithms

**shm_create_pool implementation:**

```c
static void shm_create_pool(struct wl_client *client,
                            struct wl_resource *resource,
                            uint32_t id, int32_t fd, int32_t size) {
    void *data = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (data == MAP_FAILED) {
        wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "mmap failed");
        return;
    }
    struct wl_shm_pool *pool = wl_shm_pool_create(data, size);
    // ... bind pool resource
}
```

### File Changes

| File                                     | Lines | Action                                               |
| ---------------------------------------- | ----- | ---------------------------------------------------- |
| `renderer.c`                             | −40   | Remove EGL/GLES stubs                                |
| `renderer.c`                             | +3    | Add `#include <EGL/egl.h>`, `#include <GLES2/gl2.h>` |
| `compositor.c`                           | +25   | Implement `shm_create_pool`                          |
| `recipes/xserver.cmake`                  | +8    | Add `lorie-wayland/*.c` to `Xlorie` target sources   |
| `lorie-wayland/tests/test_ndk_compile.c` | +40   | New test: verify APK build succeeds                  |

### Interface Contracts

- **C → Build system**: `renderer.c` must compile with real NDK EGL/GLES headers when `__ANDROID__` is defined.
- **Test → Build**: `gradlew assembleDebug` must produce APK with no undefined references from `lorie-wayland/` sources.

### Error Handling

- `shm_create_pool`: `mmap` failure → post `WL_SHM_ERROR_INVALID_FD` to client, close fd.
- Build failure → caught at compile time; no runtime path.

### Performance Impact

Negligible. This is a build-system fix.

---

## Feature 2: Real Damage Tracking

### Problem

- `lorie_renderer_damage_surface()` is a no-op.
- `lorie_renderer_commit()` calls `glClear(GL_COLOR_BUFFER_BIT)` every frame, wasting GPU fill-rate on mobile.

### Data Structures

**Existing `lorie_surface` already has:**

```c
struct lorie_surface {
    // ...
    pixman_region32_t damage;
    // ...
};
```

**New field in `struct renderer_surface`:**

```c
struct renderer_surface {
    struct wl_list link;
    struct lorie_surface *surface;
    int z_index;
    pixman_region32_t accumulated_damage;  /* NEW: damage since last commit */
};
```

### Algorithms

**Damage accumulation (`lorie_renderer_damage_surface`):**

```c
void lorie_renderer_damage_surface(struct lorie_renderer *r, struct lorie_surface *s,
                                    int32_t x, int32_t y, int32_t w, int32_t h) {
    pthread_mutex_lock(&r->surfaces_lock);
    struct renderer_surface *rs;
    wl_list_for_each(rs, &r->surfaces, link) {
        if (rs->surface == s) {
            pixman_region32_union_rect(&rs->accumulated_damage,
                                        &rs->accumulated_damage, x, y, w, h);
            break;
        }
    }
    pthread_mutex_unlock(&r->surfaces_lock);
}
```

**Per-surface scissor draw (`lorie_renderer_commit`):**

```
For each surface in z-order:
    If accumulated_damage is empty AND buffer unchanged:
        Skip surface (no redraw)
    Else:
        Compute bounding box of accumulated_damage
        glScissor(bbox.x, bbox.y, bbox.w, bbox.h)
        glEnable(GL_SCISSOR_TEST)
        Draw surface quad
        glDisable(GL_SCISSOR_TEST)
        pixman_region32_clear(&accumulated_damage)
```

**Damage propagation from `surface_commit`:**

```c
// In surface.c surface_commit()
if (s->pending_attached) {
    // After buffer swap
    lorie_renderer_damage_surface(s->compositor->renderer, s,
                                   0, 0, s->width, s->height);
}
```

> **Design decision**: The compositor does not currently store a renderer pointer. We add `struct lorie_renderer *renderer` to `struct lorie_compositor` (set during initialization in `wayland-activity.c`).

### File Changes

| File                  | Lines | Action                                                                |
| --------------------- | ----- | --------------------------------------------------------------------- |
| `compositor.h`        | +1    | Add `struct lorie_renderer *renderer;` to `lorie_compositor`          |
| `renderer.c`          | +40   | Implement `lorie_renderer_damage_surface`                             |
| `renderer.c`          | +60   | Replace `glClear` with per-surface scissor in `lorie_renderer_commit` |
| `renderer.c`          | +15   | `renderer_surface` init/fini for `accumulated_damage`                 |
| `surface.c`           | +5    | Call `lorie_renderer_damage_surface` on commit with new buffer        |
| `tests/test_damage.c` | +50   | New tests: damage accumulation, empty skip, scissor bounds            |

### Interface Contracts

- **surface.c → renderer.c**: `lorie_renderer_damage_surface(r, s, x, y, w, h)` is called from `surface_commit` when a new buffer is attached, and from `surface_damage` when client requests damage.
- **renderer.c internal**: `accumulated_damage` is owned by the renderer; cleared after each successful commit. Protected by `surfaces_lock`.

### Error Handling

- `pixman_region32_union_rect` never fails (bounds are clamped).
- Empty damage region → safe skip; no GL operations issued for that surface.
- If renderer is NULL during damage call → no-op (defensive).

### Performance Considerations

| Metric                         | Before   | After                                         |
| ------------------------------ | -------- | --------------------------------------------- |
| Fullscreen clear per frame     | 1×       | 0× (only on first frame or full damage)       |
| GPU fill-rate (static content) | 100%     | ~0%                                           |
| Lock contention                | None new | `surfaces_lock` held briefly for region union |

**Mobile battery impact**: Eliminating `glClear` + full quad draw for undamaged regions reduces GPU power consumption significantly for static UIs.

---

## Feature 3: Texture Binding + Transforms/Scales + Viewporter

### Problem

- `buffer_transform` and `buffer_scale` are stored on `lorie_surface` but ignored by renderer.
- No `wp_viewporter` protocol support.
- Renderer uses a static fullscreen quad (`quad_vertices[]`) — no concept of surface position, size, or transform.

### Data Structures

**Extended `struct lorie_surface` (in `compositor.h`):**

```c
struct lorie_surface {
    // ... existing fields ...
    int32_t buffer_scale;
    int32_t buffer_transform;

    /* NEW: viewporter state (double-buffered, applied on commit) */
    struct {
        double src_x, src_y, src_w, src_h;  /* -1.0 = unset */
        int32_t dst_w, dst_h;               /* -1 = unset */
        int has_src : 1;
        int has_dst : 1;
    } viewport;

    /* NEW: computed logical size after transforms + viewporter */
    int32_t logical_width;
    int32_t logical_height;

    /* NEW: pending viewport state */
    struct {
        double src_x, src_y, src_w, src_h;
        int32_t dst_w, dst_h;
        int has_src : 1;
        int has_dst : 1;
    } pending_viewport;
};
```

**New `struct lorie_viewport` (for protocol object):**

```c
struct lorie_viewport {
    struct wl_resource *resource;
    struct lorie_surface *surface;
};
```

**New shader uniform locations (in `struct lorie_renderer`):**

```c
struct lorie_renderer {
    // ... existing fields ...
    GLint u_transform;   /* NEW: mat4 for scale/rotate/translate */
};
```

**New vertex shader:**

```glsl
attribute vec4 position;
attribute vec2 texCoords;
varying vec2 outTexCoords;
uniform mat4 transform;
void main(void) {
    outTexCoords = texCoords;
    gl_Position = transform * position;
}
```

### Algorithms

**Transform matrix computation:**

```
Input: surface->buffer_transform (enum wl_output_transform)
       surface->buffer_scale (int)
       surface->viewport (optional crop/scale)
       surface->x, surface->y (position in output coords)
       surface->width, surface->height (buffer pixel size)
       output_width, output_height

Step 1: Compute buffer → surface-local transform
    scale_mat = scale(buffer_scale, buffer_scale, 1)
    rotate_mat = rotation matrix for 90°×transform

Step 2: Apply viewporter source crop (if set)
    texcoords are adjusted to src_rect / buffer_size

Step 3: Apply viewporter destination size (if set)
    surface_logical_w = dst_w (or src_w if no dst)
    surface_logical_h = dst_h (or src_h if no dst)

Step 4: Compute surface-local → output-NDC transform
    // NDC: [-1,1] x [-1,1]
    translate(x, y, 0)
    scale(2*logical_w/output_w, 2*logical_h/output_h, 1)
    translate(-1 + 2*x/output_w, -1 + 2*y/output_h, 0)

Final matrix = NDC × local × rotate × scale
```

**Per-surface vertex buffer:**
Instead of one static fullscreen quad, generate a VBO per surface with 4 vertices positioned at the surface's logical rectangle. Alternatively, keep one unit quad and apply all transforms via the `u_transform` matrix uniform. **Decision**: Use the matrix uniform approach — one static VBO, one uniform change per surface.

**Viewporter protocol validation:**

```
set_source(x, y, w, h):
    If x,y,w,h all -1 → unset
    Else if x < 0 or y < 0 or w <= 0 or h <= 0 → bad_value error
    Else → store in pending_viewport

set_destination(w, h):
    If w == -1 and h == -1 → unset
    Else if w <= 0 or h <= 0 → bad_value error
    Else → store in pending_viewport

destroy():
    Clear pending_viewport (unset both src and dst)
    Mark for removal on next commit
```

### File Changes

| File                      | Lines | Action                                                         |
| ------------------------- | ----- | -------------------------------------------------------------- |
| `compositor.h`            | +20   | Add viewport fields, `logical_width/height` to `lorie_surface` |
| `surface.c`               | +40   | Apply `pending_viewport` on commit; compute logical size       |
| `renderer.c`              | +80   | Replace static quad with transform matrix; new vertex shader   |
| `renderer.c`              | +30   | Upload per-surface `u_transform` uniform in commit loop        |
| `protocols/viewporter.c`  | +120  | New file: `wp_viewporter` + `wp_viewport` implementation       |
| `compositor.c`            | +5    | Create `viewporter_global` in compositor lifecycle             |
| `compositor.h`            | +2    | Add `viewporter_global` field                                  |
| `tests/test_transform.c`  | +60   | Test transform matrices, viewporter errors                     |
| `tests/test_viewporter.c` | +50   | Test protocol operations                                       |

### Interface Contracts

- **viewporter.c ↔ surface.c**: `lorie_surface_set_viewport(surface, src, dst)` called from `wp_viewport.set_source/set_destination`. State is pending until `wl_surface.commit`.
- **surface.c ↔ renderer.c**: `lorie_surface.logical_width/height` read by renderer to compute NDC transform matrix.
- **renderer.c ↔ GL**: `u_transform` uniform is a `mat4` in column-major order (standard GL).

### Error Handling

- `wp_viewport.set_source` with negative/zero dimensions → `WP_VIEWPORT_ERROR_BAD_VALUE`.
- `wp_viewport.set_source` with non-integer width/height and no destination → `WP_VIEWPORT_ERROR_BAD_SIZE` (validated at commit time).
- Source rectangle outside buffer → `WP_VIEWPORT_ERROR_OUT_OF_BUFFER` (validated at commit time).
- Surface destroyed before viewport → `wp_viewport` requests (except destroy) raise `WP_VIEWPORT_ERROR_NO_SURFACE`.

### Performance Considerations

| Concern                   | Decision                                                        |
| ------------------------- | --------------------------------------------------------------- |
| Matrix upload per surface | One `glUniformMatrix4fv` per surface per frame — negligible     |
| Vertex shader complexity  | Adds one `mat4 × vec4` multiply — minimal on modern mobile GPUs |
| Viewporter validation     | Per-commit validation is O(1); no texture operations            |

---

## Feature 4: DMA-BUF Import

### Problem

- `linux-dmabuf.c` `params_create` closes FDs without importing them.
- No `EGL_EXT_image_dma_buf_import` path exists.
- GPU zero-copy buffer sharing is impossible.

### Data Structures

**New `struct lorie_dmabuf_buffer` (attached to `wl_buffer` user_data):**

```c
struct lorie_dmabuf_buffer {
    struct wl_resource *buffer_resource;
    EGLImageKHR egl_image;
    GLuint texture_id;
    int32_t width;
    int32_t height;
    uint32_t format;      /* DRM fourcc */
    uint32_t num_planes;
    struct {
        int fd;
        uint32_t offset;
        uint32_t stride;
    } planes[4];
    int imported;         /* 1 if eglCreateImageKHR succeeded */
};
```

**New buffer type in `LorieBuffer` (minor extension):**

```c
// In buffer.h: extend enum
enum {
    LORIEBUFFER_UNKNOWN,
    LORIEBUFFER_REGULAR,
    LORIEBUFFER_FD,
    LORIEBUFFER_AHARDWAREBUFFER,
    LORIEBUFFER_DMABUF,   /* NEW */
};
```

> **Design decision**: Instead of modifying `LorieBuffer` (shared with X11), we store `lorie_dmabuf_buffer` as `wl_buffer` user data and teach `renderer.c` to detect dmabuf-backed buffers and bind their `texture_id` directly. This avoids touching `lorie/buffer.c` and keeps X11 path isolated.

### Algorithms

**DMA-BUF import (`params_create`):**

```
Input: buffer_params with plane FDs, width, height, format

1. Validate:
   - width > 0, height > 0
   - format is in supported list (DRM_FORMAT_ABGR8888, DRM_FORMAT_XBGR8888, DRM_FORMAT_ARGB8888)
   - at least one plane has valid fd

2. Build EGL attribute list:
   EGL_LINUX_DRM_FOURCC_EXT = format
   EGL_WIDTH = width
   EGL_HEIGHT = height
   For each plane i:
       EGL_DMA_BUF_PLANE{i}_FD_EXT = fd
       EGL_DMA_BUF_PLANE{i}_OFFSET_EXT = offset
       EGL_DMA_BUF_PLANE{i}_PITCH_EXT = stride
   EGL_NONE

3. eglCreateImageKHR(egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attribs)

4. If success:
       glGenTextures(1, &texture_id)
       glBindTexture(GL_TEXTURE_2D, texture_id)
       glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_image)
       Store in lorie_dmabuf_buffer, attach to wl_buffer user data
   Else:
       Log error, close FDs, post WL_BUFFER error
```

**Renderer dmabuf detection (in `lorie_renderer_commit`):**

```c
for each surface:
    if (s->buffer) {
        // Existing LorieBuffer path (shm or AHardwareBuffer)
        LorieBuffer_attachToGL(lb);
        LorieBuffer_bindTexture(lb);
    } else if (s->buffer_resource) {
        struct lorie_dmabuf_buffer *dmabuf =
            wl_resource_get_user_data(s->buffer_resource);
        if (dmabuf && dmabuf->imported) {
            glBindTexture(GL_TEXTURE_2D, dmabuf->texture_id);
        }
    }
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
```

**Supported formats table:**
| DRM FourCC | GL Format | Planes | Notes |
|------------|-----------|--------|-------|
| `DRM_FORMAT_ABGR8888` | `GL_RGBA` | 1 | Most common |
| `DRM_FORMAT_XBGR8888` | `GL_RGB` | 1 | No alpha |
| `DRM_FORMAT_ARGB8888` | `GL_BGRA_EXT` | 1 | BGRA byte order |

### File Changes

| File                       | Lines | Action                                                        |
| -------------------------- | ----- | ------------------------------------------------------------- |
| `protocols/linux-dmabuf.c` | +120  | Implement `params_create` with EGL import                     |
| `protocols/linux-dmabuf.h` | +30   | New header: `lorie_dmabuf_buffer` struct, helper functions    |
| `renderer.c`               | +25   | Detect dmabuf buffers in commit loop, bind texture_id         |
| `renderer.c`               | +20   | Check `EGL_EXT_image_dma_buf_import` at init, store flag      |
| `compositor.c`             | +5    | Pass `renderer->egl_display` to `linux-dmabuf` init if needed |
| `tests/test_dmabuf.c`      | +60   | Test import success/failure, format validation, cleanup       |

### Interface Contracts

- **linux-dmabuf.c ↔ renderer.c**: `linux-dmabuf.c` creates `EGLImageKHR` and `GLuint texture_id` using the renderer's `egl_display`. It does NOT hold the EGL lock — EGLImage creation is thread-safe on Android if done on the GL thread. **Refinement**: DMA-BUF import must happen on the GL thread. Since `params_create` is called from the Wayland event loop thread, we defer import to the renderer commit or use `eglCreateImageKHR` with a shared EGLDisplay (which is thread-safe for creation on Android).
- **linux-dmabuf.c → wl_buffer**: `lorie_dmabuf_buffer` is stored as `wl_buffer` user data with a destroy callback that calls `eglDestroyImageKHR` and `glDeleteTextures`.

### Error Handling

| Error Condition             | Response                                                                              |
| --------------------------- | ------------------------------------------------------------------------------------- |
| Unsupported format          | Post `ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT`, close FDs                     |
| `eglCreateImageKHR` fails   | Post `ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER`, close FDs, log format/dims |
| Plane count mismatch format | Post `ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT`                                |
| FD is -1 for required plane | Post `ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_PLANE`                                 |
| Renderer not initialized    | Silently skip dmabuf global creation (degraded mode)                                  |

### Performance Considerations

| Metric               | shm path                          | dmabuf path               |
| -------------------- | --------------------------------- | ------------------------- |
| Buffer upload        | CPU memcpy + `glTexSubImage2D`    | Zero-copy GPU import      |
| Memory bandwidth     | 2× (client → shm → GPU)           | 1× (client → GPU via fd)  |
| GPU texture creation | ~0.1 ms                           | ~0.2 ms (import overhead) |
| Battery impact       | Higher (CPU copy wakes big cores) | Lower                     |

**Critical path**: `eglCreateImageKHR` must be called with the EGLDisplay bound to a context, or on some drivers it fails. On Android, creating an EGLImage from a dma-buf fd is typically safe from any thread with the EGLDisplay. We test this at init.

---

## Feature 5: Bidirectional Clipboard

### Problem

- `data_device_set_selection` creates an offer but does not transfer data.
- `wayland-activity.c` `sendClipboardEvent` receives bytes from Java but frees them immediately.
- Android → Wayland and Wayland → Android clipboard paths are both missing.
- Risk of infinite loop if both directions sync without loop prevention.

### Data Structures

**New `struct lorie_clipboard` (singleton, owned by compositor):**

```c
struct lorie_clipboard {
    struct lorie_compositor *compositor;

    /* Wayland → Android direction */
    struct wl_data_source *current_source;
    struct wl_data_offer *current_offer;
    pthread_t android_writer_thread;
    int write_fd;          /* fd offered to Wayland client */
    int read_fd;           /* fd we read from */

    /* Android → Wayland direction */
    char *android_text;    /* Cached text from Android ClipboardManager */
    size_t android_text_len;
    uint64_t android_sequence;  /* Monotonic counter for loop prevention */
    uint64_t last_wayland_sequence; /* Sequence of last Wayland-set clip */

    pthread_mutex_t lock;
};
```

**Java-side (existing `LorieWaylandView`):**

```java
// Native methods already exist:
native void sendClipboardEvent(byte[] text);
// New method for Java → C:
native void requestClipboardFromWayland();  // optional, for pull model
```

### Algorithms

**Wayland → Android (client sets selection):**

```
data_device_set_selection(source):
    1. If current_source exists, cancel it (send cancelled event)
    2. Create pipe pair: read_fd, write_fd
    3. Create data_offer with mime_type "text/plain;charset=utf-8"
    4. Send selection event to all data_devices
    5. Start android_writer_thread:
        - Offer write_fd to source via wl_data_source_send_send
        - Read from read_fd until EOF
        - Convert to jbyteArray
        - Call Java ClipboardManager.setPrimaryClip()
        - Tag with source="wayland", sequence++
        - Close fds
```

**Android → Wayland (user copies on Android):**

```
sendClipboardEvent(byte[] text):
    1. Copy text into clipboard->android_text
    2. Increment clipboard->android_sequence
    3. If sequence == last_wayland_sequence (echo), ignore
    4. Create wl_data_source, offer "text/plain;charset=utf-8"
    5. Set as selection via data_device_set_selection
    6. When Wayland client requests data:
        wl_data_source_send_send(source, mime_type, fd)
        Write android_text into fd, close fd
```

**Loop prevention:**

```
Android writes clip → sequence = N
Wayland receives → writes to Android → sequence = N (detected as echo) → ignored
```

> **Alternative simpler approach**: Store a `source_tag` enum (`CLIPBOARD_SOURCE_ANDROID`, `CLIPBOARD_SOURCE_WAYLAND`) on the clipboard. When the JNI bridge receives an Android clipboard update, check if the last write was from Wayland and within 500ms — if so, ignore. This is simpler than sequence numbers and sufficient for clipboard semantics.

### File Changes

| File                                 | Lines | Action                                                                             |
| ------------------------------------ | ----- | ---------------------------------------------------------------------------------- |
| `compositor.h`                       | +15   | Add `struct lorie_clipboard`, `clipboard` field to `lorie_compositor`              |
| `protocols/wl-data-device-manager.c` | +80   | Implement `data_device_set_selection`, `data_offer_receive`, pipe-based fd passing |
| `wayland-activity.c`                 | +60   | Forward clipboard bytes to Wayland data_source; read from pipe and call Java       |
| `wayland-activity.c`                 | +30   | Loop prevention: source tag + timestamp gate                                       |
| `tests/test_clipboard.c`             | +80   | Test Wayland→Android, Android→Wayland, loop prevention                             |

### Interface Contracts

**C → Java (JNI) for clipboard:**

```c
// Called from worker thread after reading Wayland clipboard data
JNIEXPORT void JNICALL
Java_com_termux_x11_LorieWaylandView_setClipboardText(JNIEnv *env, jclass clazz,
                                                        jbyteArray text);
```

**Java → C (JNI) for clipboard:**

```c
// Already exists: sendClipboardEvent(byte[] text)
// Extended to create wl_data_source and set selection
```

**Thread safety:**

- `wl_data_source` operations happen on Wayland event loop thread.
- Pipe read/write and Java JNI calls happen on dedicated worker threads.
- `lorie_clipboard.lock` protects `android_text` and sequence numbers.

### Error Handling

| Error Condition                 | Response                                |
| ------------------------------- | --------------------------------------- |
| pipe() fails                    | Log error, skip clipboard transfer      |
| JNI call fails (Java exception) | Clear exception, log, close fds         |
| Wayland client writes >1MB      | Truncate or abort read with warning     |
| FD leak in data_offer_receive   | Close fd in all paths (success + error) |

### Performance Considerations

- Clipboard transfers are infrequent; performance is not critical.
- Pipe buffers are 64KB typical; for text <1MB, transfer is instantaneous.
- Worker threads are short-lived (created per transfer, joined after). If overhead is measurable, switch to a thread pool — but for clipboard, this is premature optimization.

---

## Threading and Synchronization

### Thread Model

| Thread             | Owner                | Activities                                            |
| ------------------ | -------------------- | ----------------------------------------------------- |
| Android Main (UI)  | Android framework    | `surfaceChanged`, input events, `sendClipboardEvent`  |
| Wayland Event Loop | `compositor.c`       | Protocol dispatch, surface commits, buffer releases   |
| Renderer/GL        | `renderer.c`         | `lorie_renderer_commit()`, EGL swaps, damage tracking |
| Clipboard Worker   | spawned per-transfer | Pipe I/O, JNI calls to Java ClipboardManager          |

### Lock Hierarchy (must acquire in this order to prevent deadlock)

```
1. lorie_renderer.egl_lock      (shortest hold time)
2. lorie_renderer.surfaces_lock
3. lorie_compositor.lock
4. lorie_clipboard.lock
```

### Critical Sections

**Renderer commit (existing):**

```c
pthread_mutex_lock(&r->surfaces_lock);
// Build sorted surface list
pthread_mutex_unlock(&r->surfaces_lock);

// Frame callbacks (no lock needed)

pthread_mutex_lock(&r->egl_lock);
// GL draw operations
pthread_mutex_unlock(&r->egl_lock);
```

**Damage tracking (new):**

```c
// Wayland thread → surface_commit → lorie_renderer_damage_surface
pthread_mutex_lock(&r->surfaces_lock);
pixman_region32_union_rect(&rs->accumulated_damage, ...);
pixman_mutex_unlock(&r->surfaces_lock);
```

> The region union is O(1) amortized; lock hold time is microseconds.

**Clipboard (new):**

```c
// Wayland thread sets selection
pthread_mutex_lock(&clipboard->lock);
clipboard->current_source = source;
pthread_mutex_unlock(&clipboard->lock);

// Worker thread reads pipe, calls JNI
// ClipboardManager.setPrimaryClip is async on Android; no blocking
```

---

## Error Handling Strategy

### C Layer

| Severity                        | Action                                        | Example                                         |
| ------------------------------- | --------------------------------------------- | ----------------------------------------------- |
| Fatal (compositor cannot start) | Log and return error to caller                | EGL init failure, `wl_display_create` failure   |
| Protocol error                  | `wl_resource_post_error()` + destroy resource | Invalid viewport dimensions, bad dma-buf format |
| Client no-memory                | `wl_client_post_no_memory(client)`            | `calloc` failure during surface creation        |
| Runtime recoverable             | Log, degrade gracefully                       | DMA-BUF import fails → fall back to shm         |
| Debug                           | `LOGI/LOGE` with `__android_log_print`        | Buffer attach, surface commit                   |

### Java Layer

| Severity                      | Action                                      |
| ----------------------------- | ------------------------------------------- |
| JNI exception                 | `ExceptionClear`, log, return default value |
| Null surface                  | No-op (defensive)                           |
| Clipboard service unavailable | Silent ignore                               |

### Cross-Layer (JNI)

- All JNI entry points check `g_compositor != NULL` before dereferencing.
- `ANativeWindow` pointers are acquired/released symmetrically.
- Fds passed across JNI use `ParcelFileDescriptor` or raw int fds with explicit ownership transfer documentation.

---

## Performance Budget

### Frame Time Budget (60 FPS = 16.67 ms/frame)

| Operation                    | Budget       | Notes                                     |
| ---------------------------- | ------------ | ----------------------------------------- |
| `eglSwapBuffers` + GPU queue | 8 ms         | vsync-bound                               |
| GL draw (all surfaces)       | 4 ms         | Assumes ≤10 surfaces, simple quads        |
| Damage region compute        | 0.5 ms       | `pixman_region32` union + bbox            |
| Transform matrix upload      | 0.2 ms       | `glUniformMatrix4fv` per surface          |
| Texture bind (shm)           | 0.5 ms       | `glTexSubImage2D` for changed shm buffers |
| Texture bind (dmabuf)        | 0.1 ms       | `glBindTexture` only                      |
| **Total worst case**         | **~13.3 ms** | Leaves 3.3 ms headroom                    |

### Memory Budget

| Structure             | Per-Instance | Max Instances | Total                   |
| --------------------- | ------------ | ------------- | ----------------------- |
| `lorie_surface`       | 256 B        | 50            | 12.5 KB                 |
| `renderer_surface`    | 128 B        | 50            | 6.4 KB                  |
| `pixman_region32_t`   | 48 B + rects | 50            | ~10 KB                  |
| `lorie_dmabuf_buffer` | 128 B        | 20            | 2.6 KB                  |
| `lorie_clipboard`     | 256 B        | 1             | 256 B                   |
| **Total**             | —            | —             | **~32 KB** (negligible) |

### Battery Impact

| Feature          | Impact                 | Mitigation                      |
| ---------------- | ---------------------- | ------------------------------- |
| Damage tracking  | **Reduces** GPU work   | Only redraw changed regions     |
| DMA-BUF import   | **Reduces** CPU memcpy | Zero-copy path                  |
| Transform matrix | Minimal                | One uniform per surface         |
| Clipboard        | Negligible             | Infrequent, short-lived threads |

---

## File Layout

### New Files

```
app/src/main/cpp/lorie-wayland/
├── protocols/
│   ├── viewporter.c          (NEW) wp_viewporter + wp_viewport protocol
│   ├── viewporter.h          (NEW) struct lorie_viewport declaration
│   ├── linux-dmabuf.h        (NEW) struct lorie_dmabuf_buffer, helper APIs
│   └── wl-data-device-manager.h (NEW) struct lorie_clipboard, helper APIs
├── tests/
│   ├── test_ndk_compile.c    (NEW) build verification tests
│   ├── test_damage.c         (NEW) damage tracking tests
│   ├── test_transform.c      (NEW) transform + viewporter tests
│   ├── test_dmabuf.c         (NEW) dma-buf import tests
│   └── test_clipboard.c      (NEW) bidirectional clipboard tests
```

### Modified Files

```
app/src/main/cpp/lorie-wayland/
├── compositor.h              (+~40 lines: renderer ptr, clipboard, viewport fields)
├── compositor.c              (+~30 lines: shm_create_pool, viewporter global, clipboard init)
├── surface.c                 (+~50 lines: viewport commit, logical size compute, damage notify)
├── renderer.h                (+~5 lines: u_transform location)
├── renderer.c                (+~120 lines: remove stubs, damage, transforms, dmabuf bind)
├── protocols/
│   ├── linux-dmabuf.c        (+~100 lines: eglCreateImageKHR import, texture creation)
│   └── wl-data-device-manager.c (+~80 lines: selection transfer, pipe I/O)
├── wayland-activity.c        (+~90 lines: clipboard forwarding, loop prevention)
└── tests/
    ├── test_main.c           (+~25 lines: register new test suites)
    └── CMakeLists.txt        (+~5 lines: add new test sources)
```

### Build System Changes

```
app/src/main/cpp/
├── CMakeLists.txt            (no change — already includes lorie-wayland/tests)
└── recipes/xserver.cmake     (+~8 lines: add lorie-wayland/*.c to Xlorie target)
```

---

## Interface Contracts

### C Module Boundaries

**compositor.h (public API):**

```c
/* Lifecycle */
struct lorie_compositor *lorie_compositor_create(void);
void lorie_compositor_destroy(struct lorie_compositor *c);
int lorie_compositor_start(struct lorie_compositor *c);

/* Window */
void lorie_compositor_set_window(struct lorie_compositor *c, ANativeWindow *window);

/* Protocol globals */
struct wl_global *lorie_xdg_shell_create(struct wl_display *display);
struct wl_global *lorie_linux_dmabuf_create(struct wl_display *display,
                                             EGLDisplay egl_display);  /* NEW: egl param */
struct wl_global *lorie_data_device_manager_create(struct wl_display *display,
                                                    struct lorie_clipboard *cb); /* NEW: cb param */
struct wl_global *lorie_viewporter_create(struct wl_display *display);  /* NEW */
```

**renderer.h (public API):**

```c
struct lorie_renderer *lorie_renderer_create(void);
void lorie_renderer_destroy(struct lorie_renderer *r);
int lorie_renderer_init(struct lorie_renderer *r);
void lorie_renderer_fini(struct lorie_renderer *r);

void lorie_renderer_set_window(struct lorie_renderer *r, ANativeWindow *window);
void lorie_renderer_add_surface(struct lorie_renderer *r, struct lorie_surface *s);
void lorie_renderer_remove_surface(struct lorie_renderer *r, struct lorie_surface *s);
void lorie_renderer_damage_surface(struct lorie_renderer *r, struct lorie_surface *s,
                                    int32_t x, int32_t y, int32_t w, int32_t h);
int lorie_renderer_commit(struct lorie_renderer *r);

extern atomic_int lorie_renderer_filtering;
```

**linux-dmabuf.h (internal protocol API):**

```c
struct lorie_dmabuf_buffer {
    struct wl_resource *buffer_resource;
    EGLImageKHR egl_image;
    GLuint texture_id;
    int32_t width, height;
    uint32_t format;
    uint32_t num_planes;
    struct { int fd; uint32_t offset; uint32_t stride; } planes[4];
    int imported;
};

struct lorie_dmabuf_buffer *lorie_dmabuf_buffer_create(
    struct wl_client *client, uint32_t id,
    int32_t width, int32_t height, uint32_t format,
    const struct lorie_dmabuf_plane *planes, uint32_t n_planes,
    EGLDisplay egl_display);
void lorie_dmabuf_buffer_destroy(struct lorie_dmabuf_buffer *buf);
```

### C ↔ Java JNI Contracts

**Existing (stable):**

```java
// Java → C
static native void surfaceChanged(Surface surface);
static native void sendMouseEvent(float x, float y, int button, boolean down, boolean relative);
static native void sendTouchEvent(int action, int id, int x, int y);
static native boolean sendKeyEvent(int scanCode, int keyCode, boolean down);
static native void sendTextEvent(byte[] text);
static native void sendClipboardEvent(byte[] text);

// C → Java (new for clipboard)
static native void setClipboardText(byte[] text);
```

**JNI registration (dynamic, in `Java_com_termux_x11_LorieWaylandView_nativeInit`):**

```c
{"setClipboardText", "([B)V", (void*)&Java_com_termux_x11_LorieWaylandView_setClipboardText},
```

---

## Data Flow Diagrams

### Damage Tracking Flow

```
Wayland Client
    │ wl_surface.damage(x, y, w, h)
    ▼
surface.c: surface_damage()
    │ pixman_region32_union_rect(&s->damage, ...)
    ▼
surface.c: surface_commit()
    │ s->buffer_resource = pending_buffer
    │ lorie_renderer_damage_surface(r, s, 0, 0, w, h)  [full buffer on attach]
    ▼
renderer.c: lorie_renderer_damage_surface()
    │ pthread_mutex_lock(&surfaces_lock)
    │ pixman_region32_union_rect(&rs->accumulated_damage, ...)
    │ pthread_mutex_unlock(&surfaces_lock)
    ▼
[Wayland event loop continues]

[Renderer thread, vsync]
    ▼
renderer.c: lorie_renderer_commit()
    │ For each surface:
    │   If !pixman_region32_not_empty(&rs->accumulated_damage):
    │       continue  /* SKIP */
    │   bbox = pixman_region32_extents(&rs->accumulated_damage)
    │   glScissor(bbox.x, bbox.y, bbox.w, bbox.h)
    │   glEnable(GL_SCISSOR_TEST)
    │   draw_surface(surface)
    │   glDisable(GL_SCISSOR_TEST)
    │   pixman_region32_clear(&rs->accumulated_damage)
    ▼
    eglSwapBuffers()
```

### Transform + Viewporter Flow

```
Wayland Client
    │ wl_surface.set_buffer_transform(90)
    │ wl_surface.set_buffer_scale(2)
    │ wp_viewport.set_source(0, 0, 100, 100)
    │ wp_viewport.set_destination(200, 200)
    │ wl_surface.commit()
    ▼
surface.c: surface_commit()
    │ Apply pending_viewport to viewport
    │ Compute logical_size:
    │   src_w = viewport.has_src ? viewport.src_w : buffer_w / scale
    │   src_h = viewport.has_src ? viewport.src_h : buffer_h / scale
    │   logical_w = viewport.has_dst ? viewport.dst_w : src_w
    │   logical_h = viewport.has_dst ? viewport.dst_h : src_h
    │   Apply transform rotation to logical_w/h if needed
    │ s->logical_width = logical_w
    │ s->logical_height = logical_h
    ▼
renderer.c: lorie_renderer_commit()
    │ For each surface:
    │   Compute transform matrix:
    │     M = NDC_translate(x, y) × NDC_scale(logical_w, logical_h)
    │         × rotate(buffer_transform) × scale(buffer_scale, buffer_scale)
    │   glUniformMatrix4fv(r->u_transform, 1, GL_FALSE, M)
    │   glDrawArrays(GL_TRIANGLE_STRIP, 0, 4)
    ▼
    eglSwapBuffers()
```

### DMA-BUF Import Flow

```
Wayland Client (EGL app, Mesa)
    │ zwp_linux_dmabuf_v1.create_params()
    │ params.add(fd=17, plane=0, offset=0, stride=800)
    │ params.create(id=42, width=200, height=200, format=DRM_FORMAT_ABGR8888)
    ▼
protocols/linux-dmabuf.c: params_create()
    │ Validate: width>0, height>0, format supported, fd>=0
    │ Build EGL attrib list:
    │   EGL_LINUX_DRM_FOURCC_EXT = DRM_FORMAT_ABGR8888
    │   EGL_WIDTH = 200
    │   EGL_HEIGHT = 200
    │   EGL_DMA_BUF_PLANE0_FD_EXT = 17
    │   EGL_DMA_BUF_PLANE0_OFFSET_EXT = 0
    │   EGL_DMA_BUF_PLANE0_PITCH_EXT = 800
    │   EGL_NONE
    │
    │ egl_image = eglCreateImageKHR(egl_display, EGL_NO_CONTEXT,
    │                                EGL_LINUX_DMA_BUF_EXT, NULL, attribs)
    │ close(17)
    │
    │ glGenTextures(1, &texture_id)
    │ glBindTexture(GL_TEXTURE_2D, texture_id)
    │ glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_image)
    │
    │ buf = calloc(1, sizeof(struct lorie_dmabuf_buffer))
    │ buf->egl_image = egl_image
    │ buf->texture_id = texture_id
    │ buf->imported = 1
    │ wl_resource_set_user_data(buffer_resource, buf)
    │ wl_resource_set_destructor(buffer_resource, dmabuf_buffer_destroy)
    ▼
[Client later attaches buffer to surface]

renderer.c: lorie_renderer_commit()
    │ For each surface:
    │   if (s->buffer_resource && !s->buffer) {
    │       struct lorie_dmabuf_buffer *dmabuf =
    │           wl_resource_get_user_data(s->buffer_resource);
    │       if (dmabuf && dmabuf->imported) {
    │           glBindTexture(GL_TEXTURE_2D, dmabuf->texture_id);
    │       }
    │   }
    │   glDrawArrays(...)
```

### Bidirectional Clipboard Flow

```
┌─────────────────┐              ┌──────────────────────┐
│  Wayland Client │              │   Android App        │
│  (text editor)  │              │   (Chrome, etc.)     │
└────────┬────────┘              └──────────┬───────────┘
         │                                  │
         │ data_device.set_selection(source)│
         ▼                                  ▼
┌──────────────────────────────────────────────────────┐
│            wl-data-device-manager.c                   │
│  1. Create pipe(read_fd, write_fd)                   │
│  2. wl_data_source_send_send(source, mime, write_fd) │
│  3. Start worker thread reading read_fd              │
└────────┬─────────────────────────────────────────────┘
         │ worker thread reads UTF-8 text
         ▼
┌──────────────────────────────────────────────────────┐
│         wayland-activity.c (JNI bridge)               │
│  4. Convert to jbyteArray                            │
│  5. Call Java ClipboardManager.setPrimaryClip()      │
│  6. Tag source = WAYLAND, seq = N                    │
└────────┬─────────────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────────────────────┐
│         Android ClipboardManager                      │
│  7. User pastes into Android app                      │
│  8. Android app calls sendClipboardEvent(bytes)       │
└────────┬─────────────────────────────────────────────┘
         │ JNI: sendClipboardEvent(byte[] text)
         ▼
┌──────────────────────────────────────────────────────┐
│         wayland-activity.c                            │
│  9. Check: source == WAYLAND && seq == N?            │
│     YES → This is an echo. IGNORE.                   │
│     NO  → Create wl_data_source, offer text/plain    │
│  10. data_device.set_selection(source)               │
└────────┬─────────────────────────────────────────────┘
         │
         ▼
┌──────────────────────────────────────────────────────┐
│            wl-data-device-manager.c                   │
│  11. Wayland client requests data via fd             │
│  12. Write android_text into fd, close               │
└──────────────────────────────────────────────────────┘
```

---

## Architecture Decisions

| #   | Decision                                                                                       | Rationale                                                                                                                                                                   |
| --- | ---------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | **Reuse `LorieBuffer` for shm, separate `lorie_dmabuf_buffer` for DMA-BUF**                    | Avoids modifying shared `lorie/buffer.c` (X11 dependency). DMA-BUF path is simple enough to manage separately.                                                              |
| 2   | **Store `EGLImageKHR` in `lorie_dmabuf_buffer`, not `LorieBuffer`**                            | `LorieBuffer` uses `EGLImage` (non-KHR typedef) for AHardwareBuffer. DMA-BUF requires `EGLImageKHR` and `eglCreateImageKHR`. Keeping them separate prevents type confusion. |
| 3   | **Transform via vertex shader matrix uniform, not per-vertex CPU transform**                   | One `glUniformMatrix4fv` per surface is cheaper than rewriting a VBO every frame. Static VBO stays in GPU memory.                                                           |
| 4   | **Damage tracking uses `pixman_region32_t` on both surface and renderer**                      | pixman is already linked. Region operations are optimized. No new dependency.                                                                                               |
| 5   | **Clipboard loop prevention via source tag + sequence, not timestamp**                         | Sequence is deterministic and testable. Timestamp gates are fragile across threads.                                                                                         |
| 6   | **Viewporter is implemented as a new `protocols/viewporter.c`, not inline in `surface.c`**     | Follows existing pattern (xdg-shell, linux-dmabuf have their own files). Keeps surface.c focused on core wl_surface semantics.                                              |
| 7   | **NDK compilation fix gates stub removal behind `#ifdef __ANDROID__`**                         | Test runner on host may not have NDK headers. Stubs can be kept for host test builds if needed.                                                                             |
| 8   | **DMA-BUF import is checked at renderer init; if extension missing, global is not advertised** | Graceful degradation. Clients will fall back to shm or AHardwareBuffer.                                                                                                     |

---

## Risks and Mitigations

| Risk                                                                      | Likelihood | Impact                    | Mitigation                                                                                        |
| ------------------------------------------------------------------------- | ---------- | ------------------------- | ------------------------------------------------------------------------------------------------- |
| NDK EGL header conflicts (e.g., `EGLImage` vs `EGLImageKHR`)              | Medium     | High — blocks build       | Use `EGL_EGLEXT_PROTOTYPES` and include `<EGL/eglext.h>` explicitly; test all ABIs in CI          |
| Damage tracking causes mutex contention on high-damage clients            | Low        | Medium — frame drops      | Profile with `systrace`; if contention, switch to lock-free damage queue per surface              |
| DMA-BUF format unsupported on Mali/Adreno/PowerVR                         | Medium     | Medium — black textures   | Maintain supported format whitelist; test `simple-dmabuf-egl` on target devices; fall back to shm |
| DMA-BUF `eglCreateImageKHR` requires bound context on some drivers        | Medium     | Medium — import fails     | Create EGLImage on renderer thread via deferred import queue                                      |
| Clipboard fd passing crashes on Android 14+ scoped storage restrictions   | Low        | Medium — clipboard broken | Use `AFileDescriptor` wrapper; validate fds with `fcntl(fd, F_GETFD)` before use                  |
| Infinite clipboard loop (tag/sequence missed)                             | Low        | Low — CPU spike           | Add 500ms cooldown gate as secondary defense; unit test echo scenario                             |
| Viewporter `bad_size` validation (non-integer source without destination) | Low        | Low — protocol error      | Validate at commit time; Weston test suite covers this                                            |
| Renderer shader matrix causes precision issues on low-end GPUs            | Low        | Low — pixel misalignment  | Use `highp float` in vertex shader; test on Mali-G52 emulator                                     |

---

## Performance Budget Summary

| Feature                    | Lines (est.) | Frame-time impact        | Memory impact           |
| -------------------------- | ------------ | ------------------------ | ----------------------- |
| 1. NDK Compile             | ~150         | None                     | None                    |
| 2. Damage Tracking         | ~200         | **−3 ms** (saves clears) | +6 KB (region data)     |
| 3. Transforms + Viewporter | ~300         | +0.2 ms (matrix upload)  | +2 KB (viewport state)  |
| 4. DMA-BUF Import          | ~350         | **−1 ms** (no CPU copy)  | +3 KB (dmabuf metadata) |
| 5. Bidirectional Clipboard | ~400         | None (async)             | +1 KB (clipboard cache) |
| **Total**                  | **~1,400**   | **Net −3.8 ms/frame**    | **~12 KB**              |

---

## Test Plan

| Test Suite           | Feature | Coverage                                                    |
| -------------------- | ------- | ----------------------------------------------------------- |
| `test_ndk_compile.c` | PR 1    | APK builds, shm pool creation, no stub conflicts            |
| `test_damage.c`      | PR 2    | Damage accumulation, empty skip, scissor correctness        |
| `test_transform.c`   | PR 3    | Matrix computation, buffer_transform rotation, buffer_scale |
| `test_viewporter.c`  | PR 3    | Protocol errors, source/dst commit, double-buffering        |
| `test_dmabuf.c`      | PR 4    | Import success, unsupported format rejection, cleanup       |
| `test_clipboard.c`   | PR 5    | Wayland→Android, Android→Wayland, loop prevention echo      |

---

## Rollback Plan

| Feature                    | Rollback Action                                                    | Files to Revert                                            |
| -------------------------- | ------------------------------------------------------------------ | ---------------------------------------------------------- |
| 1. NDK Compile             | Revert `xserver.cmake`; restore stubs in `renderer.c`              | `renderer.c`, `recipes/xserver.cmake`                      |
| 2. Damage Tracking         | Replace scissor draw with `glClear(GL_COLOR_BUFFER_BIT)`           | `renderer.c`                                               |
| 3. Transforms + Viewporter | Revert shader to static quad; remove viewporter global             | `renderer.c`, `protocols/viewporter.c`, `compositor.c`     |
| 4. DMA-BUF Import          | Revert `params_create` to stub (close FDs)                         | `protocols/linux-dmabuf.c`                                 |
| 5. Clipboard               | Revert to no-op in `sendClipboardEvent`; remove selection transfer | `wayland-activity.c`, `protocols/wl-data-device-manager.c` |

---

_End of Design Document_
