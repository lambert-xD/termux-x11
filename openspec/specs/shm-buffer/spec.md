# SHM Buffer Creation Specification

## Purpose
Define the behavior of `shm_pool_create_buffer`, the `struct lorie_shm_buffer` wrapper, pool reference counting, parameter validation, and the surface integration needed to copy SHM pixel data into `LorieBuffer` for the GLES2 renderer pipeline in the Lorie Wayland compositor.

## Scope
This spec covers two stacked PRs:
- **PR 1: Core Buffer Creation** — `shm_pool_create_buffer` implementation, validation, pool refcounting, buffer destroy.
- **PR 2: Surface Integration** — `surface_commit` extraction, release lifecycle, renderer hand-off.

## Non-Goals
- Zero-copy SHM via `LorieBuffer_wrapFileDescriptor`.
- New pixel formats beyond ARGB8888 and XRGB8888.
- `shm_pool_resize` beyond stub/reject behavior.
- DMA-BUF path changes.
- X11 server code changes.

---

## PR 1: Core Buffer Creation
### Requirements
#### Requirement: Buffer Parameter Validation

The system MUST validate all parameters passed to `shm_pool_create_buffer` before creating a `wl_buffer` resource. On validation failure, the system MUST post a `wl_resource_post_error` with the appropriate `wl_shm_error` code and MUST NOT create the resource.

##### Scenario: Valid parameters create buffer

- GIVEN an active `wl_shm_pool` resource backed by a pool of size 4096 bytes
- WHEN the client calls `wl_shm_pool.create_buffer(id=1, offset=0, width=10, height=10, stride=40, format=WL_SHM_FORMAT_ARGB8888)`
- THEN a `wl_buffer` resource is created with ID 1
- AND the backing `struct lorie_shm_buffer` has `offset=0, width=10, height=10, stride=40, format=WL_SHM_FORMAT_ARGB8888, data=pool->data+0`

##### Scenario: Negative offset is rejected

- GIVEN an active `wl_shm_pool` resource
- WHEN the client calls `create_buffer` with `offset < 0`
- THEN `wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_MMAP)` is called
- AND no `wl_buffer` resource is created

##### Scenario: Non-positive width is rejected

- GIVEN an active `wl_shm_pool` resource
- WHEN the client calls `create_buffer` with `width <= 0`
- THEN `wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE)` is called
- AND no `wl_buffer` resource is created

##### Scenario: Non-positive height is rejected

- GIVEN an active `wl_shm_pool` resource
- WHEN the client calls `create_buffer` with `height <= 0`
- THEN `wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE)` is called
- AND no `wl_buffer` resource is created

##### Scenario: Non-positive stride is rejected

- GIVEN an active `wl_shm_pool` resource
- WHEN the client calls `create_buffer` with `stride <= 0`
- THEN `wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE)` is called
- AND no `wl_buffer` resource is created

##### Scenario: Stride smaller than width times bytes-per-pixel is rejected

- GIVEN an active `wl_shm_pool` resource
- WHEN the client calls `create_buffer` with `stride < width * 4`
- THEN `wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE)` is called
- AND no `wl_buffer` resource is created

##### Scenario: Buffer bounds exceeding pool size are rejected

- GIVEN an active `wl_shm_pool` resource of size 100 bytes
- WHEN the client calls `create_buffer` with parameters such that `offset + (height - 1) * stride + width * 4 > pool->size`
- THEN `wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_MMAP)` is called
- AND no `wl_buffer` resource is created

##### Scenario: Unsupported pixel format is rejected

- GIVEN an active `wl_shm_pool` resource
- WHEN the client calls `create_buffer` with a format other than `WL_SHM_FORMAT_ARGB8888` or `WL_SHM_FORMAT_XRGB8888`
- THEN `wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FORMAT)` is called
- AND no `wl_buffer` resource is created

#### Requirement: Pool Reference Counting

The system MUST maintain a reference count on `struct lorie_shm_pool` to ensure the pool memory is not unmapped while buffers created from it are still alive.

##### Scenario: Buffer creation increments pool refcount

- GIVEN an active `wl_shm_pool` with refcount 1
- WHEN `shm_pool_create_buffer` succeeds
- THEN the pool's refcount is incremented to 2

##### Scenario: Pool destroy with live buffers defers munmap

- GIVEN an active `wl_shm_pool` with refcount 2 (one from creation, one from a live buffer)
- WHEN the pool resource is destroyed (client calls `wl_shm_pool.destroy` or disconnects)
- THEN `munmap` is NOT called immediately
- AND the pool remains allocated until the last buffer is destroyed

##### Scenario: Buffer destroy decrements pool refcount

