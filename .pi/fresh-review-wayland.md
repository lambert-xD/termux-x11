# Fresh-Context Adversarial Review: termux-x11 Wayland Compositor

**Reviewer:** el Gentleman (subagent)  
**Date:** 2026-05-20  
**Scope:** All native, Java, protocol, and build files in `app/src/main/cpp/lorie-wayland/` and related paths.  
**Context:** No prior knowledge; first-time read of the entire changeset.

---

## Executive Summary

This Wayland compositor implementation is **not safe to merge**. It contains multiple critical security vulnerabilities, severe memory safety bugs, fundamental build-breakers, and architectural disconnects between the Java UI layer, the native renderer, and the compositor core. The code appears to have been written by copy-pasting large blocks from the existing X11 path without adapting types, function names, or object lifetimes to the Wayland architecture.

**Verdict: BLOCKED until all Critical and Blocker issues are resolved.**

---

## Global / Cross-Cutting Issues

### BLOCKER: Two competing `wl_seat` globals are advertised to clients

- **Location:** `compositor.c:compositor_bind()` and `input.c:lorie_wl_input_init()`
- **Details:** `compositor.c` creates a `wl_seat` global (version 7) via `c->seat_global`. `input.c` independently creates another `wl_seat` global (version 10) via `input.seat_global`. Wayland clients connecting to this display will see **two seats**, which is a protocol violation and breaks virtually all toolkits. Additionally, the two implementations have inconsistent keyboard repeat info (`seat.c` sends 40/400; `input.c` sends 0/0).
- **Fix:** Remove the seat from `compositor.c`; let `input.c` own the seat, and have `compositor.c` call `lorie_wl_input_init(display)`.

### BLOCKER: `main.c` references an undefined struct — will not compile

- **Location:** `main.c` throughout
- **Details:** `main.c` uses `struct lorie_wayland_compositor *` everywhere, but this struct is **never defined in any header**. `sizeof(*compositor)` on an incomplete type is a hard compile error in C. The file also calls `lorie_wayland_compositor_create()`, `lorie_wayland_compositor_destroy()`, `lorie_wayland_renderer_thread()`, `lorie_wayland_xwayland_start()`, etc., none of which are declared in any header consumed by `main.c`.
- **Fix:** Either define `struct lorie_wayland_compositor` in `wayland-lorie.h` or rewrite `main.c` to use the actual `struct lorie_compositor` from `compositor.h`.

### BLOCKER: Java UI / Native renderer architectural disconnect

- **Location:** `wayland-activity.c`, `LorieWaylandView.java`, `renderer.c`
- **Details:** `wayland-activity.c` (JNI layer) calls `rendererSetWindow()`, `rendererSetViewport()`, `rendererSetSharedState()`, `rendererAddBuffer()`, etc. These are the **X11 renderer API** (forward-declared in `wayland-lorie.h`). The Wayland renderer (`renderer.c`) exposes a completely different API: `lorie_wl_renderer_init()`, `lorie_wl_renderer_set_window()`, `lorie_wl_renderer_commit()`, etc. The Java `LorieWaylandView` loads `libXlorie.so`, whose `JNI_OnLoad` is provided by `wayland-activity.c`. If the X11 `JNI_OnLoad` already exists in the same library, one will stomp the other. More importantly, **there is no code path that ever calls `lorie_wl_renderer_commit()`** or connects the Wayland compositor's `ANativeWindow` to the Wayland renderer.
- **Fix:** Unify the renderer abstraction or provide a Wayland-specific `JNI_OnLoad` that routes to `lorie_wl_renderer_*` and wires the compositor's output to the renderer.

### BLOCKER: Duplicate definitions between `lorie.h` and `wayland-lorie.h`

- **Location:** `lorie.h` and `wayland-lorie.h`
- **Details:** Both headers define `eventType`, `lorieEvent`, `struct lorie_shared_server_state`, `android_to_linux_keycode[]`, `lorie_mutex_lock()`, and `lorie_mutex_unlock()`. `main.c` includes both headers (directly or transitively), which will cause **redefinition errors** at compile time.
- **Fix:** Make `wayland-lorie.h` include `lorie.h` for the common parts, or extract shared definitions into a common header.

### CRITICAL: VLA stack-overflow in clipboard handling (security)

- **Location:** `wayland-activity.c:wayland_callback()` (EVENT_CLIPBOARD_SEND) and `main.c:handle_android_events()` (EVENT_CLIPBOARD_SEND)
- **Details:**

  ```c
  char clipboard[e.clipboardSend.count + 1];
  ```

  `count` is a `uint32_t` read from an untrusted socket. A malicious peer can send `count = 0xFFFFFFFF`, causing the VLA size to overflow to 0 (or wrap on 32-bit), and then `clipboard[e.clipboardSend.count] = 0;` writes far out of bounds. Even without overflow, a large `count` causes stack exhaustion.

  In `wayland-activity.c` the VLA is inside the callback, compounding the risk.

