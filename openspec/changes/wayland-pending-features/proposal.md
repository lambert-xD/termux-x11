# Proposal: Wayland Compositor Pending Features

## Executive Summary

The Lorie Wayland compositor (10 PRs completed) has a functional foundation: display server, core protocols, renderer, input, xdg-shell, and shm texture import. Five features remain incomplete and block real-world usability. They are ordered by dependency — each feature is a prerequisite for the next. Total estimated scope is ~1,400 lines across 5 PRs. All PRs fit within the 400-line review budget individually.

## Problem Statement

The compositor currently compiles and passes protocol-level unit tests, but several critical paths are stubs or no-ops:

1. **NDK compilation is broken** — `lorie-wayland/` sources are not linked into the main APK build. `renderer.c` contains EGL/GLES type stubs that conflict with real NDK headers, and `shm_create_pool` is a no-op. Without fixing this first, no later feature can be verified on device.
2. **Damage tracking is a no-op** — `lorie_renderer_damage_surface()` is empty. The renderer calls `glClear(GL_COLOR_BUFFER_BIT)` every frame, which wastes GPU time and battery on mobile devices.
3. **Buffer transforms and scale are ignored** — `buffer_transform` and `buffer_scale` are stored on `lorie_surface` but never applied in the renderer. There is no `wp_viewporter` protocol support. The renderer uses a static fullscreen quad, so rotated or scaled buffers render incorrectly.
4. **DMA-BUF import is a skeleton** — `linux-dmabuf.c` has protocol bindings but `params_create` closes FDs without importing them. No `EGL_EXT_image_dma_buf_import` path exists, so GPU zero-copy buffer sharing is impossible.
5. **Clipboard is unidirectional and dropped** — `wl_data_device_manager` protocol exists but `data_device_set_selection` does not transfer data. `wayland-activity.c` receives clipboard bytes from Java but immediately frees them without forwarding to Wayland clients. Android → Wayland and Wayland → Android clipboard paths are both missing.

## Goals

1. Fix NDK compilation so `lorie-wayland/` builds as part of the main APK and runs on Android devices.
2. Implement per-surface damage tracking with `pixman_region32_t` → `glScissor` propagation.
3. Apply `buffer_transform` and `buffer_scale` in the renderer, add `wp_viewporter` protocol support, and replace the static fullscreen quad with a transformable vertex shader matrix.
4. Complete DMA-BUF import using `EGL_EXT_image_dma_buf_import`, including format negotiation and plane validation.
5. Implement bidirectional clipboard: Android → Wayland and Wayland → Android, with pipe-based fd passing and `ClipboardManager` integration.

## Non-Goals

- Do NOT rewrite the renderer in Vulkan or switch away from GLES2.
- Do NOT implement `linux-dmabuf` feedback or modifier negotiation (beyond basic format support).
- Do NOT implement drag-and-drop (only selection/clipboard).
- Do NOT add multi-seat or multi-output support.
- Do NOT change the existing X11 server code.

## Success Criteria

| #   | Criterion                                                                  | How Verified                                                   |
| --- | -------------------------------------------------------------------------- | -------------------------------------------------------------- |
| 1   | `lorie-wayland/` compiles and links into the APK for all Android ABIs      | CI build + `gradlew assembleDebug`                             |
| 2   | `test_renderer` passes on an Android device or emulator                    | Device test run                                                |
| 3   | Damage tracking reduces fullscreen clears — only damaged regions redraw    | Manual test with `simple-damage` client or framebuffer capture |
| 4   | Rotated/scaled buffers render correctly; `wp_viewporter` global advertised | `weston-info` or custom client                                 |
| 5   | DMA-BUF buffers display without shm copy; `simple-dmabuf-egl` works        | Client test                                                    |
| 6   | Copying text on Android pastes into a Wayland client, and vice versa       | End-to-end manual test                                         |
| 7   | No regressions in existing X11 mode                                        | X11 smoke test                                                 |

## Risks

| Risk                                               | Impact                                       | Mitigation                                                                                                |
| -------------------------------------------------- | -------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| NDK EGL/GLES header conflicts                      | High — blocks all GPU-dependent features     | Remove stubs, include `<EGL/egl.h>` and `<GLES2/gl2.h>`, gate behind `#ifdef __ANDROID__` for test builds |
| Damage tracking exposes renderer mutex contention  | Medium — frame drops                         | Profile with `systrace`; reduce lock scope                                                                |
| DMA-BUF format negotiation is device-specific      | Medium — works on some GPUs, fails on others | Start with `DRM_FORMAT_ABGR8888` and `DRM_FORMAT_XBGR8888`; gracefully fall back to shm                   |
| Clipboard fd passing across JNI is fragile         | Medium — leaks or crashes                    | Use `AFileDescriptor` or `ParcelFileDescriptor` for safe fd handoff; close fds in all paths               |
| Loop prevention (Android ↔ Wayland clipboard sync) | Low — infinite ping-pong                     | Add sequence token or source tag; ignore self-originated updates                                          |

