# DMA-BUF Import Specification

## Purpose

Complete the `linux-dmabuf` protocol implementation so that Wayland clients can share GPU buffers via DMA-BUF fd passing, eliminating the CPU copy path required by `wl_shm`. The renderer must detect dmabuf-backed buffers and bind their imported textures.

## Requirements

### Requirement: Parameter Validation

The system MUST validate plane count, format, width, and height in `params_create` before attempting import.

#### Scenario: Valid ABGR8888 single-plane params

- GIVEN a client provides one plane with `format=DRM_FORMAT_ABGR8888`, `width=100`, `height=100`
- WHEN `params_create` is called
- THEN import proceeds and a `wl_buffer` resource is created

#### Scenario: Unsupported format rejected

- GIVEN a client provides `format=DRM_FORMAT_NV12`
- WHEN `params_create` is called
- THEN `wl_resource_post_error` is invoked with `ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT` and FDs are closed

#### Scenario: Zero dimensions rejected

- GIVEN a client provides `width=0` or `height=0`
- WHEN `params_create` is called
- THEN `wl_resource_post_error` is invoked and FDs are closed

#### Scenario: RED test — params_create is stub

- GIVEN `params_create` closes all FDs without validation or import
- WHEN a unit test calls `lorie_dmabuf_params_create()` with valid params
- THEN no `wl_buffer` user data is set (RED phase confirms stub before GREEN)

### Requirement: EGLImage Import

The system MUST import validated dmabuf FDs via `eglCreateImageKHR` using `EGL_EXT_image_dma_buf_import`, then create a GL texture from the `EGLImage`.

#### Scenario: Successful import creates texture

- GIVEN valid dmabuf params with `DRM_FORMAT_ABGR8888`
- WHEN `eglCreateImageKHR` succeeds
- THEN a `GLuint` texture ID is generated and stored in `lorie_dmabuf_buffer` attached to the `wl_buffer`

#### Scenario: Failed import posts error

- GIVEN dmabuf params that `eglCreateImageKHR` rejects
- WHEN import fails
- THEN `wl_resource_post_error` is invoked and FDs are closed; no memory leaks

#### Scenario: RED test — no EGLImage path

- GIVEN `eglCreateImageKHR` is never called
- WHEN `params_create` runs
- THEN no texture is created (RED phase confirms missing import path)

### Requirement: Renderer Texture Binding

The system MUST, in `lorie_renderer_commit()`, detect dmabuf-backed buffers and bind their imported texture instead of falling through to the shm path.

#### Scenario: Dmabuf buffer bound in commit

- GIVEN a surface whose `buffer_resource` has a `lorie_dmabuf_buffer` with a valid texture ID
- WHEN `lorie_renderer_commit()` draws the surface
- THEN `glBindTexture(GL_TEXTURE_2D, dmabuf_tex_id)` is called before `glDrawArrays`

#### Scenario: Shm buffer still works

- GIVEN a surface whose `buffer_resource` has a `wl_shm_buffer`
- WHEN `lorie_renderer_commit()` draws the surface
- THEN the existing `LorieBuffer_attachToGL` / `LorieBuffer_bindTexture` path is used

#### Scenario: RED test — renderer ignores dmabuf

- GIVEN the renderer checks only for `wl_shm_buffer`
- WHEN a dmabuf-backed surface commits
- THEN the surface is drawn as a placeholder (RED phase confirms missing dmabuf path)

### Requirement: AHardwareBuffer Fallback

The system MAY support `EGL_ANDROID_image_native_buffer` for `AHardwareBuffer` import if the extension is available at runtime.

#### Scenario: AHardwareBuffer import when extension present

- GIVEN `EGL_ANDROID_image_native_buffer` is in the EGL extension string
- WHEN an `AHardwareBuffer` handle is provided via dmabuf
- THEN the buffer is imported via `eglCreateImageKHR` with `EGL_NATIVE_BUFFER_ANDROID`

## Test Plan

| Test | Location | Type | What It Verifies |
|------|----------|------|------------------|
| `test_dmabuf_validate_abgr8888` | `test_dmabuf.c` (new) | Unit | Valid ABGR8888 params pass validation |
| `test_dmabuf_reject_nv12` | `test_dmabuf.c` (new) | Unit | Unsupported format is rejected with error |
| `test_dmabuf_reject_zero_size` | `test_dmabuf.c` (new) | Unit | Zero dimensions are rejected |
| `test_dmabuf_import_success` | `test_dmabuf.c` (new) | Unit | Valid params create texture (mock EGL) |
| `test_dmabuf_import_failure` | `test_dmabuf.c` (new) | Unit | Failed import posts error and cleans up FDs |
| `test_dmabuf_renderer_binds_texture` | `test_dmabuf.c` (new) | Unit | Renderer binds dmabuf texture ID |
| `test_dmabuf_shm_fallback` | `test_dmabuf.c` (new) | Unit | Shm buffers continue to work |
| `simple-dmabuf-egl` | Device / emulator | Integration | Client can display dmabuf buffers |

## Acceptance Criteria

1. `test_dmabuf.c` suite passes (RED before implementation, GREEN after).
2. `params_create` validates format, dimensions, and plane count before import.
3. `DRM_FORMAT_ABGR8888` and `DRM_FORMAT_XBGR8888` are supported.
4. Failed imports post a Wayland error and close all FDs without leaking.
5. Successful imports attach a `lorie_dmabuf_buffer` with a valid GL texture ID to the `wl_buffer`.
6. The renderer binds dmabuf textures correctly during commit.
7. Shm buffers continue to work unchanged (no regression).
8. `simple-dmabuf-egl` or equivalent client displays buffers without shm copy.
9. No FD leaks occur in any error path (verified by `/proc/self/fd` inspection in tests).