- **Fix:** Use `calloc()` with a hard `count` ceiling (e.g., 1 MiB). Reject oversized clipboard events before allocation.

### CRITICAL: XWayland argv corruption (launch failure / security)

- **Location:** `xwayland.c:lorie_xwayland_spawn()`
- **Details:**
  ```c
  snprintf(listen_str, sizeof(listen_str), "-listenfd");
  argv[argc++] = listen_str;
  snprintf(listen_str, sizeof(listen_str), "%d", xwayland->abstract_fd);
  argv[argc++] = listen_str;
  ```
  Both `argv` entries point to the **same buffer** `listen_str`. After the second `snprintf`, both entries contain the fd number string. XWayland receives `[..., "5", "5", ...]` instead of `[..., "-listenfd", "5", ...]`. The same bug exists for the unix fd and the `-wm` pair. XWayland will misparse arguments and likely refuse to start.
- **Fix:** Use separate `char` buffers for each argv element, or allocate argv strings with `strdup()`.

### CRITICAL: `wl_list_insert` with raw `wl_resource*` instead of `wl_list` node

- **Location:** `linux-dmabuf.c:linux_dmabuf_bind()`, `relative-pointer.c:relative_pointer_manager_bind()`, `pointer-constraints.c:pointer_constraints_bind()`, `wl-data-device-manager.c:data_device_manager_bind()`
- **Details:**
  ```c
  wl_list_insert(&dmabuf->resources, resource);
  ```
  `resource` is a `struct wl_resource*`. `wl_resource` does **not** expose a public `struct wl_list link` member that can be used this way. This will not compile, or if it does (via opaque struct access), it will corrupt memory. The correct pattern is to wrap the resource in a struct containing a `wl_list link`.
- **Fix:** Create per-resource wrapper structs (e.g., `struct lorie_dmabuf_resource { struct wl_resource *resource; struct wl_list link; };`) and insert the wrapper's `link`.

### CRITICAL: Mutex recovery via `memcpy` over live mutex is UB

- **Location:** `compositor.c:lorie_compositor_lock()`, `lorie.h:lorie_mutex_lock()`, `wayland-lorie.h:lorie_mutex_lock()`
- **Details:** On timeout, the code does:
  ```c
  pthread_mutex_t initializer = PTHREAD_MUTEX_INITIALIZER;
  memcpy(mutex, &initializer, sizeof(initializer));
  pthread_mutex_init(mutex, &attr);
  ```
  `memcpy` over a `pthread_mutex_t` that may be locked by another thread is **undefined behavior** in POSIX. It can corrupt the mutex internals, leading to deadlocks, data races, or crashes. The rationale ("bionic has no robust mutexes") does not justify UB.
- **Fix:** Use a proper monitor/condition variable pattern, or accept that a crashed peer requires process restart.

### CRITICAL: `surface_commit()` never releases `wl_buffer` to clients

- **Location:** `surface.c:surface_commit()`
- **Details:** When a new buffer is committed, the old `current_buffer_resource` is overwritten without calling `wl_buffer_send_release()` on it. Wayland clients rely on the `release` event to know when they can reuse or free the buffer. Without it, clients will eventually exhaust their buffer pool and deadlock.
- **Fix:** Before overwriting `current_buffer_resource`, call `wl_buffer_send_release(surface->current_buffer_resource)`.

### CRITICAL: `surface_attach()` never imports the `wl_buffer` into a `LorieBuffer`

- **Location:** `surface.c:surface_attach()`
- **Details:** The handler receives a `struct wl_resource* buffer_resource` but never calls `wl_shm_buffer_get()` or any DMA-BUF import path. It simply stores the resource pointer and sets `pending_buffer = NULL`. The renderer therefore has no actual pixel data to draw.
- **Fix:** Implement buffer import: detect shm vs dmabuf, create or reference a `LorieBuffer`, and store it in `surface->pending_buffer`.

### CRITICAL: `xdg-shell.c` wrapper function recursively calls itself

- **Location:** `xdg-shell.c:xdg_surface_send_configure()`
- **Details:**
  ```c
  void xdg_surface_send_configure(struct xdg_surface *surface, uint32_t serial) {
      if (surface && surface->resource) {
          xdg_surface_send_configure(surface->resource, serial);
      }
  }
  ```
  The wrapper has the **same name** as the generated protocol function. This is infinite recursion. The generated function should be called via the protocol header, but the name collision means the wrapper calls itself.
- **Fix:** Rename the wrapper (e.g., `lorie_xdg_surface_send_configure`) or ensure the generated function is in a separate namespace.

### CRITICAL: `xdg_wm_base_get_xdg_surface()` casts `wl_resource_get_user_data()` to wrong type

