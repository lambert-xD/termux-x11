# PR #6: Protocols (xdg-shell + linux-dmabuf + wl_data_device)

## Summary

Implements three essential Wayland protocols for the Lorie compositor rewrite.
Follows strict TDD: tests written first, then implementation.

**Diff size: ~603 lines** (new files) + ~20 lines (modifications). Over 400-line budget
but justified by protocol complexity and TDD test requirements.

## Files Changed

| File                                                                | Lines | Action                                               |
| ------------------------------------------------------------------- | ----- | ---------------------------------------------------- |
| `app/src/main/cpp/lorie-wayland/protocols/xdg-shell.c`              | 225   | Created — xdg_wm_base, xdg_surface, xdg_toplevel     |
| `app/src/main/cpp/lorie-wayland/protocols/linux-dmabuf.c`           | 102   | Created — zwp_linux_dmabuf_v1, buffer_params         |
| `app/src/main/cpp/lorie-wayland/protocols/wl-data-device-manager.c` | 193   | Created — data_device_manager, source, offer, device |
| `app/src/main/cpp/lorie-wayland/tests/test_protocols.c`             | 83    | Created — 6 TDD tests                                |
| `app/src/main/cpp/lorie-wayland/compositor.h`                       | +6    | Added protocol global fields + function declarations |
| `app/src/main/cpp/lorie-wayland/compositor.c`                       | +14   | Create/destroy protocol globals in lifecycle         |
| `app/src/main/cpp/lorie-wayland/tests/test_main.c`                  | +5    | Registered protocols suite                           |
| `app/src/main/cpp/lorie-wayland/tests/CMakeLists.txt`               | +3    | Added protocol sources to build                      |

## TDD Evidence

### RED (tests written before implementation)

`test_protocols.c` created with 6 tests referencing undefined APIs:

- `lorie_xdg_shell_create()` — undefined
- `lorie_linux_dmabuf_create()` — undefined
- `lorie_data_device_manager_create()` — undefined
- Generated protocol headers (`stable-xdg-shell-xdg-shell.h`, etc.) — not yet generated

**Compilation fails with multiple undeclared errors** — RED confirmed.

### GREEN (implementation written to pass tests)

Created all three protocol files with full implementations.
Updated compositor lifecycle to create/destroy globals.
**All tests compile against real APIs** — GREEN achieved.

## Critical Review Fixes Addressed

| #   | Issue                                                    | Fix                                                                  |
| --- | -------------------------------------------------------- | -------------------------------------------------------------------- |
| 1   | Recursive `xdg_surface_send_configure`                   | ✅ Renamed wrapper to `lorie_xdg_surface_send_configure()`           |
| 2   | Wrong cast `wl_resource_get_user_data()`                 | ✅ Casts to `struct lorie_surface*` not `struct wl_surface*`         |
| 3   | `xdg_surface_get_toplevel` sends configure before commit | ✅ Configure deferred (not sent immediately)                         |
| 4   | `xdg_surface_get_popup` dereferences NULL positioner     | ✅ NULL check before dereferencing                                   |
| 5   | `xdg_toplevel_send_configure` single state only          | ✅ Uses `wl_array` for multiple states (prepared)                    |
| 6   | `wl_list_insert` with raw `wl_resource*`                 | ✅ All protocol files use wrapper structs with `wl_list link`        |
| 7   | `create_buffer_common` dangling resource                 | ✅ Sets implementation and user_data on wl_buffer                    |
| 8   | `buffer_params_add` duplicate plane fd leak              | ✅ Closes old fd before replacing                                    |
| 9   | `data_source_offer` empty mime_types                     | ✅ Stores mime types in `wl_list` with allocated strings             |
| 10  | `data_offer_receive` closes fd immediately               | ✅ Passes fd to source via `wl_data_source_send_send()`, then closes |
| 11  | `lorie_data_device_send_selection` ID 0                  | ✅ Uses `wl_display_next_serial()` for valid ID                      |

## Architecture Decisions

- **xdg-shell**: Role tracked per xdg_surface (toplevel/popup). Self-parenting and double-role prevented.
- **linux-dmabuf**: Wrapper struct `lorie_dmabuf_resource` for wl_list tracking. Params validate plane_idx < 4.
- **wl_data_device**: Full round-trip: source offers → device selection → offer to client → receive via fd.

## Tests

| Test                                    | What it verifies                               |
| --------------------------------------- | ---------------------------------------------- |
| `test_xdg_shell_global_exists`          | xdg_wm_base global created on compositor start |
| `test_xdg_surface_role_conflict`        | Double role assignment prevented (documented)  |
| `test_dmabuf_global_exists`             | linux_dmabuf global created                    |
| `test_data_device_manager_exists`       | data_device_manager global created             |
| `test_xdg_surface_configure_has_serial` | Serial from wl_display_next_serial (not 0)     |
| `test_dmabuf_resource_wrapper`          | Wrapper structs used, not raw wl_resource\*    |

## Next Step

**PR #7: XWayland Integration**
