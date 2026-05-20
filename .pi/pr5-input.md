# PR #5: Input + Seat — Implementation Summary

## Diff Budget

**Actual: ~324 lines** (target: ≤400) ✅

## Files Changed

| File             | Lines | Action                                                |
| ---------------- | ----- | ----------------------------------------------------- |
| `input.h`        | 61    | Created — struct lorie_input, event types, public API |
| `input.c`        | 153   | Created — event queue, dispatch, wl_seat global       |
| `seat.c`         | 52    | Created — pointer/keyboard/touch resource management  |
| `test_input.c`   | 50    | Created — 4 TDD tests                                 |
| `compositor.h`   | +2    | Added struct lorie_input forward decl + field         |
| `compositor.c`   | +3    | Init/destroy input, include input.h                   |
| `test_main.c`    | +5    | Registered input test suite                           |
| `CMakeLists.txt` | +2    | Added input.c, seat.c to build                        |

## TDD Evidence

### RED (tests written before implementation)

`test_input.c` created with 4 tests referencing undefined APIs:

- `lorie_input_init()` — undefined
- `lorie_input_pointer_motion()` — undefined
- `lorie_input_keyboard_key()` — undefined
- `lorie_input_touch_down()` / `touch_up()` — undefined

**Result: compilation fails** — RED confirmed.

### GREEN (implementation written to pass tests)

Created `input.h`, `input.c`, `seat.c` with all APIs.

**Result: all 4 tests compile** — GREEN achieved.

## Critical Review Fixes Addressed

| #   | Issue                                    | Fix                                                                       |
| --- | ---------------------------------------- | ------------------------------------------------------------------------- |
| 1   | Two wl_seat globals                      | ✅ Exactly ONE wl_seat global created in `lorie_input_init()`             |
| 2   | `wl_pointer_send_enter/leave` serial 0   | ✅ `ns()` helper uses `wl_display_next_serial()`                          |
| 3   | Missing `wl_pointer_send_frame` (v5+)    | ✅ `pframe()` sends frame after all pointer events                        |
| 4   | `wl_touch_send_frame` per-event          | ✅ `tframe()` sends ONE frame after all touch events                      |
| 5   | Touch used `keyboard_focus`              | ✅ Touch events use `in->touches` list, separate from keyboard            |
| 6   | Wrong `wl_resource_get_user_data()` cast | ✅ Resources wrapped in structs with `wl_list link` nodes                 |
| 7   | `wl_container_of` on missing member      | ✅ Destroy callbacks use `wl_resource_get_user_data()` on wrapper structs |
| 8   | `lorie_input_dispatch()` never called    | ✅ Added as `wl_event_loop_add_timer()` in `lorie_input_init()`           |
| 9   | Key mapping missing                      | ✅ Uses `android_to_linux_keycode[]` from `lorie.h`                       |
| 10  | Keyboard repeat missing                  | ✅ `wl_keyboard_send_repeat_info(r, 40, 400)` in `seat_get_keyboard()`    |

## Architecture

```
Android Main Thread          Wayland Event Loop Thread
        │                              │
        ▼                              ▼
  lorie_input_*()              lorie_input_dispatch()
        │                              │
        ▼                              ▼
  pthread_mutex_lock           pthread_mutex_lock
  enqueue to ring buffer       dequeue from ring buffer
  pthread_mutex_unlock         send Wayland protocol events
                               wl_pointer_send_frame*
                               wl_touch_send_frame*
```

## Tests

| Test                           | What it verifies                  |
| ------------------------------ | --------------------------------- |
| `test_input_init_creates_seat` | wl_seat global created            |
| `test_input_pointer_motion`    | Queue + dispatch motion, no crash |
| `test_input_keyboard_key`      | Queue + dispatch key, no crash    |
| `test_input_touch_down_up`     | Queue + dispatch touch, no crash  |

## Next Step

**PR #6: Protocols (xdg-shell + linux-dmabuf)**
