# Fresh-Context Adversarial Review — termux-x11 Wayland Compositor

Branch: `wayland-compositor-rewrite` (29 commits ahead of origin)
Scope: 13 files across renderer, surface, compositor, protocols (dmabuf, viewporter, data-device), clipboard, and JNI bridge.

---

## BLOCKER (must fix before push)

### B1. renderer.c — stack buffer overflow with >64 surfaces
**File:** `app/src/main/cpp/lorie-wayland/renderer.c`  
**Line:** `lorie_renderer_commit()`, around `struct renderer_surface *sorted[64];` and `qsort(sorted, count, ...)`  
**Severity:** BLOCKER

`count` is computed as the total number of surfaces, but only the first 64 are stored in `sorted`. The subsequent `qsort(sorted, count, ...)` call passes the full `count`; if `count > 64`, `qsort` reads past the end of the stack array. Additionally, the render loop iterates `j < count`, so surfaces beyond 64 are still accessed in `sorted[]`, causing further out-of-bounds reads.

**Suggested fix:** Dynamically allocate `sorted` with `calloc(count, sizeof(*sorted))`, or clamp `count` to the array size and use the clamped value for both `qsort` and the render loop. If a hard limit is intentional, reject or ignore surfaces beyond the limit consistently.

---

### B2. wayland-activity.c — JNI callback targets wrong class
**File:** `app/src/main/cpp/lorie-wayland/wayland-activity.c`  
**Line:** `Java_com_termux_x11_WaylandEntryPoint_start()`, `clipboard_callback()`  
**Severity:** BLOCKER

`g_lorie_view` is assigned from `clazz` (the `WaylandEntryPoint` jclass). `clipboard_callback` later calls `GetStaticMethodID(env, g_lorie_view, "setClipboardText", ...)` and `CallStaticVoidMethod(env, g_lorie_view, ...)`. The method `setClipboardText` is defined in `LorieWaylandView`, **not** in `WaylandEntryPoint`. JNI resolution will fail with `NoSuchMethodError` at runtime.

**Suggested fix:** Store a global ref to `LorieWaylandView`'s jclass instead (find it with `FindClass` and `NewGlobalRef`), or move the callback method to `WaylandEntryPoint`.

---

### B3. surface.c / viewporter.c — use-after-free when surface destroyed before viewport
**File:** `app/src/main/cpp/lorie-wayland/surface.c` and `protocols/viewporter.c`  
**Line:** `surface_handle_resource_destroy()`; `viewport_handle_resource_destroy()`  
**Severity:** BLOCKER

`viewporter_get_viewport` creates a `wp_viewport` resource whose user_data points to the surface `s`. The viewport's destroy handler (`viewport_handle_resource_destroy`) dereferences `s`. However, `surface_handle_resource_destroy` does **not** destroy `s->viewport_resource`. If the surface is destroyed first (e.g., client destroys `wl_surface` before `wp_viewport`), the viewport resource outlives the surface. When the viewport is later destroyed, `viewport_handle_resource_destroy` accesses freed memory.

**Suggested fix:** In `surface_handle_resource_destroy`, if `s->viewport_resource` is non-NULL, call `wl_resource_destroy(s->viewport_resource)` before freeing `s`.

---

### B4. clipboard.c / wl-data-device-manager.c — dangling `current_source` pointer
**File:** `app/src/main/cpp/lorie-wayland/clipboard.c`  
**Line:** `lorie_clipboard_set_selection()`  
**Severity:** BLOCKER

`cb->current_source` stores a raw `wl_resource*` to the current data source. If the client destroys that source, `cb->current_source` becomes dangling. The next call to `lorie_clipboard_set_selection` will call `wl_data_source_send_cancelled(cb->current_source)` on the stale pointer, causing a use-after-free or crash.

