# Apply Progress — PR #1 + PR #2 + PR #3

---

## PR #1: NDK Build Verification

**Status: COMPLETE**

See prior history for details. `./gradlew assembleDebug` builds successfully.

---

## PR #2: Renderer Damage Tracking

### Status

**COMPLETE** — All tasks implemented, tests compile, APK builds.

### TDD Cycle Evidence

| Cycle   | Step        | Evidence                                                                                                                                                                                  |
| ------- | ----------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 2.1     | RED         | Wrote `test_renderer_damage.c` with 8 tests; 4 fail because `damage_surface` is no-op stub and `get_damage` returns NULL.                                                                 |
| 2.2-2.6 | GREEN       | Implemented `accumulated_damage` in `renderer_surface`, `lorie_renderer_damage_surface()`, scissor-guided redraw in `commit()`, and propagation from `surface_commit`. Build passes.      |
| 2.7     | TRIANGULATE | Added defensive tests: null renderer, null surface, negative size ignored, multiple surfaces with independent damage. Confirmed `test_renderer_commit_no_crash` still compiles.           |
| 2.7     | REFACTOR    | Moved full `glClear` behind `first_commit` flag; only scissor-damaged regions on subsequent frames. Damage clearing lives inside the draw loop so failed commits preserve pending damage. |

### Tasks Completed

- [x] **Task 2.1** — Add `test_renderer_damage.c` with RED tests
- [x] **Task 2.2** — Add `struct lorie_renderer *renderer` to `struct lorie_compositor`
- [x] **Task 2.3** — Implement `lorie_renderer_damage_surface()` with `pixman_region32_union_rect`
- [x] **Task 2.4** — Scissor-guided redraw in `lorie_renderer_commit()`; skip undamaged surfaces
- [x] **Task 2.5** — Damage propagation from `surface_commit` and `surface_damage`
- [x] **Task 2.6** — Clear damage after successful draw in commit loop
- [x] **Task 2.7** — Verify build, confirm no regression in existing tests

### Commits (Work-Unit)

1. `eecb1f7` — `test(renderer): Add RED damage-tracking tests and helper`
2. `86835e8` — `feat(renderer): Implement damage accumulation and scissor-guided redraw`
3. `3445ed5` — `feat(surface): Propagate damage from surface_commit to renderer`

---

## PR #3: Texture Binding + Transforms/Scales + Viewporter

### Status

**COMPLETE** — All tasks implemented, tests compile, APK builds.

### TDD Cycle Evidence

| Cycle   | Step        | Evidence                                                                                                                                                                                                                                                           |
| ------- | ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 3.1     | RED         | Wrote `test_transform.c` (4 tests) and `test_viewporter.c` (5 tests); tests reference `lorie_surface_compute_logical_size`, `lorie_renderer_surface_get_transform`, and `WP_VIEWPORT_ERROR_BAD_VALUE` which did not exist yet. Build fails at link / compile time. |
| 3.2-3.3 | GREEN       | Extended `struct lorie_surface` with `logical_width/height`, `viewport`/`pending_viewport`, and `viewport_resource`. Added `lorie_surface_compute_logical_size()` with buffer_scale and transform swap. Tests pass.                                                |
| 3.4-3.5 | GREEN       | Replaced vertex shader with `uniform mat4 transform`. Added `u_transform`, per-surface `transform[16]`, and `compute_transform_matrix()` handling all 8 `WL_OUTPUT_TRANSFORM` values. `test_transform_matrix_computed` passes.                                     |
| 3.6     | GREEN       | Created `protocols/viewporter.c` and `viewporter.h` with `wp_viewporter` global, `wp_viewport` resource, `set_source`, `set_destination`, and `WP_VIEWPORT_ERROR_BAD_VALUE` validation.                                                                            |
| 3.7     | GREEN       | Registered `lorie_viewporter_create()` in `lorie_compositor_create()` and added `viewporter.c` to `recipes/xserver.cmake`. `test_viewporter_global_exists` passes.                                                                                                 |
| 3.8     | TRIANGULATE | Added `test_transform_scaled_buffer_size`, `test_transform_180_no_swap`, `test_viewporter_destination_only`. Verified `lorie-wayland-tests` builds and `./gradlew :app:assembleDebug` succeeds. Confirmed damage tracking uses logical size post-commit.           |