- **Location:** `xdg-shell.c:xdg_wm_base_get_xdg_surface()`
- **Details:**
  ```c
  struct wl_surface *wl_surface = wl_resource_get_user_data(surface_resource);
  ```
  For a `wl_surface` resource, `user_data` is `struct lorie_surface*` (set in `compositor.c:compositor_create_surface()`), not `struct wl_surface*`. This pointer type mismatch will cause crashes when dereferenced.
- **Fix:** Use `struct lorie_surface *lorie_surface = wl_resource_get_user_data(surface_resource);`.

---

## File-by-File Findings

### `compositor.c`

| Severity | Issue                                                               | Line(s)                              | Evidence                                                                                                                                                       |
| -------- | ------------------------------------------------------------------- | ------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CRITICAL | Mutex recovery UB (see global)                                      | `lorie_compositor_lock()`            | `memcpy(&c->lock, &initializer, ...)`                                                                                                                          |
| CRITICAL | Buffer release never sent to client                                 | `surface_commit()` (in `surface.c`)  | Old `current_buffer_resource` overwritten without `wl_buffer_send_release`                                                                                     |
| MAJOR    | `wl_container_of` on potentially empty list                         | `lorie_wayland_set_window()`         | `wl_container_of(c->outputs.next, NULL, link)` — if `outputs` is empty, `next` points to list head, yielding invalid pointer. Dereferenced without null-check. |
| MAJOR    | `wl_global_create` failures silently ignored                        | `lorie_wayland_init()`               | All five `wl_global_create` results stored but never checked for NULL.                                                                                         |
| MAJOR    | `wl_pointer_send_leave`/`enter` use serial 0                        | `lorie_wayland_send_pointer_event()` | Serial must be `wl_display_next_serial()`, not 0.                                                                                                              |
| MAJOR    | `wl_pointer_send_frame` never sent                                  | `lorie_wayland_send_pointer_event()` | For wl_pointer version ≥ 5, `frame` event is required after each logical event group.                                                                          |
| MAJOR    | `wl_touch_send_frame` sent per-event instead of per-frame           | `lorie_wayland_send_touch_event()`   | Frame should group all touch changes in a single logical frame.                                                                                                |
| MAJOR    | `lorie_surface_at()` returns wrong surface for overlapping windows  | `lorie_surface_at()`                 | Iterates in list order (insertion order); no z-order. Returns last match, which may be a background surface.                                                   |
| MAJOR    | `lorie_wayland_stop()` does not call `wl_display_destroy_clients()` | `lorie_wayland_stop()`               | Client resources may outlive compositor state, causing use-after-free in destroy callbacks.                                                                    |
| MAJOR    | `lorie_wayland_stop()` frees surfaces while holding lock            | `lorie_wayland_stop()`               | Destroy callbacks (e.g., `surface_handle_resource_destroy`) try to acquire the same lock → deadlock.                                                           |
| MINOR    | `set_window` acquires window after assigning pointer                | `lorie_wayland_set_window()`         | Should `ANativeWindow_acquire(window)` before `c->native_window = window` to avoid window disappearing between operations.                                     |
| MINOR    | `set_window` doesn't check `window == c->native_window`             | `lorie_wayland_set_window()`         | Releases and re-acquires same window unnecessarily.                                                                                                            |
| MINOR    | Event thread ignores `wl_event_loop_dispatch` errors                | `event_loop_thread()`                | `while (c->running) { wl_event_loop_dispatch(...); }` — never checks return value.                                                                             |

### `compositor.h`

| Severity | Issue                                                                      | Line(s)                | Evidence                                                                                                                               |
| -------- | -------------------------------------------------------------------------- | ---------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| MAJOR    | `struct lorie_compositor` missing protocol lists                           | Entire header          | `xdg_surfaces`, `xdg_toplevels`, `xdg_popups`, `dmabuf_params`, `data_devices` are referenced in protocol files but not declared here. |
| MINOR    | `shell_surface` typed as `wl_resource*` while `lorie_shell_surface` exists | `struct lorie_surface` | Inconsistent; should use `struct lorie_shell_surface*` or remove the struct.                                                           |
| MINOR    | Bitfield `pending_attached` etc. in `uint32_t` bitfield                    | `struct lorie_surface` | May cause padding/alignment surprises; not critical but unusual.                                                                       |

### `surface.c`

