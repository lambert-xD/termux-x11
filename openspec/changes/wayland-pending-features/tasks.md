# Tasks: Wayland Compositor Pending Features

## Review Workload Forecast

| Field                   | Value                                      |
| ----------------------- | ------------------------------------------ |
| Estimated changed lines | ~1,400 (150 + 200 + 300 + 350 + 200 + 200) |
| 400-line budget risk    | Medium (PR 5 boundary → split to 5a+5b)    |
| Chained PRs recommended | Yes                                        |
| Suggested split         | PR 1 → PR 2 → PR 3 → PR 4 → PR 5a → PR 5b  |
| Delivery strategy       | auto-chain                                 |
| Chain strategy          | stacked-to-main                            |

```
Decision needed before apply: No
Chained PRs recommended: Yes
Chain strategy: stacked-to-main
400-line budget risk: Medium
```

## Dependency Graph

```
PR 1: NDK Build Verification (~150 lines)
   │
   ▼
PR 2: Renderer Damage Tracking (~200 lines)
   │
   ▼
PR 3: Texture Binding + Transforms/Scales + Viewporter (~300 lines)
   │
   ▼
PR 4: DMA-BUF Import (~350 lines)
   │
   ▼
PR 5a: Clipboard Wayland → Android (~200 lines)
   │
   ▼
PR 5b: Clipboard Android → Wayland + Loop Prevention (~200 lines)
```

Each PR depends on all prior PRs being merged. No parallel work units.

---

## PR 1: NDK Build Verification

**Estimated scope:** ~150 changed lines  
**Files touched:** `renderer.c`, `compositor.c`, `recipes/xserver.cmake`, `tests/test_ndk_build.c` (new)  
**Rollback:** Revert `CMakeLists.txt`/`xserver.cmake`; stubs remain in git history.

### Task 1.1 — RED: Write failing build-verification tests

**Files:** `app/src/main/cpp/lorie-wayland/tests/test_ndk_build.c` (new)

- [ ] Create `test_ndk_build.c` with `test_ndk_headers_present`: compile a source that includes `<EGL/egl.h>` and `<GLES2/gl2.h>` alongside `renderer.c` — must fail (RED) because stubs redefine types.
- [ ] Create `test_shm_pool_create`: call a test helper `lorie_shm_pool_create()` that wraps the current `shm_create_pool` stub — must return NULL (RED).
- [ ] Create `test_shm_pool_mmap`: create a temp fd, call pool creation helper — memory mapping fails because stub does not call `mmap` (RED).
- [ ] Wire new test file into `tests/test_main.c` and `tests/CMakeLists.txt`.
- [ ] Run `./gradlew test` and `cmake . && make && ctest` in `lorie-wayland/tests/` — confirm RED (tests fail / build fails).

### Task 1.2 — GREEN: Remove EGL/GLES stubs, use real NDK headers

**Files:** `app/src/main/cpp/lorie-wayland/renderer.c`

- [ ] Remove all `typedef` stubs for EGL/GLES types (`EGLDisplay`, `EGLSurface`, `EGLContext`, `EGLConfig`, `GLint`, `GLsizei`, etc.).
- [ ] Remove `extern` declarations for EGL/GLES functions that are provided by NDK headers.
- [ ] Add `#include <EGL/egl.h>` and `#include <GLES2/gl2.h>` (guarded by `#ifdef __ANDROID__` for non-Android test builds if needed; per spec, test build should use same headers).
- [ ] Verify `renderer.c` compiles without type redefinition errors.

### Task 1.3 — GREEN: Implement `shm_create_pool`

**Files:** `app/src/main/cpp/lorie-wayland/compositor.c`

- [ ] Implement `shm_create_pool` handler: `mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0)`.
- [ ] On `MAP_FAILED`, call `wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "mmap failed")`, `close(fd)`, return.
- [ ] On success, `close(fd)`, create `wl_shm_pool` resource from mapped data, bind to client.
- [ ] Add `munmap` cleanup in pool destructor.

### Task 1.4 — GREEN: Link `lorie-wayland/` into APK build

**Files:** `app/src/main/cpp/recipes/xserver.cmake`