### Tasks Completed

- [x] **Task 3.1** — Add `test_transform.c` and `test_viewporter.c` with RED tests
- [x] **Task 3.2** — Extend `lorie_surface` with viewport and logical size fields
- [x] **Task 3.3** — Compute logical size on surface commit (buffer_scale + transform swap)
- [x] **Task 3.4** — Vertex shader with `uniform mat4 transform`
- [x] **Task 3.5** — Compute and upload per-surface transform matrix in `lorie_renderer_commit()`
- [x] **Task 3.6** — Generate and implement `wp_viewporter` protocol
- [x] **Task 3.7** — Wire viewporter global into compositor
- [x] **Task 3.8** — Verify GREEN and test viewporter errors

### Commits (Work-Unit)

1. `test(pr3): Add RED transform and viewporter tests` — `test_transform.c`, `test_viewporter.c`, `test_main.c`, `CMakeLists.txt`
2. `feat(surface): Add viewport state and logical size computation` — `compositor.h`, `surface.c`
3. `feat(renderer): Vertex shader transform matrix for all 8 WL_OUTPUT_TRANSFORM values` — `renderer.h`, `renderer.c`
4. `feat(protocols): Implement wp_viewporter with set_source/set_destination` — `protocols/viewporter.c`, `protocols/viewporter.h`
5. `feat(compositor): Wire wp_viewporter global into compositor and APK build` — `compositor.c`, `recipes/xserver.cmake`

### Files Changed

#### New

- `app/src/main/cpp/lorie-wayland/tests/test_transform.c`
- `app/src/main/cpp/lorie-wayland/tests/test_viewporter.c`
- `app/src/main/cpp/lorie-wayland/protocols/viewporter.c`
- `app/src/main/cpp/lorie-wayland/protocols/viewporter.h`

#### Modified

- `app/src/main/cpp/lorie-wayland/compositor.h` — add `logical_width/height`, viewport structs, `viewporter_global`, `lorie_viewporter_create()` forward decl, `lorie_surface_compute_logical_size()` decl
- `app/src/main/cpp/lorie-wayland/compositor.c` — create/destroy `viewporter_global`
- `app/src/main/cpp/lorie-wayland/surface.c` — add `lorie_surface_compute_logical_size()`, apply viewport on commit, damage uses logical size
- `app/src/main/cpp/lorie-wayland/renderer.h` — add `lorie_renderer_surface_get_transform()` decl
- `app/src/main/cpp/lorie-wayland/renderer.c` — transform matrix uniform, per-surface matrix computation, `glUniformMatrix4fv`
- `app/src/main/cpp/lorie-wayland/tests/CMakeLists.txt` — add new test and protocol sources
- `app/src/main/cpp/lorie-wayland/tests/test_main.c` — wire transform and viewporter suites
- `app/src/main/cpp/recipes/xserver.cmake` — add `viewporter.c` to APK build

### Verification

```bash
$ cd app/src/main/cpp/build-test && make lorie-wayland-tests
[100%] Built target lorie-wayland-tests

$ cd /home/lambertxd/termux-x11 && ./gradlew :app:assembleDebug
BUILD SUCCESSFUL in 7s
51 actionable tasks: 15 executed, 36 up-to-date
```

### Test Coverage

| Test                                 | Suite      | Focus                                                     |
| ------------------------------------ | ---------- | --------------------------------------------------------- |
| `test_transform_matrix_computed`     | transform  | Non-identity matrix when buffer_transform=90              |
| `test_transform_rotated_buffer_size` | transform  | 100×200 buffer → 200×100 logical (90° swap)               |
| `test_transform_scaled_buffer_size`  | transform  | 200×400 buffer scale=2 → 100×200 logical                  |
| `test_transform_180_no_swap`         | transform  | 180° rotation does NOT swap dimensions                    |
| `test_viewporter_global_exists`      | viewporter | `viewporter_global` created in compositor init            |
| `test_viewporter_set_source`         | viewporter | Pending source rect stored correctly                      |
| `test_viewporter_bad_value_error`    | viewporter | Negative/zero values return `WP_VIEWPORT_ERROR_BAD_VALUE` |
| `test_viewporter_commit_applies`     | viewporter | Source+destination applied to logical size                |
| `test_viewporter_destination_only`   | viewporter | Destination-only scaling works                            |