- GIVEN an active `wl_shm_pool` with refcount 2
- WHEN the buffer resource is destroyed
- THEN the pool's refcount is decremented to 1

##### Scenario: Last buffer destroy triggers pool cleanup

- GIVEN a pool with refcount 1 and no live buffers, marked for deferred destroy
- WHEN the last buffer is destroyed
- THEN `munmap(pool->data, pool->size)` is called
- AND the pool struct is freed

#### Requirement: Buffer Destroy Lifecycle

The system MUST implement `buffer_destroy` as the `wl_buffer` resource destruction callback. It MUST release all references held by the buffer and free the `struct lorie_shm_buffer`.

##### Scenario: Buffer destroy frees lorie_shm_buffer

- GIVEN a live `wl_buffer` resource with a `struct lorie_shm_buffer` attached as user data
- WHEN the client calls `wl_buffer.destroy` or the resource is destroyed for any reason
- THEN `buffer_destroy` is invoked
- AND the `struct lorie_shm_buffer` is freed

##### Scenario: Buffer destroy does not crash with NULL pool

- GIVEN a `wl_buffer` resource whose `struct lorie_shm_buffer` has `pool == NULL`
- WHEN the buffer is destroyed
- THEN the operation completes without crash or undefined behavior

#### Requirement: wl_buffer Interface Implementation

The system MUST create `wl_buffer` resources using the standard `wl_buffer_interface` with `buffer_destroy` as the destruction callback.

##### Scenario: wl_buffer resource has correct interface

- GIVEN a successful `shm_pool_create_buffer` call
- THEN the created resource's interface is `wl_buffer_interface`
- AND the resource's user data is the `struct lorie_shm_buffer*`
- AND the resource's destroy callback is `buffer_destroy`

#### Requirement: Pool Creation Initializes Refcount

The system MUST initialize `lorie_shm_pool.refcount` to 1 at creation time.

##### Scenario: New pool has refcount 1

- GIVEN a newly created `struct lorie_shm_pool` via `lorie_shm_pool_create`
- THEN `pool->refcount == 1`

### Acceptance Criteria (PR 1)
| # | Criterion | Measurable Condition |
|---|-----------|----------------------|
| 1 | Valid buffer creation | `shm_pool_create_buffer` with valid params creates a `wl_buffer` resource with correct user data |
| 2 | Negative offset rejection | `offset < 0` posts `WL_SHM_ERROR_INVALID_MMAP` and returns without creating resource |
| 3 | Dimension rejection | `width <= 0` or `height <= 0` posts `WL_SHM_ERROR_INVALID_STRIDE` and returns |
| 4 | Stride rejection | `stride <= 0` or `stride < width * 4` posts `WL_SHM_ERROR_INVALID_STRIDE` and returns |
| 5 | Bounds rejection | `offset + (height - 1) * stride + width * 4 > pool->size` posts `WL_SHM_ERROR_INVALID_MMAP` and returns |
| 6 | Format rejection | Unsupported format posts `WL_SHM_ERROR_INVALID_FORMAT` and returns |
| 7 | Refcount increment | Pool refcount increases by 1 on each successful buffer creation |
| 8 | Deferred pool destroy | Pool resource destroy with live buffers does not call `munmap` |
| 9 | Refcount decrement | Pool refcount decreases by 1 on each buffer destroy |
| 10 | Final cleanup | When refcount reaches 0 after buffer destroy, `munmap` + `free(pool)` occur |
| 11 | Buffer destroy safety | `buffer_destroy` frees `lorie_shm_buffer` without crash even if pool is NULL |
| 12 | wl_buffer interface | Created resource uses `wl_buffer_interface` with `buffer_destroy` callback |
| 13 | Pool init refcount | `lorie_shm_pool_create` sets `refcount = 1` |