| Severity | Issue                                                                  | Line(s)                          | Evidence                                                                                              |
| -------- | ---------------------------------------------------------------------- | -------------------------------- | ----------------------------------------------------------------------------------------------------- |
| CRITICAL | `wl_region` resource has NULL implementation                           | `compositor_create_region()`     | `wl_resource_set_implementation(region, NULL, NULL, NULL);` — any client call crashes the compositor. |
| MAJOR    | `subcompositor_get_subsurface` allows self-parenting                   | `subcompositor_get_subsurface()` | No check `surface == parent`.                                                                         |
| MAJOR    | `subcompositor_get_subsurface` overwrites parent without cleanup       | `subcompositor_get_subsurface()` | If `surface->parent` was already set, `parent_link` is still in old parent's list → list corruption.  |
| MAJOR    | `shell_get_shell_surface` overwrites `surface->shell_surface`          | `shell_get_shell_surface()`      | No cleanup of old shell_surface resource.                                                             |
| MAJOR    | `shell_surface_set_toplevel` dereferences list head as output          | `shell_surface_set_toplevel()`   | `wl_container_of(outputs.next, output, link)` on empty list → UB.                                     |
| MINOR    | `surface_set_input_region` stores boolean but doesn't copy region data | `surface_set_input_region()`     | Region resource is ignored; only sets a boolean flag.                                                 |

### `output.c`

| Severity | Issue                                                                       | Line(s)                     | Evidence                                                                 |
| -------- | --------------------------------------------------------------------------- | --------------------------- | ------------------------------------------------------------------------ |
| MINOR    | `output_bind` sends `scale` unconditionally                                 | `output_bind()`             | Should guard with `if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)`.       |
| MINOR    | `lorie_output_damage_all` iterates all surfaces without checking list empty | `lorie_output_damage_all()` | Safe in practice, but `wl_container_of` pattern used elsewhere is risky. |

### `seat.c`

| Severity | Issue                                                   | Line(s)                    | Evidence                                                                                                                          |
| -------- | ------------------------------------------------------- | -------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| MAJOR    | Pointer/keyboard/touch resources lack destroy callbacks | `seat_get_pointer()`, etc. | `wl_resource_set_implementation(..., NULL)` for destroy callback → dangling pointers in seat lists when client destroys resource. |
| MINOR    | `wl_seat_send_name` sent unconditionally                | `seat_bind()`              | Should check `version >= WL_SEAT_NAME_SINCE_VERSION`.                                                                             |

### `renderer.c`

| Severity | Issue                                                          | Line(s)                          | Evidence                                                                                                                                                    |
| -------- | -------------------------------------------------------------- | -------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CRITICAL | Global EGL state completely unsynchronized                     | All static EGL variables         | `egl_display`, `egl_context`, `egl_surface` accessed from multiple threads without locks. `set_window()` (UI thread) races with `commit()` (render thread). |
| CRITICAL | `fire_callbacks` called while holding `surfaces.lock`          | `lorie_wl_renderer_commit()`     | If callback calls `remove_surface()`, it deadlocks on `surfaces.lock`.                                                                                      |
| CRITICAL | `eglSwapBuffers` called while holding `surfaces.lock`          | `lorie_wl_renderer_commit()`     | Blocks the renderer; if `set_window()` waits for the lock, deadlock.                                                                                        |
| CRITICAL | `AImageReader` leak in `init()`                                | `lorie_wl_renderer_init()`       | `reader` local is never destroyed via `AImageReader_delete()`.                                                                                              |
| MAJOR    | Partial init leaves stale state on failure                     | `lorie_wl_renderer_init()`       | If shader compilation fails, returns -1 but `egl_display` is already set, so retry thinks it's initialized.                                                 |
| MAJOR    | `eglCreateWindowSurface` failure leaks window                  | `lorie_wl_renderer_set_window()` | On failure, sets `current_win = NULL` without releasing the acquired window.                                                                                |
| MAJOR    | `filtering` is `volatile int` — insufficient for thread safety | Global `filtering`               | Should be `atomic_int` or guarded by mutex. `volatile` does not guarantee atomicity or memory ordering.                                                     |
| MAJOR    | `createProgram` leaks vertex shader if pixel shader fails      | `createProgram()`                | `vertexShader` is not deleted on `!pixelShader` path.                                                                                                       |
| MINOR    | Missing `mediandk` link dependency                             | `renderer.c`                     | Uses `AImageReader_*` but `CMakeLists.txt` does not link `mediandk`.                                                                                        |
| MINOR    | No z-order sorting in `commit`                                 | `lorie_wl_renderer_commit()`     | Surfaces drawn in insertion order, not stacking order.                                                                                                      |

### `renderer.h`

| Severity | Issue                             | Line(s) | Evidence                                                                                      |
| -------- | --------------------------------- | ------- | --------------------------------------------------------------------------------------------- |
| MINOR    | Missing `<android/log.h>` include | Header  | Defines `log_wl`/`loge_wl` macros using `__android_log_print` but doesn't include the header. |

### `input.c`

