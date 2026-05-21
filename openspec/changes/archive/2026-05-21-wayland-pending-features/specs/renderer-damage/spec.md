# Renderer Damage Tracking Specification

## Purpose

Eliminate full-screen `glClear` every frame by implementing per-surface damage tracking. The compositor already accumulates damage rectangles in `pixman_region32_t`; this spec wires that data into the renderer via `glScissor` to redraw only damaged regions.

## Requirements

### Requirement: Damage Accumulation

The system MUST implement `lorie_renderer_damage_surface()` to merge incoming damage rectangles into a per-surface `pixman_region32_t` region.

#### Scenario: Single damage rectangle

- GIVEN a surface with an empty damage region
- WHEN `lorie_renderer_damage_surface(r, s, 10, 20, 100, 50)` is called
- THEN the surface’s damage region contains exactly one rectangle `(10, 20, 100, 50)`

#### Scenario: Multiple damage calls merge

- GIVEN a surface with damage `(0, 0, 50, 50)`
- WHEN `lorie_renderer_damage_surface(r, s, 30, 30, 50, 50)` is called
- THEN the damage region is the union of both rectangles

#### Scenario: RED test — damage_surface is no-op

- GIVEN `lorie_renderer_damage_surface` is a no-op
- WHEN a unit test inspects `s->damage` after calling it
- THEN the region remains empty (RED phase confirms no-op before GREEN)

### Requirement: Scissor-Guided Redraw

The system MUST, during `lorie_renderer_commit()`, compute the bounding box of each surface’s damage region and apply `glScissor` before drawing that surface.

#### Scenario: Damaged surface is redrawn with scissor

- GIVEN a surface with damage `(10, 20, 100, 50)` and a valid buffer
- WHEN `lorie_renderer_commit()` runs
- THEN `glScissor(10, 20, 100, 50)` is called before the surface draw

#### Scenario: Undamaged surface is skipped

- GIVEN a surface with an empty damage region and a valid buffer
- WHEN `lorie_renderer_commit()` runs
- THEN the surface draw call (`glDrawArrays`) is skipped for that surface

#### Scenario: RED test — full clear every frame

- GIVEN damage tracking is not implemented
- WHEN `lorie_renderer_commit()` runs
- THEN `glClear(GL_COLOR_BUFFER_BIT)` clears the entire framebuffer (RED)

### Requirement: Damage Clear After Commit

The system MUST clear a surface’s damage region after it has been successfully redrawn in `lorie_renderer_commit()`.

#### Scenario: Post-commit damage is empty

- GIVEN a surface with damage `(0, 0, 10, 10)`
- WHEN `lorie_renderer_commit()` completes successfully
- THEN `pixman_region32_n_rects(&s->damage) == 0`

### Requirement: First-Frame Fallback

The system MUST perform a full framebuffer clear when no surface has ever been damaged (first frame or all surfaces empty).

#### Scenario: Initial frame clears fully

- GIVEN the renderer has just been initialized with a window
- WHEN the first `lorie_renderer_commit()` runs
- THEN `glClear(GL_COLOR_BUFFER_BIT)` clears the entire framebuffer before any surface draws

## Test Plan

| Test                               | Location                       | Type | What It Verifies                               |
| ---------------------------------- | ------------------------------ | ---- | ---------------------------------------------- |
| `test_damage_accumulates`          | `test_renderer_damage.c` (new) | Unit | Damage rectangles merge into region            |
| `test_damage_empty_skips_draw`     | `test_renderer_damage.c` (new) | Unit | Surface with empty damage is skipped           |
| `test_damage_scissor_applied`      | `test_renderer_damage.c` (new) | Unit | Scissor box matches damage bounding box        |
| `test_damage_cleared_after_commit` | `test_renderer_damage.c` (new) | Unit | Region is empty after commit                   |
| `test_first_frame_full_clear`      | `test_renderer_damage.c` (new) | Unit | First frame still clears fully                 |
| `test_renderer_commit_no_crash`    | `test_renderer.c` (existing)   | Unit | Commit does not crash with null/empty surfaces |

## Acceptance Criteria

1. `test_renderer_damage.c` suite passes (RED before implementation, GREEN after).
2. `lorie_renderer_damage_surface` is no longer a no-op; it populates `pixman_region32_t`.
3. `lorie_renderer_commit` skips surfaces with empty damage.
4. `glScissor` is applied to the bounding box of each surface’s damage region before draw.
5. Damage region is cleared after successful commit.
6. First frame still performs full clear (no black-screen regression).
7. No renderer crashes occur when surfaces have no buffers.