### Test Plan (PR 1)
| Test | Validates | Approach |
|------|-----------|----------|
| `test_shm_buffer_create_valid` | Criterion 1, 7, 12 | Create pool, call `shm_pool_create_buffer` with valid params, assert resource != NULL, assert pool refcount == 2 |
| `test_shm_buffer_reject_negative_offset` | Criterion 2 | Call with `offset = -1`, assert error posted, assert no resource created |
| `test_shm_buffer_reject_zero_width` | Criterion 3 | Call with `width = 0`, assert error posted |
| `test_shm_buffer_reject_zero_height` | Criterion 3 | Call with `height = 0`, assert error posted |
| `test_shm_buffer_reject_zero_stride` | Criterion 4 | Call with `stride = 0`, assert error posted |
| `test_shm_buffer_reject_stride_too_small` | Criterion 4 | Call with `stride = width * 4 - 1`, assert error posted |
| `test_shm_buffer_reject_bounds_exceeded` | Criterion 5 | Create small pool (64 bytes), call with params exceeding bounds, assert error posted |
| `test_shm_buffer_reject_invalid_format` | Criterion 6 | Call with `WL_SHM_FORMAT_RGB565`, assert error posted |
| `test_shm_buffer_pool_refcount_on_create` | Criterion 7 | Assert refcount == 1 before, == 2 after create |
| `test_shm_buffer_pool_deferred_destroy` | Criterion 8 | Create buffer, destroy pool resource, assert pool data is still accessible (not unmapped) |
| `test_shm_buffer_destroy_decrements_refcount` | Criterion 9 | Create buffer, destroy buffer, assert refcount == 1 |
| `test_shm_buffer_final_cleanup` | Criterion 10 | Create buffer, destroy pool resource, destroy buffer, assert pool is freed (use-after-free detection via mock or valgrind) |
| `test_shm_buffer_destroy_null_pool_safe` | Criterion 11 | Manually construct `lorie_shm_buffer` with `pool = NULL`, invoke destroy, assert no crash |
| `test_shm_pool_create_sets_refcount` | Criterion 13 | Create pool, assert `refcount == 1` |

### Dependencies (PR 1)
| Dependency | Source | Why |
|------------|--------|-----|
| `struct lorie_shm_pool` | `compositor.h` | Must add `int refcount` field |
| `lorie_shm_pool_create` | `compositor.c` | Must initialize `refcount = 1` |
| `lorie_shm_pool_destroy` | `compositor.c` | Must check refcount before munmap/free |
| `shm_pool_handle_resource_destroy` | `compositor.c` | Must defer destroy when refcount > 1 |
| `test_ndk_build.c` | existing | Existing SHM pool tests must continue to pass |

---

## PR 2: Surface Integration
### Requirements
#### Requirement: Buffer Extraction in surface_commit

The system MUST extract pixel data from an attached `lorie_shm_buffer` during `surface_commit` and copy it into a newly allocated `LorieBuffer` for the renderer pipeline.

##### Scenario: Commit with SHM buffer copies pixels

- GIVEN a `lorie_surface` with `buffer_resource` pointing to a `wl_buffer` backed by a `lorie_shm_buffer` of size 100x100, stride 400, format ARGB8888
- WHEN `surface_commit` is called
- THEN a `LorieBuffer` is allocated with width=100, height=100, format `AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM`
- AND pixel data is copied row-by-row from `shm->data` to `LorieBuffer_description(lb)->data`
- AND `s->buffer` is set to the allocated `LorieBuffer*`
- AND `s->width` and `s->height` are updated to 100

##### Scenario: Commit with null buffer resource skips copy

- GIVEN a `lorie_surface` with `buffer_resource == NULL`
- WHEN `surface_commit` is called
- THEN no `LorieBuffer` is allocated
- AND `s->buffer` remains NULL

##### Scenario: Commit with non-SHM buffer resource is safely ignored

- GIVEN a `lorie_surface` with `buffer_resource` pointing to a resource that is NOT a `lorie_shm_buffer` (e.g., a DMA-BUF buffer)
- WHEN `surface_commit` is called
- THEN `lorie_shm_buffer_from_resource` returns NULL
- AND no `LorieBuffer` is allocated from SHM data
- AND the existing DMA-BUF path (if any) is not disrupted

#### Requirement: Buffer Release Lifecycle on Reattach

The system MUST send `wl_buffer.send_release` on the previously attached buffer when a new buffer is attached, and MUST release the associated `LorieBuffer`.

##### Scenario: Reattach releases old buffer

- GIVEN a `lorie_surface` with `buffer_resource` set to buffer A and `s->buffer` set to a `LorieBuffer*`
- WHEN `surface_attach` is called with buffer B
- AND `surface_commit` is called
- THEN `wl_buffer_send_release(buffer_A)` is sent
- AND `LorieBuffer_release(s->buffer)` is called
- AND `s->buffer` is set to NULL before the new buffer is processed

##### Scenario: Reattach with no prior buffer does not crash

- GIVEN a `lorie_surface` with `buffer_resource == NULL` and `s->buffer == NULL`
- WHEN `surface_attach` is called with buffer A
- AND `surface_commit` is called
- THEN no release is sent on a NULL resource
- AND no `LorieBuffer_release` is called on NULL

#### Requirement: Buffer Release Lifecycle on Surface Destroy

The system MUST send `wl_buffer.send_release` on the attached buffer and release the `LorieBuffer` when a surface resource is destroyed.