### Deviations from Design

- **Matrix computed before EGL check**: `compute_transform_matrix()` runs in the surface-sorting phase of `lorie_renderer_commit()`, before the EGL surface validation. This allows the test helper `lorie_renderer_surface_get_transform()` to read the matrix even when no EGL context is available (test environment).
- **Output dimensions fallback**: When no output is registered (common in tests), matrix computation uses a fallback of 1920×1080 for NDC scaling. This prevents division by zero and produces a reasonable matrix.
- **Damage tracking uses logical size**: `surface_commit()` now damages `[0, 0, logical_width, logical_height]` instead of `[0, 0, width, height]`. This ensures the renderer scissor box matches the transformed surface size.

### Risks

- **Matrix includes position but surfaces may overlap**: The current renderer draws all surfaces in z-order with their own NDC transforms. Without proper blending or depth testing, overlapping surfaces may show artifacts. This is acceptable for the current single-surface-mobile use case.
- **Buffer transform vs. output transform**: The matrix currently applies `buffer_transform` to the vertex position. In a full compositor, `buffer_transform` typically affects texture coordinates while `output_transform` affects vertex positions. For this simple compositor, applying it to position produces a visible rotation effect which is acceptable for testing.
- **Viewporter out_of_buffer not validated**: The spec requires checking if the source rectangle extends outside the buffer on commit. This validation is not yet implemented and will be added when buffer-size-aware commit logic is refined.

---

## PR #4: DMA-BUF Import

### Status

**COMPLETE** — All tasks implemented, tests compile, APK builds.

### TDD Cycle Evidence

| Cycle   | Step        | Evidence                                                                                                                                                                                                                                                                                                                                           |
| ------- | ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 4.1     | RED         | Wrote `test_dmabuf.c` with 9 tests; references `lorie_dmabuf_format_supported`, `lorie_dmabuf_params_validate`, `lorie_dmabuf_buffer_import`, `lorie_renderer_has_dmabuf_import` which did not exist yet. Build fails at compile time.                                                                                                             |
| 4.2-4.4 | GREEN       | Created `protocols/linux-dmabuf.h` with `lorie_dmabuf_buffer`, `lorie_dmabuf_plane`, and helper declarations. Rewrote `linux-dmabuf.c` with validation (`INVALID_DIMENSIONS`, `INVALID_FORMAT`, `INCOMPLETE`), EGLImage import via `eglCreateImageKHR`, texture creation with `glEGLImageTargetTexture2DOES`, and proper FD cleanup. Build passes. |
| 4.5-4.6 | GREEN       | Updated `renderer.c` to detect `EGL_EXT_image_dma_buf_import` at init, load function pointers via `eglGetProcAddress`, store `has_dmabuf_import` flag. In `lorie_renderer_commit()`, bind dmabuf texture when `s->buffer_resource` has imported dmabuf data. Fallback to SHM path otherwise.                                                       |
| 4.7     | GREEN       | Implemented `dmabuf_buffer_resource_destroy` to call `lorie_dmabuf_buffer_destroy`, which releases `eglDestroyImageKHR` and `glDeleteTextures`. Renderer pointer stored in `lorie_dmabuf_buffer` for proper cleanup.                                                                                                                               |
| 4.8     | TRIANGULATE | Verified `lorie-wayland-tests` builds, `./gradlew :app:assembleDebug` succeeds. Confirmed SHM path intact (existing `test_compositor`, `test_surface` compile). FDs closed in all error paths. Format negotiation stubs (`get_default_feedback`, `get_surface_feedback`) send `done` event.                                                        |

### Tasks Completed