## Feature Proposals

### 1. NDK Compilation Verification (~150 lines, 1 PR)

**Scope:**

- Remove EGL/GLES type stubs from `renderer.c` (use real NDK headers).
- Implement `shm_create_pool` in `compositor.c` (wrap `mmap` + `wl_shm_pool` create).
- Link `lorie-wayland/` sources into the main `CMakeLists.txt` / `build.gradle`.
- Add a CI step that builds the APK.

**Rationale:** Must be first. Without this, no feature can be tested on a real Android device.

**Rollback:** Revert `CMakeLists.txt` changes; stubs remain in git history.

---

### 2. Real Damage Tracking (~200 lines, 1 PR)

**Scope:**

- Implement `lorie_renderer_damage_surface()` to merge damage rectangles into `s->damage` (already a `pixman_region32_t`).
- In `lorie_renderer_commit()`, compute per-surface damage region, apply `glScissor` before texture draw.
- Clear the damage region after successful commit.
- If damage region is empty, skip the surface redraw entirely.

**Dependencies:** PR 1 (renderer must compile with real GLES).

**Rollback:** Replace `glScissor` path with `glClear(GL_COLOR_BUFFER_BIT)` fallback.

---

### 3. Texture Binding + Transforms/Scales + Viewporter (~300 lines, 1 PR)

**Scope:**

- Add a vertex-shader matrix uniform for transform/scale.
- In `lorie_renderer_commit()`, read `buffer_transform` and `buffer_scale` from `lorie_surface`, compute a 4×4 matrix (rotation + scale), upload to shader.
- Replace static fullscreen quad with a vertex buffer sized to the surface’s logical dimensions.
- Generate `wp_viewporter` protocol bindings (add to `wayland-protocols.cmake`).
- Implement `wp_viewporter` and `wp_viewport` in a new `protocols/viewporter.c`.

**Dependencies:** PR 2 (damage tracking must work so transformed surfaces redraw correctly).

**Rollback:** Revert shader/matrix changes; keep static quad as fallback.

---

### 4. DMA-BUF Import (~350 lines, 1 PR)

**Scope:**

- In `params_create`, validate plane count, format, and dimensions.
- Import FDs via `eglCreateImageKHR` with `EGL_EXT_image_dma_buf_import`.
- Create `EGLImage` → `GL_TEXTURE_EXTERNAL_OES` or `GL_TEXTURE_2D` path.
- Store imported texture ID in a `lorie_dmabuf_buffer` struct attached to `wl_buffer` user data.
- In the renderer, detect dmabuf-backed buffers and bind their texture.
- Handle `AHardwareBuffer` import path if available (`EGL_ANDROID_image_native_buffer`).

**Dependencies:** PR 3 (renderer must support texture binding and transforms).

**Rollback:** Revert `linux-dmabuf.c` changes; fall back to stub that closes FDs.

---

### 5. Bidirectional Clipboard (~400 lines, 1–2 PRs)

**Scope:**

- **Wayland → Android:**
  - Implement `data_offer_receive` in `wl-data-device-manager.c` to accept `text/plain;charset=utf-8`.
  - Create a pipe pair, offer the write end to the Wayland client, read the read end in a worker thread.
  - Forward received text to Java via JNI (`ClipboardManager.setPrimaryClip`).
- **Android → Wayland:**
  - In `wayland-activity.c`, instead of freeing clipboard bytes, create a `wl_data_source` and offer `text/plain;charset=utf-8`.
  - When a Wayland client requests the selection, write the cached text into the offered fd.
- **Loop prevention:** Tag clipboard updates with source (`android` vs `wayland`); ignore echo.

**Dependencies:** PR 4 (DMA-BUF is independent, but clipboard is the last feature and has the most lines).

**Rollback:** Revert `wl-data-device-manager.c` and `wayland-activity.c`; clipboard returns to no-op.

## Recommended Order

```
PR 1: NDK Compilation Verification
   │
   ▼
PR 2: Real Damage Tracking
   │
   ▼
PR 3: Texture Binding + Transforms/Scales + Viewporter
   │
   ▼
PR 4: DMA-BUF Import
   │
   ▼
PR 5: Bidirectional Clipboard (may split into 5a + 5b if >400 lines)
```

## Delivery Strategy

All individual PRs are within the 400-line budget. PR 5 is at the boundary; if it exceeds 400 lines during implementation, split into:

- **5a:** Wayland → Android clipboard (~200 lines)
- **5b:** Android → Wayland clipboard + loop prevention (~200 lines)

Use **stacked PRs to `main`** because each feature is independently reviewable and can land without waiting for the next.

## Estimated Total Scope

| Feature              | Lines      | PRs     |
| -------------------- | ---------- | ------- |
| NDK Compile          | ~150       | 1       |
| Damage Tracking      | ~200       | 1       |
| Texture + Transforms | ~300       | 1       |
| DMA-BUF Import       | ~350       | 1       |
| Clipboard            | ~400       | 1–2     |
| **Total**            | **~1,400** | **5–6** |