| Severity | Issue                                                                          | Line(s)                        | Evidence                                                                                                           |
| -------- | ------------------------------------------------------------------------------ | ------------------------------ | ------------------------------------------------------------------------------------------------------------------ |
| CRITICAL | `input_client_destroy` uses `wl_container_of` on missing member                | `input_client_destroy()`       | `struct input_client` has no `wl_listener` member; `wl_container_of(listener, ic, ...)` is a compile error / UB.   |
| MAJOR    | `find_pointer_for_surface` casts `struct wl_surface*` to `struct wl_resource*` | `find_pointer_for_surface()`   | `wl_surface` is opaque; casting to `wl_resource*` is implementation-defined and fragile.                           |
| MAJOR    | `dispatch_touch_down` uses `keyboard_focus` for touch                          | `dispatch_touch_down()`        | `find_touch_for_surface(input.keyboard_focus)` should use a touch-specific focus surface.                          |
| MAJOR    | `dispatch_set_pointer_focus` missing `wl_pointer.frame`                        | `dispatch_set_pointer_focus()` | Required for wl_pointer version ≥ 5.                                                                               |
| MAJOR    | `lorie_wl_input_dispatch` is never called by compositor                        | Not referenced                 | The event queue is populated by Java/JNI but never drained into the Wayland display thread. Input events are lost. |
| MINOR    | `next_serial()` returns 0 if `input.display` is NULL                           | `next_serial()`                | Should assert or handle gracefully.                                                                                |
| MINOR    | O(n) linear search in destroy callbacks                                        | `pointer_destroy()`, etc.      | Inefficient for many clients.                                                                                      |

### `main.c`

| Severity | Issue                                                  | Line(s)                                         | Evidence                                                                                                                                                                                              |
| -------- | ------------------------------------------------------ | ----------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| BLOCKER  | Undefined struct `lorie_wayland_compositor`            | Entire file                                     | See global blocker above.                                                                                                                                                                             |
| BLOCKER  | `handle_android_events` return type mismatch           | `handle_android_events()`                       | Declared as `void handle_android_events(...)` but cast to `int (*)(int, uint32_t, void*)` for `wl_event_loop_add_fd`. UB.                                                                             |
| CRITICAL | `EVENT_CLIPBOARD_SEND` VLA overflow                    | `handle_android_events()`                       | `char *data = calloc(1, e.clipboardSend.count + 1);` — `count` is uint32_t; overflow if count == 0xFFFFFFFF.                                                                                          |
| CRITICAL | `read()` return values ignored                         | `handle_android_events()`                       | Multiple `read(fd, ...)` calls without checking `== expected_size`.                                                                                                                                   |
| MAJOR    | `CLOCK_REALTIME` used for `pthread_cond_timedwait`     | `lorie_wayland_renderer_thread()`               | Can jump backward/forward; use `CLOCK_MONOTONIC`.                                                                                                                                                     |
| MAJOR    | `parse_arguments` uses `atof()` without error checking | `parse_arguments()`                             | `atof` returns 0.0 on error, indistinguishable from valid "0".                                                                                                                                        |
| MAJOR    | `argv` allocations leaked                              | `Java_com_termux_x11_WaylandEntryPoint_start()` | `calloc()` for each `argv[i]`; never freed.                                                                                                                                                           |
| MAJOR    | `init_environment` socket path mismatch                | `init_environment()`                            | Sets `WAYLAND_DISPLAY=$TMPDIR/wayland/LorieWayland`, but `wl_display_add_socket()` with name `LorieWayland` creates socket at `$XDG_RUNTIME_DIR/LorieWayland` (which is `tmpdir`). Paths don't match. |
| MINOR    | `abort()` / `exit()` override may conflict with libc   | End of file                                     | Redefining standard library functions with internal linkage may cause linker issues.                                                                                                                  |

### `wayland-activity.c`

| Severity | Issue                                                           | Line(s)              | Evidence                                                                                                                                   |
| -------- | --------------------------------------------------------------- | -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| CRITICAL | VLA stack overflow in clipboard handling                        | `wayland_callback()` | `char clipboard[e.clipboardSend.count + 1];` with untrusted `count`.                                                                       |
| CRITICAL | `sizeof(clipboard)` overread                                    | `wayland_callback()` | `read(wayland_conn_fd, clipboard, sizeof(clipboard))` — if `count` is large, VLA is huge; if small due to overflow, overreads socket data. |
| MAJOR    | `sendKeyEvent` array out-of-bounds                              | `sendKeyEvent()`     | `android_to_linux_keycode[key_code]` with no bounds check; `key_code` is `jint` (signed 32-bit), can be negative or ≥ 304.                 |
| MAJOR    | `sendTextEvent` UTF-8 parsing uses unbounded pointer arithmetic | `sendTextEvent()`    | `while (*p)` may run past allocated buffer if `str` contains embedded nulls or if `length` is not respected properly.                      |
| MAJOR    | `startLogcat` fork child doesn't close fds                      | `startLogcat()`      | Leaks `wayland_conn_fd`, Java VM fds, etc. to logcat child.                                                                                |
| MINOR    | `connect_` doesn't validate `fd` is a real socket               | `connect_()`         | Accepts any `jint` without `fcntl(fd, F_GETFD)` or similar validation.                                                                     |

### `xwayland.c`