- [ ] Add `lorie-wayland/*.c` source glob to the `Xlorie` (or equivalent) CMake target.
- [ ] Ensure `lorie-wayland/` headers are on the include path.
- [ ] Verify `./gradlew assembleDebug` produces APK for all ABIs (`x86`, `x86_64`, `armeabi-v7a`, `arm64-v8a`).

### Task 1.5 — TRIANGULATE: Verify GREEN and guard against regression

**Files:** `tests/test_ndk_build.c`, CI config

- [ ] Run `test_ndk_build.c` suite — confirm GREEN (all pass).
- [ ] Run `./gradlew assembleDebug` — confirm APK builds for all ABIs with no undefined references from `lorie-wayland/`.
- [ ] Verify existing X11 mode (`libXlorie.so`) still builds (no regression).
- [ ] Add CI step for `gradlew assembleDebug` if not already present.

---

## PR 2: Renderer Damage Tracking

**Estimated scope:** ~200 changed lines  
**Files touched:** `compositor.h`, `renderer.c`, `surface.c`, `tests/test_renderer_damage.c` (new)  
**Rollback:** Replace `glScissor` path with `glClear(GL_COLOR_BUFFER_BIT)` fallback.

### Task 2.1 — RED: Write failing damage-tracking tests

**Files:** `app/src/main/cpp/lorie-wayland/tests/test_renderer_damage.c` (new)

- [ ] `test_damage_accumulates`: call `lorie_renderer_damage_surface(r, s, 10, 20, 100, 50)` twice with overlapping rects — inspect `rs->accumulated_damage` region; must be empty/no-op (RED).
- [ ] `test_damage_empty_skips_draw`: commit with empty damage — verify `glDrawArrays` is not called for that surface (RED; currently full clear draws everything).
- [ ] `test_damage_scissor_applied`: commit with damage `(10, 20, 100, 50)` — verify `glScissor` bounding box matches (RED).
- [ ] `test_damage_cleared_after_commit`: commit with damage, then inspect region — must still contain rects because clear is not implemented (RED).
- [ ] `test_first_frame_full_clear`: first commit after renderer init — verify `glClear(GL_COLOR_BUFFER_BIT)` is called (GREEN expectation; this should already pass but must not break).
- [ ] Wire into `tests/test_main.c` and `tests/CMakeLists.txt`.

### Task 2.2 — GREEN: Add renderer pointer to compositor

**Files:** `app/src/main/cpp/lorie-wayland/compositor.h`, `app/src/main/cpp/lorie-wayland/wayland-activity.c`

- [ ] Add `struct lorie_renderer *renderer;` to `struct lorie_compositor` in `compositor.h`.
- [ ] Set `c->renderer = renderer` during compositor initialization in `wayland-activity.c`.

### Task 2.3 — GREEN: Implement `lorie_renderer_damage_surface`

**Files:** `app/src/main/cpp/lorie-wayland/renderer.c`

- [ ] Add `pixman_region32_t accumulated_damage;` to `struct renderer_surface` (init in add_surface, fini in remove_surface).
- [ ] Implement `lorie_renderer_damage_surface()`:
  - Lock `surfaces_lock`.
  - Find `renderer_surface` matching `lorie_surface`.
  - `pixman_region32_union_rect(&rs->accumulated_damage, &rs->accumulated_damage, x, y, w, h)`.
  - Unlock.
- [ ] If renderer is NULL, no-op (defensive).

### Task 2.4 — GREEN: Scissor-guided redraw in `lorie_renderer_commit`

**Files:** `app/src/main/cpp/lorie-wayland/renderer.c`

- [ ] Replace unconditional `glClear(GL_COLOR_BUFFER_BIT)` with per-surface logic:
  - For each surface in z-order:
    - If `!pixman_region32_not_empty(&rs->accumulated_damage)` AND buffer unchanged → skip `glDrawArrays` for this surface.
    - Else:
      - `bbox = pixman_region32_extents(&rs->accumulated_damage)`.
      - `glScissor(bbox.x1, bbox.y1, bbox.x2 - bbox.x1, bbox.y2 - bbox.y1)`.
      - `glEnable(GL_SCISSOR_TEST)`.
      - Draw surface quad.
      - `glDisable(GL_SCISSOR_TEST)`.
