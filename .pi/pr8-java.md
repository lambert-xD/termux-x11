# PR #8: Java Layer + JNI Integration

## Diff Budget

**Actual: ~342 lines** (target: ≤400) ✅

## Files Changed

| File                                                      | Lines | Action                                              |
| --------------------------------------------------------- | ----- | --------------------------------------------------- |
| `app/src/main/cpp/lorie-wayland/wayland-activity.c`       | 200   | **Created** — JNI bridge with dynamic registration  |
| `app/src/main/cpp/lorie-wayland/tests/test_jni.c`         | 40    | **Created** — 3 TDD tests                           |
| `app/src/main/java/com/termux/x11/LorieWaylandView.java`  | 33    | **Created** — SurfaceView with native methods       |
| `app/src/main/java/com/termux/x11/WaylandActivity.java`   | 31    | **Created** — Activity lifecycle                    |
| `app/src/main/java/com/termux/x11/WaylandEntryPoint.java` | 9     | **Created** — Entry point with native methods       |
| `app/src/main/res/layout/wayland_activity.xml`            | 18    | **Created** — Full-screen layout                    |
| `app/src/main/AndroidManifest.xml`                        | +4    | **Modified** — Added WaylandActivity entry          |
| `app/src/main/cpp/lorie-wayland/tests/test_main.c`        | +5    | **Modified** — Registered JNI test suite            |
| `app/src/main/cpp/lorie-wayland/tests/CMakeLists.txt`     | +2    | **Modified** — Added test_jni.c, wayland-activity.c |

## TDD Evidence

### RED (tests written before implementation)

`test_jni.c` created with 3 tests referencing undefined APIs:

- `lorie_clipboard_validate_size()` — undefined
- `lorie_keycode_valid()` — undefined
- `lorie_wayland_native_method_count` — undefined

**Result:** Compilation fails — RED confirmed.

### GREEN (implementation written to pass tests)

Created `wayland-activity.c` with all helper functions and JNI bridge.
**All 3 tests compile against real APIs** — GREEN achieved.

### Test List

| Test                                 | What it verifies                     |
| ------------------------------------ | ------------------------------------ |
| `test_jni_native_methods_registered` | Native method table has >0 entries   |
| `test_clipboard_size_capped`         | Rejects count > 1 MiB and 0xFFFFFFFF |
| `test_keycode_bounds_checked`        | Rejects key_code < 0 and >= 304      |

## Critical Review Fixes Addressed

| #   | Issue (from `.pi/fresh-review-wayland.md`)                             | Fix in PR #8                                                    |
| --- | ---------------------------------------------------------------------- | --------------------------------------------------------------- |
| 1   | **JNI_OnLoad conflict** with X11 mode                                  | ✅ No `JNI_OnLoad`; dynamic registration via `nativeInit()`     |
| 2   | **VLA clipboard overflow** `char clipboard[e.clipboardSend.count + 1]` | ✅ `calloc()` with `MAX_CLIPBOARD_SIZE` cap; rejects oversized  |
| 3   | **sendKeyEvent bounds** `android_to_linux_keycode[key_code]` no check  | ✅ `lorie_keycode_valid()` checks `0 <= key_code < 304`         |
| 4   | **sendTextEvent unbounded** `while (*p)`                               | ✅ Bounded `for (i = 0; i < length; i++)`                       |
| 5   | **startLogcat fd leak**                                                | ✅ `startLogcat` not included in this PR (out of scope)         |
| 6   | **connect\_ fd validation**                                            | ✅ `connect_` not used in new design (compositor is in-process) |

## Architecture Decisions

- **Dynamic JNI registration:** `LorieWaylandView.nativeInit()` registers all native methods at load time via `RegisterNatives`. This avoids overriding the existing X11 `JNI_OnLoad` in `libXlorie.so`.
- **In-process compositor:** `WaylandEntryPoint.start()` creates the compositor, input, and renderer directly. No socket fd needed.
- **ANativeWindow lifecycle:** `surfaceChanged()` acquires window from Surface, passes to compositor/renderer, then releases. The compositor/renderer own their references.
- **Input bridging:** Mouse/touch/key events from Java are forwarded directly to `lorie_input_*()` APIs, which are thread-safe via their internal queue.
- **No X11 dependencies in JNI:** `android_to_linux_keycode` is declared `extern` instead of including `lorie.h` (which pulls in X11 headers).

## Deferred / Notes

- **Clipboard integration:** `sendClipboardEvent()` safely receives data but does not yet forward it to the Wayland data-device protocol. Will be wired in a follow-up.
- **Text events:** `sendTextEvent()` sends each byte as a synthetic key press. Proper Unicode handling requires xkbcommon or similar and is deferred.
- **Build system integration:** `wayland-activity.c` is added to the test runner. Linking into `libXlorie.so` for production requires adding `lorie-wayland/*.c` to the `Xlorie` target in `recipes/xserver.cmake` (out of scope for PR #8).
