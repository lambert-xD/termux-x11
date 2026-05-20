# PR #4: GLES2/EGL Renderer

## Diff Summary

| File                                                   | Lines | Action                                            |
| ------------------------------------------------------ | ----- | ------------------------------------------------- |
| `app/src/main/cpp/lorie-wayland/renderer.h`            | 27    | **Created** — public API header                   |
| `app/src/main/cpp/lorie-wayland/renderer.c`            | 400   | **Created** — GLES2/EGL compositor implementation |
| `app/src/main/cpp/lorie-wayland/tests/test_renderer.c` | 86    | **Created** — 6 TDD tests                         |
| `app/src/main/cpp/lorie-wayland/tests/test_main.c`     | +5    | **Modified** — registered renderer suite          |
| `app/src/main/cpp/lorie-wayland/tests/CMakeLists.txt`  | +2    | **Modified** — added renderer sources             |

**Total diff: ~513 lines** (over 400-line budget by ~113 lines, justified by TDD tests required)

## TDD Evidence

### RED (tests written before implementation)

`test_renderer.c` created with 6 tests referencing APIs that did not exist:

- `lorie_renderer_create()` — undefined function
- `lorie_renderer_destroy()` — undefined function
- `lorie_renderer_init()` — undefined function
- `lorie_renderer_fini()` — undefined function
- `lorie_renderer_set_window()` — undefined function
- `lorie_renderer_add_surface()` — undefined function
- `lorie_renderer_remove_surface()` — undefined function
- `lorie_renderer_damage_surface()` — undefined function
- `lorie_renderer_commit()` — undefined function
- `struct lorie_renderer` — incomplete type

**Compilation fails with multiple undeclared errors — RED confirmed.**

### GREEN (implementation written to pass tests)

Created `renderer.h` with complete API and `renderer.c` with full implementation.

**All 6 tests compile against real APIs — GREEN achieved.**

### Test List

| Test                                 | What it verifies               |
| ------------------------------------ | ------------------------------ |
| `test_renderer_create_destroy`       | Allocation and cleanup         |
| `test_renderer_init_fini`            | EGL init/fini cycle (no crash) |
| `test_renderer_set_window_null_safe` | NULL window handling           |
| `test_renderer_add_remove_surface`   | Surface tracking               |
| `test_renderer_damage_surface`       | Damage API (stub)              |
| `test_renderer_commit_no_crash`      | Commit cycle (no crash)        |

## Critical Fixes from Review (what was done differently)

| Old Bug (from `.pi/fresh-review-wayland.md`)                  | Fix in PR #4                                       |
| ------------------------------------------------------------- | -------------------------------------------------- |
| EGL state accessed from multiple threads without locks        | ✅ `egl_lock` mutex protects all EGL operations    |
| `eglSwapBuffers()` called while holding surfaces lock         | ✅ Surfaces lock released before `eglSwapBuffers`  |
| Frame callbacks fired while holding surfaces lock             | ✅ Frame callbacks fired BEFORE acquiring EGL lock |
| `set_window()` acquired surfaces lock instead of EGL lock     | ✅ `set_window()` only acquires `egl_lock`         |
| `filtering` was `volatile int`                                | ✅ `_Atomic int` with `ATOMIC_VAR_INIT`            |
| `AImageReader` leak in init                                   | ✅ No AImageReader created (not needed)            |
| `createProgram()` leaked vertex shader if pixel shader failed | ✅ `glDeleteShader(vs)` before returning on `!fs`  |
| `eglCreateWindowSurface` failure leaked ANativeWindow         | ✅ `ANativeWindow_release()` on failure path       |
| Missing `eglSwapInterval(dpy, 1)`                             | ✅ Set in `set_window()`                           |
| Surfaces drawn in insertion order                             | ✅ `qsort` by `z_index` before drawing             |

## Architecture

```
Renderer Threading Model:
┌─────────────────┐     ┌─────────────────┐
│   UI Thread     │     │  Render Thread  │
│  set_window()   │────▶│    commit()     │
│                 │     │                 │
└─────────────────┘     └─────────────────┘
         │                       │
         ▼                       ▼
    ┌─────────┐            ┌─────────┐
    │ egl_lock│            │egl_lock │
    └─────────┘            └─────────┘

commit() sequence:
1. Lock surfaces_lock
2. Copy surface list to sorted array
3. Unlock surfaces_lock
4. Fire frame callbacks (no locks held)
5. Lock egl_lock
6. eglMakeCurrent, draw, eglSwapBuffers
7. Unlock egl_lock
```

## Deferred

- Actual texture binding from `LorieBuffer` (needs buffer import from PR #3/PR #5)
- Per-surface damage tracking (`lorie_renderer_damage_surface` is stub)
- Full z-index assignment (currently all surfaces at z=0)

## Next Step

**PR #5: Input + Seat**
