# Renderer Transforms + Viewporter Specification

## Purpose

Implement `wl_surface.set_buffer_transform`, `wl_surface.set_buffer_scale`, and the `wp_viewporter` protocol so that Wayland clients can rotate, scale, and crop their buffers. The renderer must apply these transforms via a vertex-shader matrix uniform.

## Requirements

### Requirement: Buffer Transform Matrix

The system MUST generate a 4×4 transform matrix that encodes all 8 `WL_OUTPUT_TRANSFORM` values (0°/90°/180°/270° plus horizontal/vertical flip variants) and apply it to the vertex shader.

#### Scenario: 90° rotation

- GIVEN a surface with `buffer_transform = WL_OUTPUT_TRANSFORM_90`
- WHEN the renderer draws the surface
- THEN the quad is rotated 90° clockwise

#### Scenario: Horizontal flip

- GIVEN a surface with `buffer_transform = WL_OUTPUT_TRANSFORM_FLIPPED`
- WHEN the renderer draws the surface
- THEN the quad is mirrored horizontally

#### Scenario: RED test — transform ignored

- GIVEN `buffer_transform` is stored but not applied
- WHEN a unit test queries the transform matrix
- THEN the matrix is identity (RED phase confirms ignored before GREEN)

### Requirement: Buffer Scale

The system MUST multiply the surface logical size by `buffer_scale` so that HiDPI buffers render at the correct pixel density.

#### Scenario: 2× scale

- GIVEN a surface with `buffer_scale = 2` and buffer size 200×100
- WHEN the renderer computes logical size
- THEN the logical size is 100×50

### Requirement: Viewporter Protocol

The system MUST implement `wp_viewporter` version 1 with `wp_viewport.set_source` and `wp_viewport.set_destination`.

#### Scenario: Crop source rectangle

- GIVEN a viewport with `set_source(10, 20, 100, 50)`
- WHEN the surface is committed
- THEN only the sub-rectangle `(10, 20, 100, 50)` of the buffer is mapped to the texture

#### Scenario: Destination size override

- GIVEN a viewport with `set_destination(640, 480)`
- WHEN the surface is committed
- THEN the surface quad renders at 640×480 logical pixels regardless of buffer size

#### Scenario: Bad value error

- GIVEN a viewport with negative source width
- WHEN `set_source(-1, 0, 100, 50)` is called
- THEN the compositor sends `WP_VIEWPORT_ERROR_BAD_VALUE` and destroys the viewport

### Requirement: Damage Tracking Compatibility

Transforms and scales MUST NOT break the damage-tracking system from PR #2. Damage rectangles in surface-local coordinates must be correctly transformed to screen coordinates before scissoring.

## Test Plan

| Test | Location | Type | What It Verifies |
|------|----------|------|-----------------|
| `test_transform_identity` | `test_transform.c` (new) | Unit | No transform = identity matrix |
| `test_transform_90` | `test_transform.c` (new) | Unit | 90° rotation matrix correct |
| `test_transform_180` | `test_transform.c` (new) | Unit | 180° rotation matrix correct |
| `test_transform_flipped` | `test_transform.c` (new) | Unit | Horizontal flip matrix correct |
| `test_scale_2x` | `test_transform.c` (new) | Unit | Logical size halved at 2× scale |
| `test_viewporter_create` | `test_viewporter.c` (new) | Unit | Viewporter global exists |
| `test_viewporter_set_source` | `test_viewporter.c` (new) | Unit | Source rect stored on surface |
| `test_viewporter_set_destination` | `test_viewporter.c` (new) | Unit | Destination size stored |
| `test_viewporter_bad_value` | `test_viewporter.c` (new) | Unit | Negative width triggers error |

## Acceptance Criteria

1. All 8 `WL_OUTPUT_TRANSFORM` values produce correct vertex positions.
2. `buffer_scale` divides logical width/height (integer scale).
3. `wp_viewporter` global is advertised to clients.
4. `set_source` / `set_destination` affect rendering.
5. `WP_VIEWPORT_ERROR_BAD_VALUE` is sent for invalid rectangles.
6. Damage tracking continues to work correctly with transformed surfaces.
7. APK builds and native test suite compiles.