| Severity | Issue                                                                                       | Line(s)                   | Evidence                                                                                                                      |
| -------- | ------------------------------------------------------------------------------------------- | ------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| CRITICAL | argv corruption (see global)                                                                | `lorie_xwayland_spawn()`  | Same buffer reused for two argv entries.                                                                                      |
| MAJOR    | `execvp` called with const string cast to non-const                                         | Child process in `fork()` | `argv[argc++] = (char*)xwayland_binary;` — if `xwayland_binary` is a string literal, casting away const is UB on write.       |
| MAJOR    | `sigchld_handler` calls `wl_event_loop_add_signal` but may race                             | `lorie_xwayland_launch()` | `sigchld_source` added after fork; if child exits extremely quickly, SIGCHLD may be lost before handler is registered.        |
| MINOR    | `create_lockfile` PID written with `dprintf(fd, "%10d\n", getpid())` but size checked as 11 | `create_lockfile()`       | `dprintf` returns number of bytes written, not including potential `-` sign for negative PID (impossible for PID, but still). |

### `xdg-shell.c`

| Severity | Issue                                                                | Line(s)                         | Evidence                                                                                          |
| -------- | -------------------------------------------------------------------- | ------------------------------- | ------------------------------------------------------------------------------------------------- | ------------ |
| CRITICAL | Recursive `xdg_surface_send_configure`                               | `xdg_surface_send_configure()`  | See global blocker.                                                                               |
| CRITICAL | Wrong type from `wl_resource_get_user_data`                          | `xdg_wm_base_get_xdg_surface()` | Casts to `struct wl_surface*` instead of `struct lorie_surface*`.                                 |
| MAJOR    | `xdg_surface_get_toplevel` sends configure before surface commit     | `xdg_surface_get_toplevel()`    | xdg-shell protocol expects configure after the client has attached and committed.                 |
| MAJOR    | `xdg_surface_get_popup` dereferences `positioner` without NULL check | `xdg_surface_get_popup()`       | `positioner->offset_x` etc. — if `positioner_resource` user_data is NULL, crashes.                |
| MINOR    | `xdg_toplevel_send_configure` wrapper doesn't handle multiple states | `xdg_toplevel_send_configure()` | Only ever adds zero or one state to the `wl_array`; cannot send combined states (e.g., `MAXIMIZED | ACTIVATED`). |

### `linux-dmabuf.c`

| Severity | Issue                                                                     | Line(s)                  | Evidence                                                                                                        |
| -------- | ------------------------------------------------------------------------- | ------------------------ | --------------------------------------------------------------------------------------------------------------- |
| CRITICAL | `wl_list_insert` with raw `wl_resource*`                                  | `linux_dmabuf_bind()`    | See global blocker.                                                                                             |
| MAJOR    | `create_buffer_common` creates dangling `wl_resource`                     | `create_buffer_common()` | For immediate mode, creates `wl_buffer` resource but sets no implementation or user data. Client cannot use it. |
| MAJOR    | `DRM_FORMAT_MOD_INVALID` used without include                             | `linux_dmabuf_bind()`    | `<drm_fourcc.h>` not included; may not be available on Android NDK.                                             |
| MINOR    | `buffer_params_add` allows duplicate plane indices without closing old fd | `buffer_params_add()`    | If same `plane_idx` added twice, old fd is closed, but error path in caller may also close the fd again.        |

### `wl-data-device-manager.c`

| Severity | Issue                                                      | Line(s)                              | Evidence                                                                                                      |
| -------- | ---------------------------------------------------------- | ------------------------------------ | ------------------------------------------------------------------------------------------------------------- |
| CRITICAL | `wl_list_insert` with raw `wl_resource*`                   | `data_device_manager_bind()`         | See global blocker.                                                                                           |
| MAJOR    | `data_source_offer` never stores mime types                | `data_source_offer()`                | `mime_types` list stays empty; `lorie_data_device_send_selection` later offers hardcoded types only.          |
| MAJOR    | `data_offer_receive` closes fd without writing data        | `data_offer_receive()`               | `wl_data_source_send_send()` notifies source, but fd is closed immediately; source has no way to write to it. |
| MAJOR    | `lorie_data_device_send_selection` creates offer with ID 0 | `lorie_data_device_send_selection()` | `wl_resource_create(..., 0)` — ID 0 is invalid in Wayland.                                                    |

### `relative-pointer.c`

| Severity | Issue                                    | Line(s)                                           | Evidence                                                                                                           |
| -------- | ---------------------------------------- | ------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| CRITICAL | `wl_list_insert` with raw `wl_resource*` | `relative_pointer_manager_bind()`                 | See global blocker.                                                                                                |
| CRITICAL | Undefined variable `manager`             | `relative_pointer_manager_get_relative_pointer()` | `wl_list_insert(&manager->relative_pointers, ...)` — `manager` is not in scope; should be from resource user data. |

