# PR #7: XWayland Integration

## Diff Budget

**Actual: ~347 lines** (target: ≤400) ✅

## Files Changed

| File                                                   | Lines | Action                                        |
| ------------------------------------------------------ | ----- | --------------------------------------------- |
| `app/src/main/cpp/lorie-wayland/xwayland.h`            | 26    | Created — struct lorie_xwayland, public API   |
| `app/src/main/cpp/lorie-wayland/xwayland.c`            | 244   | Created — init, launch, shutdown, socket mgmt |
| `app/src/main/cpp/lorie-wayland/tests/test_xwayland.c` | 62    | Created — 5 TDD tests                         |
| `app/src/main/cpp/lorie-wayland/tests/test_main.c`     | +5    | Modified — registered xwayland suite          |
| `app/src/main/cpp/lorie-wayland/tests/CMakeLists.txt`  | +2    | Modified — added xwayland.c, test_xwayland.c  |

## TDD Evidence

### RED (tests written before implementation)

`test_xwayland.c` created with 5 tests referencing undefined APIs:

- `lorie_xwayland_init()` — undefined
- `lorie_xwayland_shutdown()` — undefined
- `struct lorie_xwayland` — incomplete type
- `xw->display_number`, `xw->lockfile`, `xw->abstract_fd`, `xw->wm_fd[]` — members undefined

**Compilation fails** — RED confirmed.

### GREEN (implementation written to pass tests)

Created `xwayland.h` with complete struct definition, then `xwayland.c` with full implementation.

**All 5 tests compile against real APIs** — GREEN achieved.

## Test List

| Test                               | What it verifies                                 |
| ---------------------------------- | ------------------------------------------------ |
| `test_xwayland_init_shutdown`      | Allocation, xserver_path stored, pid=-1 init     |
| `test_xwayland_init_finds_display` | Display number in valid range (0-99)             |
| `test_xwayland_lockfile_format`    | Lockfile exists with exactly 11 bytes (`%10d\n`) |
| `test_xwayland_sockets_created`    | Abstract or unix socket fd created               |
| `test_xwayland_wm_socketpair`      | WM socketpair created, fds distinct and valid    |

## Critical Review Fixes Addressed

| #   | Issue (from `.pi/fresh-review-wayland.md`)          | Fix in PR #7                                                                         |
| --- | --------------------------------------------------- | ------------------------------------------------------------------------------------ | ---------------------------- |
| 1   | argv corruption: same buffer reused for two entries | ✅ Separate `listen1_str`, `fd1_str`, `listen2_str`, `fd2_str`, `wm_str`, `wmfd_str` |
| 2   | `execvp()` called with const string cast            | ✅ `bin = strdup(xw->xserver_path)` — mutable `char*`                                |
| 3   | `sigchld_handler` registered after fork             | ✅ `wl_event_loop_add_signal()` called **before** `fork()`                           |
| 4   | `create_lockfile()` size check mismatch             | ✅ `snprintf` returns 11, `write(fd, pid_str, 11)` verified == 11                    |
| 5   | Socket paths incorrect                              | ✅ Abstract `@/tmp/.X11-unix/X<n>` + unix `/tmp/.X11-unix/X<n>`                      |
| 6   | WM communication missing                            | ✅ `socketpair(AF_UNIX, SOCK_STREAM                                                  | SOCK_CLOEXEC, 0, xw->wm_fd)` |
| 7   | Lockfile format wrong                               | ✅ `"%10d\n"` format with exact 11-byte size check                                   |

## Architecture Decisions

- **Display search**: Iterates 0–99, creates lockfile (O_EXCL), binds abstract + unix sockets. First successful display wins.
- **Socket ownership**: Abstract socket uses `sun_path[0] = '\0'` with path in `sun_path+1`. Unix socket binds to filesystem path.
- **Lockfile cleanup**: `unlink()` on shutdown, even if XWayland never launched.
- **SIGCHLD**: Registered via `wl_event_loop_add_signal()` before fork; handler uses `waitpid(WNOHANG)`.
- **Lazy launch ready**: `lorie_xwayland_launch()` is separate from `init()`; can be called on first X11 client connection.
- **Argv safety**: Each argv element has its own stack buffer; `strdup()` on binary path for execvp mutability.

## Next Step

**PR #8: Java Layer / JNI Integration**