- [ ] First frame / no surfaces ever damaged: still call `glClear(GL_COLOR_BUFFER_BIT)`.

### Task 2.5 — GREEN: Damage propagation from `surface_commit`

**Files:** `app/src/main/cpp/lorie-wayland/surface.c`

- [ ] In `surface_commit()`, after buffer swap, call `lorie_renderer_damage_surface(s->compositor->renderer, s, 0, 0, s->width, s->height)`.
- [ ] Ensure `surface_damage()` (client-requested damage) also forwards to `lorie_renderer_damage_surface`.

### Task 2.6 — GREEN: Clear damage after successful commit

**Files:** `app/src/main/cpp/lorie-wayland/renderer.c`

- [ ] After drawing a surface in `lorie_renderer_commit()`, call `pixman_region32_clear(&rs->accumulated_damage)`.

### Task 2.7 — TRIANGULATE: Verify GREEN and profile

**Files:** `tests/test_renderer_damage.c`

- [ ] Run full `test_renderer_damage.c` suite — confirm GREEN.
- [ ] Confirm `test_renderer_commit_no_crash` (existing) still passes.
- [ ] Manual check: static UI should show no GPU load increase; lock hold time for `surfaces_lock` should be <1 ms.

---

## PR 3: Texture Binding + Transforms/Scales + Viewporter

**Estimated scope:** ~300 changed lines  
**Files touched:** `compositor.h`, `surface.c`, `renderer.h`, `renderer.c`, `protocols/viewporter.c` (new), `protocols/viewporter.h` (new), `compositor.c`, `tests/test_transform.c` (new), `tests/test_viewporter.c` (new)  
**Rollback:** Revert shader/matrix changes; keep static quad as fallback.

### Task 3.1 — RED: Write failing transform and viewporter tests

**Files:** `app/src/main/cpp/lorie-wayland/tests/test_transform.c` (new), `tests/test_viewporter.c` (new)

- [ ] `test_transform_matrix_computed`: set `buffer_transform=90`, `buffer_scale=2` on surface, commit — verify renderer computes non-identity matrix (RED; currently ignored).
- [ ] `test_transform_rotated_buffer_size`: set `buffer_transform=90` with 100×200 buffer — verify `logical_width/logical_height` are swapped (RED).
- [ ] `test_viewporter_set_source`: call `wp_viewport.set_source(0, 0, 50, 50)` — verify pending state stored (RED; protocol not implemented).
- [ ] `test_viewporter_bad_value_error`: call `set_source(-1, 0, 50, 50)` — verify `WP_VIEWPORT_ERROR_BAD_VALUE` posted (RED).
- [ ] `test_viewporter_commit_applies`: set source+destination, commit — verify logical size computed correctly (RED).
- [ ] Wire both test files into `tests/test_main.c` and `tests/CMakeLists.txt`.

### Task 3.2 — GREEN: Extend `lorie_surface` with viewport and logical size

**Files:** `app/src/main/cpp/lorie-wayland/compositor.h`

- [ ] Add `int32_t logical_width, logical_height;` to `struct lorie_surface`.
- [ ] Add viewport state (current + pending):
  ```c
  struct { double src_x, src_y, src_w, src_h; int has_src; int32_t dst_w, dst_h; int has_dst; } viewport;
  struct { double src_x, src_y, src_w, src_h; int has_src; int32_t dst_w, dst_h; int has_dst; } pending_viewport;
  ```
- [ ] Add `struct wl_global *viewporter_global;` to `struct lorie_compositor`.

### Task 3.3 — GREEN: Compute logical size on surface commit

**Files:** `app/src/main/cpp/lorie-wayland/surface.c`

- [ ] On `surface_commit()`:
  - Apply `pending_viewport` → `viewport`.
  - Compute `logical_width/height`:
    - `src_w = viewport.has_src ? viewport.src_w : s->width / s->buffer_scale;`
    - `src_h = viewport.has_src ? viewport.src_h : s->height / s->buffer_scale;`
    - `logical_w = viewport.has_dst ? viewport.dst_w : src_w;`
    - `logical_h = viewport.has_dst ? viewport.dst_h : src_h;`
    - Apply transform rotation to swap w/h if needed (90°, 270°).
  - Store in `s->logical_width`, `s->logical_height`.

