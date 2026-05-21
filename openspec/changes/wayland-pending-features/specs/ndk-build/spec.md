# NDK Build Specification

## Purpose

Ensure `lorie-wayland/` compiles and links into the main APK using real Android NDK EGL/GLES headers, and that `wl_shm` pool creation works end-to-end. Without this foundation, no GPU-dependent feature can be verified on a real Android device.

## Requirements

### Requirement: Real NDK Headers in Renderer

The system MUST compile `renderer.c` against real Android NDK EGL/GLES headers instead of local typedef stubs.

#### Scenario: Renderer compiles with NDK headers

- GIVEN `renderer.c` contains EGL/GLES type stubs (`typedef void *EGLDisplay`, etc.)
- WHEN the build runs with `#include <EGL/egl.h>` and `#include <GLES2/gl2.h>`
- THEN compilation succeeds without type redefinition errors

#### Scenario: RED test — stubs cause build failure

- GIVEN a test build that defines `LORIE_WAYLAND_TEST_BUILD`
- WHEN stubs are present alongside real headers
- THEN compilation MUST fail (RED phase confirms stubs are removed before GREEN)

### Requirement: SHM Pool Creation

The system MUST implement `shm_create_pool` in `compositor.c` to wrap `mmap` and create a valid `wl_shm_pool`.

#### Scenario: Client creates SHM pool

- GIVEN a Wayland client calls `wl_shm_create_pool(fd, size)`
- WHEN `shm_create_pool` is invoked
- THEN the fd is `mmap`'d, a `wl_shm_pool` resource is created, and the client receives the pool object

#### Scenario: RED test — shm_create_pool is no-op

- GIVEN `shm_create_pool` is a no-op placeholder
- WHEN a unit test calls `lorie_shm_pool_create()` (test helper)
- THEN it returns NULL (RED phase confirms no-op is replaced before GREEN)

### Requirement: APK Linkage

The system MUST link `lorie-wayland/` sources into the main APK build for all Android ABIs (`x86`, `x86_64`, `armeabi-v7a`, `arm64-v8a`).

#### Scenario: APK builds for all ABIs

- GIVEN `build.gradle` references `app/src/main/cpp/CMakeLists.txt`
- WHEN `./gradlew assembleDebug` executes
- THEN the APK contains `liblorie-wayland.so` (or equivalent linked module) for every ABI

#### Scenario: CI build passes

- GIVEN a CI runner with Android SDK and NDK
- WHEN `gradlew assembleDebug` runs
- THEN the build exits with code 0 and artifacts are produced

## Test Plan

| Test                       | Location                 | Type        | What It Verifies                                                    |
| -------------------------- | ------------------------ | ----------- | ------------------------------------------------------------------- |
| `test_ndk_headers_present` | `test_ndk_build.c` (new) | Unit        | `EGL/egl.h` and `GLES2/gl2.h` are includable without stub conflicts |
| `test_shm_pool_create`     | `test_ndk_build.c` (new) | Unit        | `shm_create_pool` creates a valid pool from a temp fd               |
| `test_shm_pool_mmap`       | `test_ndk_build.c` (new) | Unit        | Pool memory is writable and matches requested size                  |
| `./gradlew assembleDebug`  | CI / local               | Integration | APK builds for all ABIs                                             |
| `test_build.c` (existing)  | `tests/`                 | Unit        | Generated protocol headers are present and parseable                |

## Acceptance Criteria

1. `./gradlew assembleDebug` completes successfully for all four ABIs.
2. `test_ndk_build.c` suite passes (RED before implementation, GREEN after).
3. No `#ifdef __ANDROID__` workarounds are needed for the test build; test build uses the same headers as the APK build.
4. Existing X11 mode (`libXlorie.so`) continues to build without regressions.