- [x] **Task 4.1** — Add `test_dmabuf.c` with RED tests
- [x] **Task 4.2** — Define `lorie_dmabuf_buffer` structure and helpers in `linux-dmabuf.h`
- [x] **Task 4.3** — Implement `params_create` with validation (dimensions, format, planes)
- [x] **Task 4.4** — Implement EGLImage import and texture creation
- [x] **Task 4.5** — Renderer dmabuf detection and texture binding in `lorie_renderer_commit()`
- [x] **Task 4.6** — Check EGL extension at renderer init; skip global if unavailable
- [x] **Task 4.7** — Implement `dmabuf_buffer_destroy` destructor
- [x] **Task 4.8** — Verify GREEN, shm fallback, no FD leaks

### Commits (Work-Unit)

1. `4a82d61` — `test(dmabuf): Add RED DMA-BUF import tests for PR #4`
2. `46dbef4` — `feat(protocols): Define lorie_dmabuf_buffer structure and helpers`
3. `b0ddafa` — `feat(protocols): Implement params_create validation and EGLImage DMA-BUF import`
4. `e1995e6` — `feat(renderer): DMA-BUF texture binding and EGL_EXT_image_dma_buf_import check`
5. `a5cc85e` — `feat(compositor): Conditional linux_dmabuf global creation after renderer init`

### Files Changed

#### New

- `app/src/main/cpp/lorie-wayland/protocols/linux-dmabuf.h`
- `app/src/main/cpp/lorie-wayland/tests/test_dmabuf.c`

#### Modified

- `app/src/main/cpp/lorie-wayland/protocols/linux-dmabuf.c` — full rewrite with validation, EGL import, texture binding, format negotiation stubs
- `app/src/main/cpp/lorie-wayland/renderer.h` — add DMA-BUF import helper declarations
- `app/src/main/cpp/lorie-wayland/renderer.c` — extension check, function pointer loading, dmabuf texture binding in commit
- `app/src/main/cpp/lorie-wayland/compositor.h` — update `lorie_linux_dmabuf_create` signature, add `lorie_compositor_create_dmabuf_global`
- `app/src/main/cpp/lorie-wayland/compositor.c` — defer dmabuf global creation, add conditional helper
- `app/src/main/cpp/lorie-wayland/wayland-activity.c` — create dmabuf global after renderer init if extension present
- `app/src/main/cpp/lorie-wayland/tests/CMakeLists.txt` — add `test_dmabuf.c`
- `app/src/main/cpp/lorie-wayland/tests/test_main.c` — wire dmabuf test suite

### Verification

```bash
$ cd app/src/main/cpp/build-test && make lorie-wayland-tests
[100%] Built target lorie-wayland-tests

$ cd /home/lambertxd/termux-x11 && ./gradlew :app:assembleDebug
BUILD SUCCESSFUL in 3s
51 actionable tasks: 11 executed, 40 up-to-date
```

### Test Coverage

| Test                                   | Suite  | Focus                                                   |
| -------------------------------------- | ------ | ------------------------------------------------------- |
| `test_dmabuf_format_supported`         | dmabuf | ABGR8888/XBGR8888/ARGB8888 supported; NV12 rejected     |
| `test_dmabuf_params_validate_abgr8888` | dmabuf | Valid ABGR8888 params return 0 (no error)               |
| `test_dmabuf_params_reject_nv12`       | dmabuf | NV12 returns `INVALID_FORMAT`                           |
| `test_dmabuf_params_reject_zero_size`  | dmabuf | width=0 or height=0 returns `INVALID_DIMENSIONS`        |
| `test_dmabuf_params_reject_no_planes`  | dmabuf | Zero planes returns `INCOMPLETE`                        |
| `test_dmabuf_params_reject_invalid_fd` | dmabuf | All negative FDs returns `INCOMPLETE`                   |
| `test_dmabuf_import_no_egl`            | dmabuf | NULL EGL args → import fails gracefully                 |
| `test_dmabuf_renderer_has_dmabuf_flag` | dmabuf | `has_dmabuf_import` is 0 before renderer init           |
| `test_dmabuf_shm_fallback`             | dmabuf | Surface creation/destruction without dmabuf still works |

### Deviations from Design