### Task 3.4 — GREEN: Vertex shader with transform matrix uniform

**Files:** `app/src/main/cpp/lorie-wayland/renderer.c`, `renderer.h`

- [ ] Add `GLint u_transform;` to `struct lorie_renderer` in `renderer.h`.
- [ ] Replace static fullscreen quad vertex shader with:
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
- [ ] Keep static unit quad VBO (one VBO for all surfaces).

### Task 3.5 — GREEN: Compute and upload per-surface transform matrix

**Files:** `app/src/main/cpp/lorie-wayland/renderer.c`

- [ ] In `lorie_renderer_commit()`, for each surface:
  - Compute 4×4 matrix `M` = NDC_translate × NDC_scale(logical_w, logical_h) × rotate(buffer_transform) × scale(buffer_scale).
  - `glUniformMatrix4fv(r->u_transform, 1, GL_FALSE, M)`.
  - Draw unit quad (position is transformed by shader).

### Task 3.6 — GREEN: Generate and implement `wp_viewporter` protocol

**Files:** `app/src/main/cpp/lorie-wayland/protocols/viewporter.c` (new), `protocols/viewporter.h` (new)

- [ ] Generate `wp_viewporter` protocol bindings (add to `wayland-protocols.cmake` or build script).
- [ ] Implement `wp_viewporter` global:
  - `get_viewport(surface)` → create `lorie_viewport` resource, store in surface.
- [ ] Implement `wp_viewport`:
  - `set_source(x, y, w, h)`: validate (negative/zero → `WP_VIEWPORT_ERROR_BAD_VALUE`), store in `pending_viewport`.
  - `set_destination(w, h)`: validate (≤0 → `WP_VIEWPORT_ERROR_BAD_VALUE`), store in `pending_viewport`.
  - `destroy()`: clear pending, schedule removal on next commit.
- [ ] Handle `WP_VIEWPORT_ERROR_NO_SURFACE` if surface destroyed before viewport.

### Task 3.7 — GREEN: Wire viewporter global into compositor

**Files:** `app/src/main/cpp/lorie-wayland/compositor.c`

- [ ] Call `lorie_viewporter_create(display)` during compositor initialization.
- [ ] Store returned global in `c->viewporter_global`.

### Task 3.8 — TRIANGULATE: Verify GREEN and test viewporter errors

**Files:** `tests/test_transform.c`, `tests/test_viewporter.c`

- [ ] Run `test_transform.c` — confirm GREEN.
- [ ] Run `test_viewporter.c` — confirm GREEN, including bad-value error cases.
- [ ] Manual test: `weston-info` or custom client should see `wp_viewporter` global advertised.
- [ ] Verify rotated/scaled buffers render correctly in emulator or device.

---

## PR 4: DMA-BUF Import

**Estimated scope:** ~350 changed lines  
**Files touched:** `protocols/linux-dmabuf.c`, `protocols/linux-dmabuf.h` (new), `renderer.c`, `compositor.c`, `tests/test_dmabuf.c` (new)  
**Rollback:** Revert `linux-dmabuf.c` changes; fall back to stub that closes FDs.

### Task 4.1 — RED: Write failing DMA-BUF tests

**Files:** `app/src/main/cpp/lorie-wayland/tests/test_dmabuf.c` (new)

- [ ] `test_dmabuf_validate_abgr8888`: call params create with valid ABGR8888 — must fail because `params_create` is stub (RED).
- [ ] `test_dmabuf_reject_nv12`: pass `DRM_FORMAT_NV12` — must not post error because stub doesn't validate (RED).
- [ ] `test_dmabuf_reject_zero_size`: pass `width=0` — must not post error (RED).
- [ ] `test_dmabuf_import_success`: mock EGL display, valid params — no texture created (RED).
- [ ] `test_dmabuf_import_failure`: mock failing `eglCreateImageKHR` — no cleanup because stub (RED).
- [ ] `test_dmabuf_renderer_binds_texture`: attach dmabuf buffer to surface, commit — renderer ignores it (RED).
- [ ] `test_dmabuf_shm_fallback`: attach shm buffer — verify existing path still works (should be GREEN baseline).
- [ ] Wire into `tests/test_main.c` and `tests/CMakeLists.txt`.

