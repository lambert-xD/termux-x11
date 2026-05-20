# Tasks: Native Wayland Compositor for Android

## Review Workload Forecast

| Metric            | Value                                     |
| ----------------- | ----------------------------------------- |
| Total new lines   | ~8,372                                    |
| Modified files    | 4                                         |
| Risk level        | **HIGH** — exceeds 400-line budget by 20× |
| Delivery strategy | **Chained PRs (Feature Branch Chain)**    |

## Chained PR Plan

### Tracker PR

**Branch:** `feature/wayland-compositor`  
**Purpose:** Draft tracker. Never merged directly. All child PRs target this branch or previous child.

### PR Chain

```
Tracker: feature/wayland-compositor
    │
    ├── PR #1: Build System + Submodules  ←  📍 START HERE
    │   Target: feature/wayland-compositor
    │   ~350 lines
    │
    ├── PR #2: Core Compositor + Output
    │   Target: PR #1 branch
    │   Depends: PR #1
    │   ~380 lines
    │
    ├── PR #3: Surface Management + Renderer
    │   Target: PR #2 branch
    │   Depends: PR #2
    │   ~400 lines
    │
    ├── PR #4: Input + Seat
    │   Target: PR #3 branch
    │   Depends: PR #3
    │   ~370 lines
    │
    ├── PR #5: Protocols (xdg-shell + linux-dmabuf)
    │   Target: PR #4 branch
    │   Depends: PR #4
    │   ~390 lines
    │
    ├── PR #6: XWayland Integration
    │   Target: PR #5 branch
    │   Depends: PR #5
    │   ~200 lines
    │
    ├── PR #7: Java Layer + Android Manifest
    │   Target: PR #6 branch
    │   Depends: PR #6
    │   ~380 lines
    │
    └── PR #8: Tests + Documentation
        Target: PR #7 branch
        Depends: PR #7
        ~250 lines
```

---

## Work Unit Details

### PR #1: Build System + Wayland Submodules

**Files:**

- `.gitmodules` (wayland/\* entries)
- `app/src/main/cpp/CMakeLists.txt` (+wayland, +wayland-protocols)
- `app/src/main/cpp/recipes/wayland.cmake`
- `app/src/main/cpp/recipes/wayland-protocols.cmake`
- `app/src/main/cpp/patches/wayland-android.patch`

**Acceptance Criteria:**

- [ ] `wayland/wayland` submodule builds as `libwayland-server`
- [ ] `wayland/wayland-protocols` generates C headers from XML
- [ ] CMake configures without errors for all ABIs
- [ ] Existing X11 build still works

**Tests:**

- CMake configuration test: `cmake -B build` succeeds
- Verify `libwayland-server.a` is produced in build output

---

### PR #2: Core Compositor + Output

**Files:**

- `app/src/main/cpp/lorie-wayland/compositor.c`
- `app/src/main/cpp/lorie-wayland/compositor.h`
- `app/src/main/cpp/lorie-wayland/output.c`
- `app/src/main/cpp/lorie-wayland/wayland-lorie.h`
- `app/src/main/cpp/lorie-wayland/main.c` (entry point only)

**Acceptance Criteria:**

- [ ] `wl_display` creates and listens on socket
- [ ] `wl_compositor` global is advertised
- [ ] `wl_output` reports correct screen dimensions
- [ ] A Wayland client can connect successfully

**Tests:**

- Unit test: `test_compositor_create()` — display initializes
- Integration test: connect a minimal client, verify globals list

---

### PR #3: Surface Management + Renderer

**Files:**

- `app/src/main/cpp/lorie-wayland/surface.c`
- `app/src/main/cpp/lorie-wayland/renderer.c`
- `app/src/main/cpp/lorie-wayland/renderer.h`

**Acceptance Criteria:**

- [ ] `wl_surface` can attach a `wl_buffer` (SHM)
- [ ] Renderer composites at least one surface to output
- [ ] Damage tracking only redraws changed regions
- [ ] Frame callbacks are sent at vsync

**Tests:**

- Unit test: `test_surface_attach_buffer()`
- Unit test: `test_renderer_composite_single()`
- Visual test: render a solid color surface, verify pixel output

---

### PR #4: Input + Seat

**Files:**

- `app/src/main/cpp/lorie-wayland/input.c`
- `app/src/main/cpp/lorie-wayland/input.h`
- `app/src/main/cpp/lorie-wayland/seat.c`