**Suggested fix:** Add a resource destroy handler to the `wl_data_source` that clears `cb->current_source` (e.g., by storing a back-pointer to the clipboard struct in the source's user_data).

---

## CRITICAL (should fix before push)

### C1. renderer.c — frame callbacks fired without surfaces lock
**File:** `app/src/main/cpp/lorie-wayland/renderer.c`  
**Line:** `lorie_renderer_commit()`, frame callback loop  
**Severity:** CRITICAL

Frame callbacks are sent after `pthread_mutex_unlock(&r->surfaces_lock)` but before `pthread_mutex_lock(&r->egl_lock)`. Meanwhile, `surface_frame()` (in `surface.c`) adds callbacks while the Wayland event loop runs on another thread. There is no synchronization; `wl_list_for_each_safe` on `s->frame_callbacks` races with `wl_list_insert` in `surface_frame`, leading to list corruption or use-after-free.

**Suggested fix:** Hold `surfaces_lock` while iterating and sending frame callbacks, or use a separate lock for frame callbacks.

---

### C2. renderer.c — `lorie_renderer_commit` uses surfaces after releasing lock
**File:** `app/src/main/cpp/lorie-wayland/renderer.c`  
**Line:** `lorie_renderer_commit()`, after `pthread_mutex_unlock(&r->surfaces_lock)`  
**Severity:** CRITICAL

The function locks `surfaces_lock`, counts surfaces, copies pointers into `sorted[]`, then unlocks. It then computes transform matrices, fires frame callbacks, and renders—all without the lock. Another thread can call `lorie_renderer_add_surface`, `lorie_renderer_remove_surface`, or destroy a surface in between. This leads to use-after-free (if a surface is removed/destroyed) or reading inconsistent state.

**Suggested fix:** Keep `surfaces_lock` held for the entire commit sequence, or copy all needed surface data under the lock and reference-count surfaces so they can't be freed until commit finishes.

---

### C3. renderer.c — `lorie_renderer_surface_get_damage` returns pointer to protected data
**File:** `app/src/main/cpp/lorie-wayland/renderer.c`  
**Line:** `lorie_renderer_surface_get_damage()`  
**Severity:** CRITICAL

This function returns `&rs->accumulated_damage` after releasing `surfaces_lock`. The caller receives a pointer to data that is no longer protected. Concurrent access (e.g., `lorie_renderer_damage_surface`) will corrupt the region.

**Suggested fix:** Do not return internal pointers across lock boundaries. Return a copy of the region, or provide an accessor that operates under the lock.

---

### C4. surface.c — duplicate frame callbacks sent to clients
**File:** `app/src/main/cpp/lorie-wayland/surface.c` and `renderer.c`  
**Line:** `surface_commit()` in surface.c; `lorie_renderer_commit()` in renderer.c  
**Severity:** CRITICAL

`surface_commit` sends `wl_callback.done` and destroys all frame callbacks. `lorie_renderer_commit` does the **same thing** again before rendering. Clients receive two `done` events per frame request, violating Wayland protocol semantics and likely breaking client frame throttling.

**Suggested fix:** Remove the frame callback loop from `surface_commit`. Frame callbacks should only be sent when the surface is actually presented (i.e., in the renderer).

---

### C5. surface.c — `wl_surface.set_buffer_scale` allows zero, causing division by zero
**File:** `app/src/main/cpp/lorie-wayland/surface.c`  
**Line:** `surface_set_buffer_scale()`  
**Severity:** CRITICAL

The function stores `scale` without validation. Wayland protocol requires scale >= 1. `lorie_surface_compute_logical_size` divides by `s->buffer_scale`; a value of 0 causes division by zero (undefined behavior, likely crash).

**Suggested fix:** Reject scale <= 0 with `wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_SCALE, ...)`.

---

### C6. surface.c — `wl_surface.attach` offsets ignored
**File:** `app/src/main/cpp/lorie-wayland/surface.c`  
**Line:** `surface_attach()` and `surface_commit()`  
**Severity:** CRITICAL

`surface_attach` stores `pending_x` and `pending_y`, but `surface_commit` never applies them to `s->x` and `s->y`. The fields remain 0. `compute_transform_matrix` in renderer.c uses `s->x` and `s->y` for positioning, so subsurfaces and offset surfaces will always render at (0,0). This is a protocol compliance violation.

**Suggested fix:** In `surface_commit`, if `pending_attached`, set `s->x = s->pending_x; s->y = s->pending_y;`.

---

### C7. linux-dmabuf.c — non-contiguous plane indices silently skipped
**File:** `app/src/main/cpp/lorie-wayland/protocols/linux-dmabuf.c`  
**Line:** `params_add()` and `lorie_dmabuf_buffer_import()`  
**Severity:** CRITICAL

`params_add` increments `params->num_planes` on every call, but the import loop iterates `i < num_planes`. If a client adds planes 0 and 2 (skipping 1), `num_planes` becomes 2, so the loop only inspects indices 0 and 1; plane 2 is never imported. Multi-plane YUV buffers will fail or import incorrectly.

**Suggested fix:** Iterate all 4 slots in the import loop (or track a bitmask of which planes are present), regardless of `num_planes`.

---

### C8. viewporter.c — committed viewport not reset on viewport destruction
**File:** `app/src/main/cpp/lorie-wayland/protocols/viewporter.c`  
**Line:** `viewport_handle_resource_destroy()`  
**Severity:** CRITICAL

Per `wp_viewporter` protocol: "When the wp_viewport object is destroyed, the crop and destination state are reset to their default values." The code only clears `pending_viewport`; it does **not** reset `s->viewport` (the committed state). The surface continues to use the old crop/destination until the next commit.

**Suggested fix:** Also `memset(&s->viewport, 0, sizeof(s->viewport));` in the destroy handler.

---

### C9. wl-data-device-manager.c — dangling `offer->source` pointer
**File:** `app/src/main/cpp/lorie-wayland/protocols/wl-data-device-manager.c`  
**Line:** `data_offer_receive()`  
**Severity:** CRITICAL

`offer->source` points to a `lorie_data_source` owned by a `wl_data_source` resource. If the client destroys the source resource, `data_source_handle_destroy` frees the source struct, but offers referencing it are not notified. `data_offer_receive` may then dereference `offer->source` (or send events to `offer->source->resource`), causing use-after-free.

**Suggested fix:** Clear `offer->source` in `data_source_handle_destroy` (requires tracking offers per source), or reference-count / validate the resource before use.

---

### C10. clipboard.c — `lorie_clipboard_get_android_text` returns pointer after releasing lock
**File:** `app/src/main/cpp/lorie-wayland/clipboard.c`  
**Line:** `lorie_clipboard_get_android_text()`  
**Severity:** CRITICAL

The function locks `cb->lock`, sets `*out_len` and `text = cb->android_text`, then unlocks. It returns `text`, but another thread can immediately call `lorie_clipboard_send_android_text`, which frees `cb->android_text` and reallocates it. The caller (e.g., `data_offer_receive`) then writes from freed memory.

**Suggested fix:** Copy the text under the lock into an output buffer provided by the caller, or require the caller to hold the lock.

---

### C11. compositor.c — resource leaks on `fail_globals` path
**File:** `app/src/main/cpp/lorie-wayland/compositor.c`  
**Line:** `lorie_compositor_create()`, `fail_globals:` label  
**Severity:** CRITICAL

If global creation fails after `c->input`, `c->xdg_shell_global`, `c->clipboard`, `c->data_device_manager_global`, or `c->viewporter_global` were successfully created, the `fail_globals` path only destroys the first three globals and the display. It leaks input, xdg_shell, clipboard, data_device_manager, and viewporter objects.

**Suggested fix:** Destroy all successfully-initialized subsystems in reverse order on the failure path, or refactor to a chained init/teardown pattern.

---

### C12. compositor.c — data race on `c->running`
**File:** `app/src/main/cpp/lorie-wayland/compositor.c`  
**Line:** `event_loop_thread_fn()` and `lorie_compositor_stop()`  
**Severity:** CRITICAL

`event_loop_thread_fn` reads `c->running` in a `while` loop with no synchronization. `lorie_compositor_stop` writes `c->running = 0` from the main thread. This is a data race (plain `int`, no atomic, no mutex, no memory barrier). On ARM64 with weak memory ordering, the event loop thread may never observe the write and hang in `wl_event_loop_dispatch` forever.

**Suggested fix:** Make `running` `atomic_int` (C11) or `_Atomic int`, or protect it with `c->lock`.

---

### C13. wayland-activity.c — race between `clipboard_callback` and `stop`
**File:** `app/src/main/cpp/lorie-wayland/wayland-activity.c`  
**Line:** `clipboard_callback()` and `Java_com_termux_x11_WaylandEntryPoint_stop()`  
**Severity:** CRITICAL

`clipboard_callback` runs on a background thread and uses `g_lorie_view` without any check. `stop` may be called concurrently, deleting the global ref and setting `g_lorie_view = NULL`. The callback may see the ref being deleted or NULL, leading to JNI crashes.

**Suggested fix:** Synchronize access to `g_lorie_view` (e.g., with `g_jni_mutex` or a dedicated mutex), and check for NULL before use.

---

### C14. wayland-activity.c — `sendTextEvent` breaks UTF-8 text
**File:** `app/src/main/cpp/lorie-wayland/wayland-activity.c`  
**Line:** `Java_com_termux_x11_LorieWaylandView_sendTextEvent()`  
**Severity:** CRITICAL

The function iterates over raw bytes and sends each byte > 32 as a key press/release. UTF-8 multi-byte characters are split into individual bytes, producing invalid keycodes for non-ASCII input.

**Suggested fix:** Decode UTF-8 properly and send keysyms or use a proper text-input protocol instead of emulating key presses per byte.

---

## WARNING (fix in follow-up)

### W1. renderer.c — unchecked EGL errors
**File:** `app/src/main/cpp/lorie-wayland/renderer.c`  
**Line:** `lorie_renderer_set_window()`, `lorie_renderer_commit()`  
**Severity:** WARNING
`eglMakeCurrent`, `eglSwapBuffers`, and some GL calls are not checked for errors. Failures are silent.

### W2. renderer.c — shader compile errors not logged
**File:** `app/src/main/cpp/lorie-wayland/renderer.c`  
**Line:** `compile_shader()`  
**Severity:** WARNING
If shader compilation fails, the error log is not retrieved with `glGetShaderInfoLog`. Debugging shader issues is impossible.

### W3. renderer.c — hardcoded 64-surface limit
**File:** `app/src/main/cpp/lorie-wayland/renderer.c`  
**Line:** `lorie_renderer_commit()`  
**Severity:** WARNING
Magic number `64` with no documentation. Should be a named constant or dynamic allocation.

### W4. surface.c — SHM buffer copy lacks bounds validation
**File:** `app/src/main/cpp/lorie-wayland/surface.c`  
**Line:** `surface_commit()`, memcpy loop  
**Severity:** WARNING
`wl_shm_buffer_get_stride` is trusted without validating it fits within the pool size. A malicious client could cause out-of-bounds reads.

### W5. surface.c — `s->damage` region never read
**File:** `app/src/main/cpp/lorie-wayland/surface.c`  
**Line:** `surface_damage()`  
**Severity:** WARNING
Damage is accumulated into `s->damage` but the renderer only uses `rs->accumulated_damage`. `s->damage` is dead code.

### W6. linux-dmabuf.c — zombie buffer on immediate create failure
**File:** `app/src/main/cpp/lorie-wayland/protocols/linux-dmabuf.c`  
**Line:** `do_params_create()`  
**Severity:** WARNING
When `immed` is true and import fails (e.g., no renderer), the `wl_buffer` resource is created but never destroyed, leaving a zombie object.

### W7. linux-dmabuf.c — version 4 without modifier support
**File:** `app/src/main/cpp/lorie-wayland/protocols/linux-dmabuf.c`  
**Line:** `lorie_linux_dmabuf_create()`  
**Severity:** WARNING
Global version is 4, but `send_modifier` is never called. Clients that require modifier negotiation may treat this as an error.

### W8. wl-data-device-manager.c — `data_offer_receive` may block
**File:** `app/src/main/cpp/lorie-wayland/protocols/wl-data-device-manager.c`  
**Line:** `data_offer_receive()`  
**Severity:** WARNING
`write(fd, text, len)` can block if the client does not read from the pipe, stalling the Wayland event loop.

### W9. wl-data-device-manager.c — selection hardcodes MIME types
**File:** `app/src/main/cpp/lorie-wayland/protocols/wl-data-device-manager.c`  
**Line:** `data_device_set_selection()`  
**Severity:** WARNING
The code ignores the source's advertised MIME types and always offers `text/plain` and `text/plain;charset=utf-8`. If the source only offers other types, this is a protocol violation.

### W10. wl-data-device-manager.c — old offers not destroyed on new selection
**File:** `app/src/main/cpp/lorie-wayland/protocols/wl-data-device-manager.c`  
**Line:** `lorie_clipboard_send_android_selection()`  
**Severity:** WARNING
A new selection sends a new offer without destroying the previous one. Offer resources accumulate until the client disconnects.

### W11. clipboard.c — unbounded pipe read
**File:** `app/src/main/cpp/lorie-wayland/clipboard.c`  
**Line:** `lorie_clipboard_read_pipe()`  
**Severity:** WARNING
There is no enforcement of `MAX_CLIPBOARD_SIZE` during the read loop. A malicious Wayland client can write unlimited data and exhaust memory.

### W12. clipboard.c — `send_cancelled` under lock
**File:** `app/src/main/cpp/lorie-wayland/clipboard.c`  
**Line:** `lorie_clipboard_set_selection()`  
**Severity:** WARNING
Calling `wl_data_source_send_cancelled` while holding `cb->lock` could deadlock if the client callback re-enters clipboard code.

### W13. wayland-activity.c — unvalidated input coordinates
**File:** `app/src/main/cpp/lorie-wayland/wayland-activity.c`  
**Line:** `sendMouseEvent()`, `sendTouchEvent()`  
**Severity:** WARNING
Pointer/touch coordinates and action values are passed directly to native without bounds checking.

---

## SUGGESTION (nice to have)

### S1. renderer.c — unused `lorie_renderer_filtering`
**File:** `app/src/main/cpp/lorie-wayland/renderer.c`, `renderer.h`  
**Severity:** SUGGESTION
`atomic_int lorie_renderer_filtering` is declared but never used in the renderer. Either wire it up to texture filtering or remove it.

### S2. renderer.c — hardcoded fallback output size
**File:** `app/src/main/cpp/lorie-wayland/renderer.c`  
**Line:** `get_output_size()`  
**Severity:** SUGGESTION
Defaults to 1920x1080. Should query the actual output or ANativeWindow size.

### S3. renderer.c — unchecked `LorieBuffer` operations
**File:** `app/src/main/cpp/lorie-wayland/renderer.c`  
**Line:** `lorie_renderer_commit()`  
**Severity:** SUGGESTION
`LorieBuffer_attachToGL` and `LorieBuffer_bindTexture` return values are ignored.

### S4. clipboard.c — unused `sequence` field
**File:** `app/src/main/cpp/lorie-wayland/clipboard.c`  
**Line:** `lorie_clipboard_send_android_text()`  
**Severity:** SUGGESTION
`cb->sequence` is incremented but never read. Dead code.

### S5. wayland-activity.c — cache JNI method ID
**File:** `app/src/main/cpp/lorie-wayland/wayland-activity.c`  
**Line:** `clipboard_callback()`  
**Severity:** SUGGESTION
`GetStaticMethodID` is called on every clipboard update. Cache it after first lookup.

### S6. wayland-activity.c — document touch action constants
**File:** `app/src/main/cpp/lorie-wayland/wayland-activity.c`  
**Line:** `sendTouchEvent()`  
**Severity:** SUGGESTION
Hardcoded `0`, `1`, `2` for down/up/move. Add comments mapping them to Android `MotionEvent` constants.

### S7. compositor.c — unused `native_window` and `clients` list
**File:** `app/src/main/cpp/lorie-wayland/compositor.c`, `compositor.h`  
**Severity:** SUGGESTION
`c->native_window` is stored but never used (renderer holds its own). `c->clients` list is initialized but unused (libwayland manages clients).

### S8. General — many `(void)arg` casts
**Severity:** SUGGESTION
Several callback stubs cast unused arguments to `(void)`. This is acceptable C idiom, but cleaning them up or using `__attribute__((unused))` improves readability.

---

## Build Integration

No tracked build artifacts found. `.gitignore` correctly excludes `.cxx/`, `build/`, `*.o`, `*.so`, and generated CMake files. Build integration is clean.

---

## Summary Counts

| Severity | Count |
|----------|-------|
| BLOCKER  | 4     |
| CRITICAL | 14    |
| WARNING  | 13    |
| SUGGESTION | 8   |