### Task 4.2 — GREEN: Define `lorie_dmabuf_buffer` structure and helpers

**Files:** `app/src/main/cpp/lorie-wayland/protocols/linux-dmabuf.h` (new)

- [ ] Define `struct lorie_dmabuf_buffer` with fields: `buffer_resource`, `egl_image`, `texture_id`, `width`, `height`, `format`, `num_planes`, `planes[4]`, `imported`.
- [ ] Declare `lorie_dmabuf_buffer_create(...)` and `lorie_dmabuf_buffer_destroy(...)`.
- [ ] Declare supported formats list (`DRM_FORMAT_ABGR8888`, `DRM_FORMAT_XBGR8888`, `DRM_FORMAT_ARGB8888`).

### Task 4.3 — GREEN: Implement `params_create` with validation

**Files:** `app/src/main/cpp/lorie-wayland/protocols/linux-dmabuf.c`

- [ ] Validate `width > 0 && height > 0` — else post `ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSION`.
- [ ] Validate `format` is in supported list — else post `ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT`.
- [ ] Validate at least one plane has `fd >= 0` — else post `ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_PLANE`.
- [ ] Validate plane count matches format expectations.

### Task 4.4 — GREEN: Implement EGLImage import and texture creation

**Files:** `app/src/main/cpp/lorie-wayland/protocols/linux-dmabuf.c`

- [ ] Build EGL attribute list for `eglCreateImageKHR`:
  - `EGL_LINUX_DRM_FOURCC_EXT`, `EGL_WIDTH`, `EGL_HEIGHT`.
  - Per plane: `EGL_DMA_BUF_PLANE{i}_FD_EXT`, `_OFFSET_EXT`, `_PITCH_EXT`.
  - `EGL_NONE` terminator.
- [ ] Call `eglCreateImageKHR(egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attribs)`.
- [ ] Close all plane FDs immediately after call (regardless of success).
- [ ] On success:
  - `glGenTextures(1, &texture_id)`.
  - `glBindTexture(GL_TEXTURE_2D, texture_id)`.
  - `glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_image)`.
  - Allocate `lorie_dmabuf_buffer`, populate fields, set `imported = 1`.
  - `wl_resource_set_user_data(buffer_resource, buf)`.
  - `wl_resource_set_destructor(buffer_resource, dmabuf_buffer_destroy)`.
- [ ] On failure:
  - Post `ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER`.
  - Log format and dimensions for debugging.

### Task 4.5 — GREEN: Renderer dmabuf detection and texture binding

**Files:** `app/src/main/cpp/lorie-wayland/renderer.c`

- [ ] In `lorie_renderer_commit()`, for each surface:
  - If `s->buffer_resource && !s->buffer` (not shm/regular):
    - `struct lorie_dmabuf_buffer *dmabuf = wl_resource_get_user_data(s->buffer_resource)`.
    - If `dmabuf && dmabuf->imported`:
      - `glBindTexture(GL_TEXTURE_2D, dmabuf->texture_id)`.
  - Else fall through to existing `LorieBuffer_attachToGL` / `LorieBuffer_bindTexture` path.

### Task 4.6 — GREEN: Check EGL extension at renderer init

**Files:** `app/src/main/cpp/lorie-wayland/renderer.c`

- [ ] During `lorie_renderer_init()`, check EGL extension string for `EGL_EXT_image_dma_buf_import`.
- [ ] Store flag `has_dmabuf_import` in `struct lorie_renderer`.
- [ ] If missing, skip creating `linux-dmabuf` global (graceful degradation).

### Task 4.7 — GREEN: Implement `dmabuf_buffer_destroy` destructor

**Files:** `app/src/main/cpp/lorie-wayland/protocols/linux-dmabuf.c`