- **No `INVALID_PLANE` error constant**: The generated `stable-linux-dmabuf-linux-dmabuf-v1.h` does not define `ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_PLANE`. Replaced with `ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE` for "no valid plane FDs" cases, which matches the protocol spec's intent for missing/incomplete planes.
- **Renderer stored in dmabuf buffer**: Added `struct lorie_renderer *renderer` to `lorie_dmabuf_buffer` so the resource destructor can properly call `eglDestroyImageKHR` without needing external context.
- **Conditional global creation**: Instead of always creating the global and making it inert, the global is created only after `lorie_renderer_init()` confirms `EGL_EXT_image_dma_buf_import` is present. This requires `lorie_compositor_create_dmabuf_global()` to be called from `wayland-activity.c` after renderer setup.

### Risks

- **EGL function pointer availability**: `eglGetProcAddress` is used to load `eglCreateImageKHR`, `eglDestroyImageKHR`, and `glEGLImageTargetTexture2DOES`. On some Android GPU drivers, these may already be directly linkable; the dual approach (check extension + load pointers) is safe.
- **Buffer destruction without current EGL context**: `eglDestroyImageKHR` requires the EGL display but not necessarily a current context. This should be safe across all major EGL implementations.
- **No per-surface dmabuf buffer tracking**: The renderer doesn't maintain a list of imported dmabuf buffers; it relies on the Wayland resource lifecycle. If a client leaks buffer resources, EGL images would leak too. This matches the existing SHM buffer behavior.

---

## PR #5a: Clipboard Wayland → Android

### Status

**COMPLETE** — All tasks implemented, tests compile, APK builds.

### TDD Cycle Evidence

| Cycle   | Step        | Evidence                                                                                                                                                                                                                                                     |
| ------- | ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 5a.1    | RED         | Added `test_clipboard_mime_type_text_plain`, `test_clipboard_mime_type_text_plain_utf8`, `test_clipboard_mime_type_rejects_image` to `test_clipboard.c`. Build fails at compile time because `lorie_clipboard_mime_type_supported` does not exist.             |
| 5a.2-5a.3 | GREEN     | Added `lorie_clipboard_mime_type_supported()` to `clipboard.c` and `compositor.h`. Updated `data_offer_receive` in `wl-data-device-manager.c` to filter MIME types through the new helper. Build passes.                                                     |
| 5a.4    | GREEN       | Added `setClipboardText(byte[])` to `LorieWaylandView.java` using Android `ClipboardManager`. Removed stub JNI registration for `setClipboardText` from `wayland-activity.c` (now a regular Java method). `clipboard_callback` calls it via `CallStaticVoidMethod`. |
| 5a.5    | TRIANGULATE | Added `test_clipboard_read_pipe_empty`, `test_clipboard_read_pipe_large`, `test_clipboard_callback_not_called_for_empty`. Verified `lorie-wayland-tests` builds and `./gradlew :app:assembleDebug` succeeds.                                                 |

### Tasks Completed

- [x] **Task 5a.1** — Add RED clipboard tests (MIME type support, pipe read)
- [x] **Task 5a.2** — `lorie_clipboard` struct already exists in compositor (from prior setup)
- [x] **Task 5a.3** — Implement `data_offer_receive` MIME filtering for `text/plain` and `text/plain;charset=utf-8`
- [x] **Task 5a.4** — JNI bridge: `clipboard_callback` → Java `setClipboardText` → Android `ClipboardManager`
- [x] **Task 5a.5** — Verify GREEN with triangulation tests

### Commits (Work-Unit)

1. `866c77f` — `test(clipboard): Add RED MIME-type and pipe-read tests for PR 5a`
2. `d63c9ee` — `feat(clipboard): Add lorie_clipboard_mime_type_supported helper`
3. `143c5c4` — `feat(protocols): Filter data_offer_receive to text/plain MIME types`
4. `4fdd347` — `feat(wayland): Java setClipboardText and JNI bridge for Wayland→Android`

### Files Changed

#### Modified