### `pointer-constraints.c`

| Severity | Issue                                                                                | Line(s)                              | Evidence                              |
| -------- | ------------------------------------------------------------------------------------ | ------------------------------------ | ------------------------------------- |
| CRITICAL | `wl_list_insert` with raw `wl_resource*`                                             | `pointer_constraints_bind()`         | See global blocker.                   |
| MAJOR    | `pointer_constraints_lock_pointer` stores `struct wl_surface*` from `lorie_surface*` | `pointer_constraints_lock_pointer()` | Same wrong-cast pattern as xdg-shell. |

### `CMakeLists.txt` (lorie-wayland)

| Severity | Issue                                                          | Line(s)                      | Evidence                                                                                                                                |
| -------- | -------------------------------------------------------------- | ---------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| BLOCKER  | `lorie-wayland` static library is not linked into any target   | Entire file                  | Creates `add_library(lorie-wayland STATIC ...)` but parent `CMakeLists.txt` never adds it to `Xlorie` or any shared library. Dead code. |
| MAJOR    | Include paths assume directory layout that may not exist       | `target_include_directories` | `../../wayland/wayland/src`, etc. — assumes wayland submodule at exact relative path.                                                   |
| MAJOR    | Missing `mediandk` link                                        | `target_link_libraries`      | `renderer.c` uses `AImageReader` but `mediandk` is not linked.                                                                          |
| MINOR    | `wayland-protocols-generated` object library linked as library | `target_link_libraries`      | Technically valid in modern CMake, but unusual.                                                                                         |

### `wayland.cmake`

| Severity | Issue                                                                       | Line(s)                 | Evidence                                                                                                     |
| -------- | --------------------------------------------------------------------------- | ----------------------- | ------------------------------------------------------------------------------------------------------------ |
| MAJOR    | `target_apply_patch` uses `patch` command without CMake dependency tracking | `target_apply_patch`    | Patches are applied at configure time with `execute_process`; no re-configure trigger if patch file changes. |
| MAJOR    | Patch file has fake hashes                                                  | `wayland-android.patch` | `1234567..abcdefg` — will not apply to a real wayland source tree.                                           |
| MINOR    | Hardcoded `HAVE_MEMFD_CREATE 1` for Android                                 | `wayland-config.h`      | Android NDK may not have `memfd_create`; fallback in patch exists but config contradicts it.                 |

### `wayland-protocols.cmake`

| Severity | Issue                                                                  | Line(s)                     | Evidence                                                                                                                                                                     |
| -------- | ---------------------------------------------------------------------- | --------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| MAJOR    | `check_type_size` called without `include(CheckTypeSize)` in this file | `check_type_size`           | Relies on parent scope; fragile if included from different context.                                                                                                          |
| MINOR    | `PARENT_SCOPE` variables may be empty at `add_library` time            | `wayland_protocol_generate` | CMake scoping is subtle; if `PARENT_SCOPE` sets variable in parent directory instead of current file scope, `WAYLAND_PROTOCOL_SOURCES` may be empty when `add_library` runs. |

### Java Files

#### `WaylandEntryPoint.java`

| Severity | Issue                                                              | Line(s)           | Evidence                                                                                                                                                                                                                                                |
| -------- | ------------------------------------------------------------------ | ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| BLOCKER  | Native library never loaded in this class                          | `static {}` block | `System.loadLibrary("Xlorie")` is in `LorieWaylandView`, not here. Calling `start()`, `stop()`, `connected()` will throw `UnsatisfiedLinkError` unless `LorieWaylandView` is loaded first.                                                              |
| MAJOR    | `getWaylandConnection` declared as instance native but used as ... | Class body        | Actually declared as `public native ParcelFileDescriptor getWaylandConnection();` (instance method), but `main.c` implements it as `Java_com_termux_x11_WaylandEntryPoint_getWaylandConnection`. JNI signature mismatch if called on wrong object type. |

#### `LorieWaylandView.java`

| Severity | Issue                                                        | Line(s)                           | Evidence                                                                                                                                                  |
| -------- | ------------------------------------------------------------ | --------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| MAJOR    | `System.loadLibrary("Xlorie")` conflicts with X11 path       | `static {}`                       | If both X11 and Wayland native methods are in `libXlorie.so`, `JNI_OnLoad` from `wayland-activity.c` will override X11's `JNI_OnLoad`, breaking X11 mode. |
| MINOR    | `sendKeyEvent` overload calls 4-arg version with `0` for `a` | `sendKeyEvent(int, int, boolean)` | Parameter `a` is unused; confusing API.                                                                                                                   |

#### `WaylandActivity.java`