##### Scenario: Surface destroy releases attached buffer

- GIVEN a `lorie_surface` with `buffer_resource` set to buffer A and `s->buffer` set to a `LorieBuffer*`
- WHEN the surface resource is destroyed
- THEN `wl_buffer_send_release(buffer_A)` is sent
- AND `LorieBuffer_release(s->buffer)` is called

#### Requirement: Renderer Integration Path

The system MUST ensure that a surface with a successfully committed SHM buffer is marked for renderer damage and is not skipped by the renderer.

##### Scenario: Commit triggers renderer damage

- GIVEN a `lorie_surface` with a valid `buffer_resource` and `compositor->renderer != NULL`
- WHEN `surface_commit` succeeds with SHM buffer copy
- THEN `lorie_renderer_damage_surface(renderer, s, 0, 0, logical_width, logical_height)` is called
- AND the surface is eligible for rendering in the next frame

#### Requirement: Helper for Buffer Type Detection

The system MUST expose `lorie_shm_buffer_from_resource(struct wl_resource *resource)` to safely extract a `struct lorie_shm_buffer*` from a `wl_resource`, returning NULL if the resource is not a `lorie_shm_buffer`.

##### Scenario: Valid resource returns lorie_shm_buffer

- GIVEN a `wl_resource` created by `shm_pool_create_buffer`
- WHEN `lorie_shm_buffer_from_resource` is called
- THEN it returns the `struct lorie_shm_buffer*` stored as user data

##### Scenario: Invalid resource returns NULL

- GIVEN a `wl_resource` for `wl_surface_interface`
- WHEN `lorie_shm_buffer_from_resource` is called
- THEN it returns NULL

### Acceptance Criteria (PR 2)
| # | Criterion | Measurable Condition |
|---|-----------|----------------------|
| 1 | SHM buffer pixel copy | `surface_commit` with `lorie_shm_buffer` allocates `LorieBuffer` and copies all rows |
| 2 | Null buffer skip | `surface_commit` with `buffer_resource == NULL` does not allocate `LorieBuffer` |
| 3 | Non-SHM buffer safety | `surface_commit` with non-`lorie_shm_buffer` resource does not crash or misread memory |
| 4 | Reattach release event | `surface_commit` on reattach sends `wl_buffer_send_release` on old `buffer_resource` |
| 5 | Reattach LorieBuffer release | `surface_commit` on reattach calls `LorieBuffer_release` on old `s->buffer` |
| 6 | First attach no crash | `surface_commit` on first attach with no prior buffer does not call release on NULL |
| 7 | Surface destroy release event | `surface_handle_resource_destroy` sends `wl_buffer_send_release` on `buffer_resource` |
| 8 | Surface destroy LorieBuffer release | `surface_handle_resource_destroy` calls `LorieBuffer_release` on `s->buffer` |
| 9 | Renderer damage triggered | After successful commit, `lorie_renderer_damage_surface` is called with logical dimensions |
| 10 | Helper correctness | `lorie_shm_buffer_from_resource` returns correct pointer for SHM buffers, NULL for others |

### Test Plan (PR 2)
| Test | Validates | Approach |
|------|-----------|----------|
| `test_surface_commit_shm_buffer` | Criterion 1, 9 | Create surface, attach SHM buffer, call commit, assert `s->buffer != NULL`, assert `s->width/height` set, assert renderer damage called |
| `test_surface_commit_null_buffer` | Criterion 2 | Commit with NULL buffer, assert `s->buffer == NULL` |
| `test_surface_commit_non_shm_buffer` | Criterion 3 | Attach a dummy `wl_resource` (non-SHM), commit, assert no crash, assert `s->buffer == NULL` |
| `test_surface_reattach_releases_old` | Criterion 4, 5 | Attach buffer A, commit, attach buffer B, commit, assert `wl_buffer_send_release` called on A, assert `LorieBuffer_release` called on A's LorieBuffer |
| `test_surface_first_attach_no_crash` | Criterion 6 | First attach + commit on fresh surface, assert no release on NULL |
| `test_surface_destroy_releases_buffer` | Criterion 7, 8 | Create surface, attach + commit, destroy surface, assert `wl_buffer_send_release` called, assert `LorieBuffer_release` called |
| `test_lorie_shm_buffer_from_resource_valid` | Criterion 10 | Create SHM buffer, call helper, assert returned pointer matches |
| `test_lorie_shm_buffer_from_resource_invalid` | Criterion 10 | Call helper with surface resource, assert returns NULL |

### Dependencies (PR 2)