**Acceptance Criteria:**

- [ ] `wl_seat` advertises pointer, keyboard, touch capabilities
- [ ] Touch events produce `wl_touch.down/up/motion`
- [ ] Keyboard events produce `wl_keyboard.key`
- [ ] Pointer motion updates focused surface

**Tests:**

- Unit test: `test_seat_create()`
- Unit test: `test_input_touch_down()`
- Unit test: `test_input_keyboard_key()`

---

### PR #5: Protocols (xdg-shell + linux-dmabuf)

**Files:**

- `app/src/main/cpp/lorie-wayland/protocols/xdg-shell.c`
- `app/src/main/cpp/lorie-wayland/protocols/xdg-shell.h`
- `app/src/main/cpp/lorie-wayland/protocols/linux-dmabuf.c`
- `app/src/main/cpp/lorie-wayland/protocols/linux-dmabuf.h`
- `app/src/main/cpp/lorie-wayland/protocols/wl-data-device-manager.c`
- `app/src/main/cpp/lorie-wayland/protocols/wl-data-device-manager.h`

**Acceptance Criteria:**

- [ ] `xdg_wm_base` global is advertised
- [ ] `xdg_toplevel` configure/ack_configure cycle works
- [ ] `zwp_linux_dmabuf_v1` can import AHardwareBuffer
- [ ] `wl_data_device_manager` supports basic clipboard

**Tests:**

- Protocol test: create xdg_surface, verify configure event
- Protocol test: import dmabuf, verify buffer creation

---

### PR #6: XWayland Integration

**Files:**

- `app/src/main/cpp/lorie-wayland/xwayland.c`
- `app/src/main/cpp/lorie-wayland/xwayland.h`

**Acceptance Criteria:**

- [ ] XWayland process launches when X11 client connects
- [ ] X11 window appears as a Wayland surface
- [ ] XWayland shutdown is clean (no zombie processes)

**Tests:**

- Integration test: launch XWayland, verify it creates a wl_surface
- Integration test: run `xeyes` or similar, verify rendering

---

### PR #7: Java Layer + Android Manifest

**Files:**

- `app/src/main/java/com/termux/x11/WaylandActivity.java`
- `app/src/main/java/com/termux/x11/LorieWaylandView.java`
- `app/src/main/java/com/termux/x11/WaylandCmdEntryPoint.java`
- `app/src/main/java/com/termux/x11/WaylandEntryPoint.java`
- `app/src/main/res/layout/wayland_activity.xml`
- `app/src/main/AndroidManifest.xml`
- `app/src/main/cpp/lorie-wayland/wayland-activity.c`

**Acceptance Criteria:**

- [ ] WaylandActivity launches without crash
- [ ] LorieWaylandView creates ANativeWindow and passes to compositor
- [ ] Input events from Android reach native compositor
- [ ] App icon launches Wayland mode

**Tests:**

- Instrumentation test: launch WaylandActivity, verify no crash
- UI test: tap surface, verify event reaches native layer

---

### PR #8: Tests + Documentation

**Files:**

- `app/src/main/cpp/lorie-wayland/tests/` (new directory)
- `openspec/wayland-compositor/` (artifacts)
- `README.md` (Wayland section)

**Acceptance Criteria:**

- [ ] Unit tests exist for compositor, surface, renderer, input
- [ ] All tests pass on CI
- [ ] SDD artifacts are complete (proposal, spec, design, tasks)
- [ ] README documents how to launch Wayland mode

**Tests:**

- Full test suite run
- CI build verification for all ABIs

---

## Test Framework

Since the project uses CMake + CTest, add a minimal test framework:

```
app/src/main/cpp/lorie-wayland/tests/
├── test_main.c
├── test_compositor.c
├── test_surface.c
├── test_renderer.c
├── test_input.c
└── CMakeLists.txt
```

Use simple assert-based tests (no external dependency) to keep build simple.

## TDD Evidence Requirement

For each PR that touches native code:

1. **RED**: Write failing test first
2. **GREEN**: Implement minimal code to pass
3. **TRIANGULATE**: Add more test cases
4. **REFACTOR**: Clean up with tests green

Record evidence in PR description:

```
## TDD Evidence
- test_compositor_create: RED → GREEN (commit abc123)
- test_surface_attach_buffer: RED → GREEN (commit def456)
- test_input_touch_down: RED → GREEN (commit ghi789)
```