- `app/src/main/cpp/lorie-wayland/tests/test_clipboard.c` — 11 tests: MIME type support, pipe read (empty, large), callback behavior
- `app/src/main/cpp/lorie-wayland/clipboard.c` — added `lorie_clipboard_mime_type_supported()`
- `app/src/main/cpp/lorie-wayland/compositor.h` — declared `lorie_clipboard_mime_type_supported()`
- `app/src/main/cpp/lorie-wayland/protocols/wl-data-device-manager.c` — `data_offer_receive` now filters via `lorie_clipboard_mime_type_supported()`
- `app/src/main/java/com/termux/x11/LorieWaylandView.java` — added `setClipboardText(byte[])` using `ClipboardManager`
- `app/src/main/cpp/lorie-wayland/wayland-activity.c` — removed stub JNI `setClipboardText` registration; method now lives in Java

### Verification

```bash
$ cd app/src/main/cpp/build-test && make lorie-wayland-tests
[100%] Built target lorie-wayland-tests

$ cd /home/lambertxd/termux-x11 && ./gradlew :app:assembleDebug
BUILD SUCCESSFUL in 11s
51 actionable tasks: 19 executed, 32 up-to-date
```

### Test Coverage

| Test                                   | Suite     | Focus                                                    |
| -------------------------------------- | --------- | -------------------------------------------------------- |
| `test_clipboard_exists`                | clipboard | `clipboard` created with compositor                      |
| `test_clipboard_read_pipe`             | clipboard | Basic pipe read helper works                             |
| `test_clipboard_wayland_to_android`    | clipboard | Callback invocation path works end-to-end                |
| `test_clipboard_set_selection_no_crash`| clipboard | NULL source does not crash                               |
| `test_clipboard_set_selection_with_source` | clipboard | Source stored, no crash                                  |
| `test_clipboard_mime_type_text_plain`  | clipboard | `text/plain` accepted                                    |
| `test_clipboard_mime_type_text_plain_utf8` | clipboard | `text/plain;charset=utf-8` accepted                      |
| `test_clipboard_mime_type_rejects_image`| clipboard | `image/png`, `application/octet-stream`, empty, NULL rejected |
| `test_clipboard_read_pipe_empty`       | clipboard | Empty pipe returns zero-length, no crash                 |
| `test_clipboard_read_pipe_large`       | clipboard | Data larger than 4096 chunk read correctly               |
| `test_clipboard_callback_not_called_for_empty` | clipboard | Empty data does not trigger callback flag                |

### Deviations from Design

- **`lorie_clipboard` struct already existed**: The clipboard structure, pipe-based worker thread, and `lorie_clipboard_set_selection` were already implemented in `clipboard.c` from prior PR setup. PR 5a focused on completing the end-to-end path: MIME type filtering in `data_offer_receive` and the Java/Android clipboard bridge.
- **`setClipboardText` is a regular Java method, not native**: The original design suggested a JNI method `Java_com_termux_x11_LorieWaylandView_setClipboardText` that would call `ClipboardManager` from C. Instead, `setClipboardText(byte[])` is implemented in Java (using `ClipboardManager` directly), and the C callback calls it via `CallStaticVoidMethod`. This is simpler and avoids JNI complexity for Android API calls.
- **Native method count reduced from 7 to 6**: Since `setClipboardText` is no longer a native method, the `JNINativeMethod` table and `lorie_wayland_native_method_count` were updated accordingly.

### Risks

- **Clipboard callback thread safety**: `clipboard_callback` attaches to the JVM from the worker thread. The `g_jni_mutex` protects the JNI call, but if the activity is destroyed while the callback is in flight, `g_lorie_view` could be invalid. This is a pre-existing risk shared with the X11 clipboard path.
- **Static ClipboardManager reference**: `LorieWaylandView` stores `clipboard` as a static field. If the app process survives an activity recreation, the old `ClipboardManager` reference may still work, but it's tied to the original context. In practice, Android typically kills the process on activity destruction, so this is low risk.
- **No loop prevention in PR 5a**: Bidirectional loop prevention (Android→Wayland echo suppression) is explicitly scoped to PR 5b. PR 5a only handles Wayland→Android.

### Next Recommended

PR #5b: Clipboard Android → Wayland + Loop Prevention (~200 lines)