- [ ] `eglDestroyImageKHR(egl_display, buf->egl_image)` if valid.
- [ ] `glDeleteTextures(1, &buf->texture_id)` if valid.
- [ ] `free(buf)`.

### Task 4.8 — TRIANGULATE: Verify GREEN, shm fallback, no FD leaks

**Files:** `tests/test_dmabuf.c`

- [ ] Run `test_dmabuf.c` suite — confirm GREEN.
- [ ] Verify `/proc/self/fd` inspection in tests shows no leaked FDs after error paths.
- [ ] Confirm `test_dmabuf_shm_fallback` passes (no regression).
- [ ] Integration test: `simple-dmabuf-egl` or equivalent client displays buffers.

---

## PR 5a: Clipboard Wayland → Android

**Estimated scope:** ~200 changed lines  
**Files touched:** `compositor.h`, `protocols/wl-data-device-manager.c`, `wayland-activity.c`, `tests/test_clipboard.c` (new)  
**Rollback:** Revert `wl-data-device-manager.c` changes; clipboard returns to no-op.

### Task 5a.1 — RED: Write failing Wayland→Android clipboard tests

**Files:** `app/src/main/cpp/lorie-wayland/tests/test_clipboard.c` (new)

- [ ] `test_clipboard_wayland_to_android`: mock a Wayland data source, call `data_device_set_selection` — verify no JNI call happens because implementation is no-op (RED).
- [ ] `test_clipboard_read_from_source`: create a `wl_data_source`, request data — verify no fd write happens (RED).
- [ ] Wire into `tests/test_main.c` and `tests/CMakeLists.txt`.

### Task 5a.2 — GREEN: Add `lorie_clipboard` struct to compositor

**Files:** `app/src/main/cpp/lorie-wayland/compositor.h`

- [ ] Define `struct lorie_clipboard` with:
  - `struct lorie_compositor *compositor`.
  - `struct wl_data_source *current_source`, `current_offer`.
  - `pthread_t android_writer_thread`.
  - `int write_fd`, `read_fd`.
  - `char *android_text`, `size_t android_text_len`.
  - `uint64_t android_sequence`, `last_wayland_sequence`.
  - `pthread_mutex_t lock`.
- [ ] Add `struct lorie_clipboard *clipboard;` to `struct lorie_compositor`.

### Task 5a.3 — GREEN: Implement `data_device_set_selection` with pipe

**Files:** `app/src/main/cpp/lorie-wayland/protocols/wl-data-device-manager.c`

- [ ] Implement `data_device_set_selection`:
  - If `current_source` exists, send `cancelled` event.
  - `pipe(&read_fd, &write_fd)`.
  - Create `wl_data_offer`, advertise `text/plain;charset=utf-8`.
  - Send `selection` event to all data devices.
  - Start worker thread:
    - Offer `write_fd` to source via `wl_data_source_send_send`.
    - Read from `read_fd` until EOF.
    - Convert to `jbyteArray`.
    - Call JNI `ClipboardManager.setPrimaryClip()`.
    - Close fds.

### Task 5a.4 — GREEN: JNI bridge for Wayland→Android text forward

**Files:** `app/src/main/cpp/lorie-wayland/wayland-activity.c`

- [ ] Implement `Java_com_termux_x11_LorieWaylandView_setClipboardText(JNIEnv*, jclass, jbyteArray)`.
- [ ] Register in JNI method table during `nativeInit`.
- [ ] Worker thread calls this JNI method after reading pipe.

### Task 5a.5 — TRIANGULATE: Verify GREEN

**Files:** `tests/test_clipboard.c`

- [ ] Run `test_clipboard_wayland_to_android` — confirm GREEN.
- [ ] Run `test_clipboard_read_from_source` — confirm GREEN.
- [ ] Verify no crash when selection is set and cleared repeatedly.

---

## PR 5b: Clipboard Android → Wayland + Loop Prevention

**Estimated scope:** ~200 changed lines  
**Files touched:** `protocols/wl-data-device-manager.c`, `wayland-activity.c`, `tests/test_clipboard.c`  
**Rollback:** Revert `wayland-activity.c` and `wl-data-device-manager.c`; clipboard returns to no-op.