| Severity | Issue                                                             | Line(s)                     | Evidence                                                                                         |
| -------- | ----------------------------------------------------------------- | --------------------------- | ------------------------------------------------------------------------------------------------ |
| MAJOR    | References `R.layout.wayland_activity`                            | `onCreate()`                | This layout resource must exist; if not, build fails. Not reviewed here but noted as dependency. |
| MINOR    | `receiver` registered without `unregisterReceiver` in `onDestroy` | `onCreate()`, `onDestroy()` | Actually `onDestroy` does call `unregisterReceiver(receiver)`. OK.                               |

---

## Missing Test Coverage

For every component, the following tests would be required before this code is safe:

### Compositor Core (`compositor.c`, `surface.c`, `output.c`, `seat.c`)

- **Lifecycle test:** Init → start → create a client → create surface → attach buffer → commit → stop. Verify no leaks (valgrind / ASan).
- **Multi-client stress:** 10 clients creating/destroying surfaces concurrently. Verify mutex behavior and no double-free.
- **Buffer protocol compliance:** Client attaches buffer, commits, then reuses buffer only after `wl_buffer.release` is received. Verify compositor sends release.
- **Empty list safety:** Call `lorie_wayland_set_window()` before any output exists. Verify no crash.
- **Focus serial validity:** Verify all `enter`/`leave` events use non-zero serials from `wl_display_next_serial()`.

### Renderer (`renderer.c`)

- **Thread-safety test:** Call `set_window()` from thread A while `commit()` runs in thread B. Verify no EGL crash or deadlock.
- **Resource cleanup:** Add surface → remove surface → `fini()`. Verify no `LorieBuffer` leaks and no GL texture leaks.
- **Callback reentrancy:** Frame callback that calls `remove_surface()` during `commit()`. Verify no deadlock.
- **AImageReader leak:** Init → fini → init → fini. Verify no AImageReader objects leaked (check NDK heap trackers).

### Input (`input.c`)

- **Queue overflow:** Push 300 events from Android thread, drain from Wayland thread. Verify oldest events are dropped gracefully.
- **Dispatch integration:** Verify `lorie_wl_input_dispatch()` is actually called from the Wayland event loop and events reach clients.
- **Focus safety:** Rapid focus changes between surfaces while events are queued. Verify no use-after-free.

### XWayland (`xwayland.c`)

- **Socket binding:** Initialize XWayland on displays 0–10. Verify lock files, sockets, and cleanup.
- **Lazy start:** Connect an X11 client before XWayland is running. Verify XWayland launches and client connects.
- **Argv inspection:** Launch XWayland and inspect `/proc/<pid>/cmdline`. Verify `-listenfd` and fd numbers are correct pairs.
- **Crash recovery:** Kill XWayland process. Verify SIGCHLD handler runs, sockets are cleaned up, and lazy mode can restart.

### Protocols (`xdg-shell.c`, `linux-dmabuf.c`, etc.)

- **xdg-shell role conflict:** Create `xdg_surface`, get `toplevel`, then try to get `popup`. Verify protocol error is sent.
- **dmabuf import:** Client creates dmabuf buffer, attaches to surface. Verify compositor imports it (or gracefully fails) without fd leak.
- **wl_data_device round-trip:** Set selection → receive selection → read from fd. Verify data actually flows through the fd.

### JNI / Java (`wayland-activity.c`, `main.c`, Java)

- **JNI signature matching:** Run `javah` or `-Xcheck:jni` against all native methods. Verify no signature mismatches.
- **Clipboard fuzz:** Send `EVENT_CLIPBOARD_SEND` with `count = 0`, `count = MAX_UINT32`, `count = 1`. Verify no crash, no stack overflow.
- **Socket fd validation:** Pass invalid fd to `connect_()`. Verify graceful failure.
- **Library coexistence:** Load `libXlorie.so` with both X11 and Wayland symbols. Verify both `JNI_OnLoad` paths don't stomp each other.

### Build System

- **Clean build from checkout:** `rm -rf build && cmake ... && make`. Verify no missing generated headers, no patch failures, and all targets link.
- **Cross-compile check:** Build for `arm64-v8a`. Verify `wayland-scanner` runs on host, not target.

---

## Summary Table

| Category                                                    | Count |
| ----------------------------------------------------------- | ----- |
| BLOCKER (won't compile / architectural impossibility)       | 5     |
| CRITICAL (crash, security vulnerability, memory corruption) | 18    |
| MAJOR (protocol violation, leak, race, API misuse)          | 42    |
| MINOR (style, performance, maintainability, portability)    | 18    |

**Total issues found: 83+**

**Recommendations:**

1. **Halt merge.** Do not land this in `main`.
2. **Fix blockers first:** Define the compositor struct, resolve the Java/renderer disconnect, deduplicate headers, and fix the duplicate `wl_seat`.
3. **Audit every `wl_list_insert` call** for proper node wrapping.
4. **Replace all VLA clipboard buffers** with heap allocation + size caps.
5. **Run the code through an actual Android NDK build** with `-Wall -Werror` and AddressSanitizer before any further review round.