| Dependency | Source | Why |
|------------|--------|-----|
| PR 1 (Core Buffer Creation) | `shm-buffer-creation` change | `lorie_shm_buffer` must exist before surface can use it |
| `lorie_shm_buffer_from_resource` | `compositor.c` / `compositor.h` | Helper must be implemented in PR 1 or PR 2 |
| `struct lorie_surface` | `compositor.h` | `buffer_resource`, `buffer`, `pending_buffer`, `pending_attached` fields |
| `surface_commit` | `surface.c` | Must replace `wl_shm_buffer_get` path with `lorie_shm_buffer_from_resource` |
| `surface_handle_resource_destroy` | `surface.c` | Must release buffer on surface destroy |
| `LorieBuffer_allocate` | `buffer.h` | Allocate destination buffer for pixel copy |
| `LorieBuffer_release` | `buffer.h` | Release old buffer on reattach/destroy |
| `LorieBuffer_description` | `buffer.h` | Access `data` pointer for memcpy destination |
| `lorie_renderer_damage_surface` | `renderer.h` | Called after commit with valid dimensions |
| `test_surface.c` | existing | Existing surface tests must continue to pass |

---

## Cross-PR Integration Requirements

### Requirement: No Regressions in Existing Behavior

All existing tests in `test_compositor.c`, `test_surface.c`, `test_renderer.c`, `test_input.c`, `test_protocols.c`, `test_ndk_build.c`, and other test suites MUST continue to pass after both PRs are applied.

##### Scenario: Existing compositor tests pass

- GIVEN the full test suite compiled with PR 1 and PR 2 changes
- WHEN `./gradlew test` or `test_main` runner executes
- THEN all 53+ existing tests pass without modification

### Requirement: X11 Mode Unaffected

The changes MUST NOT affect the X11 server code path. The `lorie_shm_buffer` and surface commit changes are confined to the Wayland compositor files.

##### Scenario: X11 smoke test passes

- GIVEN the application built with both PRs
- WHEN run in X11 mode
- THEN the X11 server initializes and renders correctly

---

## Data Structures

### struct lorie_shm_pool (modified)

```c
struct lorie_shm_pool {
    void *data;
    int32_t size;
    int refcount;        /* ADDED: starts at 1 */
};
```

### struct lorie_shm_buffer (new)

```c
struct lorie_shm_buffer {
    struct wl_resource *resource;   /* the wl_buffer resource */
    struct lorie_shm_pool *pool;    /* back-ref to pool (ref-counted) */
    int32_t offset;
    int32_t width;
    int32_t height;
    int32_t stride;
    uint32_t format;
    void *data;                     /* pool->data + offset */
};
```

## API Surface

### New / Modified Functions

| Function | Location | PR | Description |
|----------|----------|-----|-------------|
| `shm_pool_create_buffer` | `compositor.c` | 1 | Validates params, creates `wl_buffer` + `lorie_shm_buffer`, increments pool refcount |
| `buffer_destroy` | `compositor.c` | 1 | Decrements pool refcount, frees `lorie_shm_buffer`, triggers pool cleanup if refcount == 0 |
| `lorie_shm_pool_create` | `compositor.c` | 1 | Initializes `refcount = 1` |
| `lorie_shm_pool_destroy` | `compositor.c` | 1 | Checks refcount; only munmap/free when refcount reaches 0 |
| `shm_pool_handle_resource_destroy` | `compositor.c` | 1 | Marks pool for deferred destroy if refcount > 1 |
| `lorie_shm_buffer_from_resource` | `compositor.c` | 2 | Returns `struct lorie_shm_buffer*` from `wl_resource` user data, or NULL |
| `surface_commit` | `surface.c` | 2 | Extracts `lorie_shm_buffer`, copies pixels to `LorieBuffer`, handles release lifecycle |
| `surface_handle_resource_destroy` | `surface.c` | 2 | Ensures `wl_buffer_send_release` and `LorieBuffer_release` on attached buffer |

## Risk Notes

1. **Format support**: Only ARGB8888 and XRGB8888 are supported. Other formats are rejected with `WL_SHM_ERROR_INVALID_FORMAT`.
2. **Performance**: Row-by-row `memcpy` per commit is CPU-intensive. Zero-copy via `LorieBuffer_wrapFileDescriptor` is documented as future work.
3. **Pool lifetime**: Reference counting prevents use-after-free but adds complexity. Tests must cover all refcount edge cases.
4. **wl_shm_buffer_get compatibility**: The current code uses `wl_shm_buffer_get` from `wayland-server-protocol.h`. PR 2 replaces this with `lorie_shm_buffer_from_resource`. The original `wl_shm_buffer_get` path (from `libwayland-server`) will no longer be used for SHM buffers created through our compositor.