### Task 5b.1 — RED: Write failing Android→Wayland and loop-prevention tests

**Files:** `app/src/main/cpp/lorie-wayland/tests/test_clipboard.c`

- [ ] `test_clipboard_android_to_wayland`: simulate `sendClipboardEvent` with text — verify no `wl_data_source` is created because bytes are freed immediately (RED).
- [ ] `test_clipboard_loop_prevention`: simulate bidirectional update — verify ping-pong occurs because no loop prevention exists (RED).
- [ ] `test_clipboard_size_cap`: pass 2 MiB payload — verify accepted (RED; should reject).
- [ ] `test_clipboard_null_terminated`: inspect buffer — verify not guaranteed null-terminated (RED).

### Task 5b.2 — GREEN: Implement `sendClipboardEvent` → Wayland source

**Files:** `app/src/main/cpp/lorie-wayland/wayland-activity.c`

- [ ] In `sendClipboardEvent(byte[] text)`:
  - Reject if `len > 1 MiB`, log warning, return.
  - Copy text into `clipboard->android_text` via `calloc(len + 1, 1)` (guarantee null termination).
  - If `source == WAYLAND && sequence == last_wayland_sequence` → ignore (echo).
  - Else:
    - Create `wl_data_source`, offer `text/plain;charset=utf-8`.
    - Call `data_device_set_selection(source)`.
    - Tag with `source = ANDROID`, increment sequence.

### Task 5b.3 — GREEN: Wayland client reads Android clipboard

**Files:** `app/src/main/cpp/lorie-wayland/protocols/wl-data-device-manager.c`

- [ ] In `wl_data_source_send_send` handler for Android-originated sources:
  - Write `clipboard->android_text` into the provided fd.
  - Close fd.
  - Truncate if text exceeds pipe buffer (should not happen for typical clipboard text).

### Task 5b.4 — GREEN: Loop prevention with source tag + timestamp gate

**Files:** `app/src/main/cpp/lorie-wayland/wayland-activity.c`

- [ ] Add enum `CLIPBOARD_SOURCE_ANDROID`, `CLIPBOARD_SOURCE_WAYLAND`.
- [ ] Store `last_source` and `last_timestamp_ms` on `lorie_clipboard`.
- [ ] In `sendClipboardEvent`:
  - If `last_source == WAYLAND && (now - last_timestamp) < 500ms` → ignore.
  - Else process normally.
- [ ] In Wayland→Android worker thread:
  - Before calling JNI, set `last_source = WAYLAND`, `last_timestamp = now`.

### Task 5b.5 — GREEN: Ensure FDs closed in all paths

**Files:** `app/src/main/cpp/lorie-wayland/protocols/wl-data-device-manager.c`, `wayland-activity.c`

- [ ] Audit all `pipe()`, `wl_data_source_send_send()`, and read/write paths.
- [ ] Close both fds on success, error, and cancellation.
- [ ] Use `goto cleanup` pattern or `__attribute__((cleanup))` wrapper if project style allows.

### Task 5b.6 — TRIANGULATE: Verify GREEN, loop prevention, size cap

**Files:** `tests/test_clipboard.c`

- [ ] Run full `test_clipboard.c` suite — confirm GREEN.
- [ ] Verify `test_clipboard_loop_prevention` passes (echo suppressed in both directions).
- [ ] Verify `test_clipboard_size_cap` passes (2 MiB rejected).
- [ ] Verify `test_clipboard_null_terminated` passes.
- [ ] End-to-end manual test: copy on Android → paste in Wayland client; copy in Wayland client → paste on Android.

---

## Cross-Cutting Verification Checklist

After all PRs are applied:

- [ ] `./gradlew test` passes (Java + native).
- [ ] `cmake . && make && ctest` in `lorie-wayland/tests/` passes.
- [ ] `./gradlew assembleDebug` builds APK for all ABIs.
- [ ] Existing X11 smoke test passes (no regression).
- [ ] `weston-info` sees `wp_viewporter` and `zwp_linux_dmabuf_v1` globals.
- [ ] No Valgrind/ASan leaks reported in native test suite.
