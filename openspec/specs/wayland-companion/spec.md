# Wayland Companion Specification

## Purpose

Define the Termux-launched Wayland companion entrypoint (`termux-wayland`) that mirrors the official `termux-x11` companion command architecture. This ensures the Wayland compositor runs inside the Termux runtime with correct library paths, socket visibility for Termux/proot/chroot clients, and a user flow aligned with existing X11 README documentation.

The spec is staged across three review slices:
- **PR 1** — command scaffolding, Java entrypoint, and packaging.
- **PR 2** — runtime directory and socket hardening aligned with Wayland/Termux conventions.
- **PR 3 / Design Gate** — cross-process `Surface`/`Activity` coordination requirements and non-goals.

---

## Requirements

### Requirement: Termux Wayland Companion Script

The system MUST provide a `termux-wayland` shell script, installable via the `termux-x11-nightly` companion package, that launches `WaylandCmdEntryPoint` through the existing `Loader`/`app_process` mechanism.

#### Scenario: Script exists and is executable after package install

- GIVEN the user has run `pkg install termux-x11-nightly`
- WHEN the user runs `which termux-wayland`
- THEN the script is found at `$PREFIX/bin/termux-wayland` and is executable

#### Scenario: Script mirrors termux-x11 guard behavior

- GIVEN a headless environment or Android version below API 26
- WHEN the user runs `termux-wayland`
- THEN the script prints an informative error and exits with a non-zero code, matching `termux-x11` behavior

#### Scenario: Script preserves startup environment

- GIVEN `LD_LIBRARY_PATH`, `LD_PRELOAD`, and `CLASSPATH` are set in the Termux shell
- WHEN the user runs `termux-wayland -xstartup "xfce4-session"`
- THEN the script saves those variables as `XSTARTUP_LD_LIBRARY_PATH`, `XSTARTUP_LD_PRELOAD`, and `XSTARTUP_CLASSPATH`, unsets the originals, sets `CLASSPATH` to the loader APK, and executes `app_process`

#### Scenario: Script overrides loader entrypoint class

- GIVEN the user runs `termux-wayland`
- WHEN `app_process` launches `com.termux.x11.Loader`
- THEN `TERMUX_X11_LOADER_OVERRIDE_CMDENTRYPOINT_CLASS` is set to `com.termux.x11.WaylandCmdEntryPoint` so the Loader invokes the Wayland entrypoint instead of the X11 entrypoint

#### Scenario: termux-wayland defaults to pure Wayland mode

- GIVEN the user runs `termux-wayland`
- WHEN the script starts
- THEN the script passes `--pure-wayland` to `WaylandCmdEntryPoint` by default

#### Scenario: termux-x11 defaults to XWayland-backed mode

- GIVEN the user runs `termux-x11`
- WHEN the script starts
- THEN the script passes `-xwayland` to `WaylandCmdEntryPoint` by default

#### Scenario: RED test — script missing

- GIVEN `termux-wayland` is not installed
- WHEN a test checks `$PREFIX/bin/termux-wayland`
- THEN the file does not exist (RED phase confirms missing script)

---

### Requirement: Wayland Command Entrypoint Java Class

The system MUST provide a `WaylandCmdEntryPoint` Java class modeled on `CmdEntryPoint` but with Wayland-specific broadcast action and native method signatures.

#### Scenario: Class is loadable and not stripped by R8/ProGuard

- GIVEN the APK is built with `minifyEnabled true`
- WHEN the app starts or `Loader` reflects on `WaylandCmdEntryPoint`
- THEN the class and its `main(String[])` method are present and callable

#### Scenario: Entrypoint broadcasts Wayland-specific start intent

- GIVEN `WaylandCmdEntryPoint.main(args)` is invoked
- WHEN the instance is constructed
- THEN it broadcasts an intent with action `com.termux.x11.WaylandCmdEntryPoint.ACTION_START` (distinct from X11's `CmdEntryPoint.ACTION_START`)

#### Scenario: Native start method binds to Wayland JNI

- GIVEN `WaylandCmdEntryPoint.start(args)` is called
- WHEN the native layer receives the call
- THEN it resolves to `Java_com_termux_x11_WaylandCmdEntryPoint_start` (or equivalent dynamically registered native method) and initializes the Wayland compositor

#### Scenario: Context creation reuses existing loader infrastructure

- GIVEN `WaylandCmdEntryPoint` runs outside the app process via `app_process`
- WHEN the static initializer executes
- THEN it creates a `Context` using the same reflection-based `createContext()` pattern as `CmdEntryPoint`, loads `libXlorie.so` from the APK, and prepares the main `Looper`

#### Scenario: Entrypoint parses `--pure-wayland` flag

- GIVEN `WaylandCmdEntryPoint` is executed with `--pure-wayland`
- WHEN native start is invoked
- THEN the selected mode is passed to JNI, and XWayland is not spawned

#### Scenario: Entrypoint parses `-xwayland` flag

- GIVEN `WaylandCmdEntryPoint` is executed with `-xwayland`
- WHEN native start is invoked
- THEN the selected mode is passed to JNI, and XWayland is spawned

#### Scenario: RED test — class stripped by ProGuard

- GIVEN no keep rule exists for `WaylandCmdEntryPoint`
- WHEN the APK is built with R8
- THEN `WaylandCmdEntryPoint` is removed and `Loader` fails with `ClassNotFoundException` (RED phase confirms missing keep rule)

---

### Requirement: Build and Packaging Integration

The system MUST copy `termux-wayland` into the companion package (`.deb` and `.pkg.tar.xz`) during `build_termux_package` execution.

#### Scenario: Package contains the new script

- GIVEN `./build_termux_package` runs successfully
- WHEN the resulting `.deb` or `.pkg.tar.xz` is inspected
- THEN `$PREFIX/bin/termux-wayland` is present alongside `termux-x11` and `termux-x11-preference`

#### Scenario: No regression in existing package contents

- GIVEN the package is built with the new script included
- WHEN it is installed
- THEN `termux-x11`, `termux-x11-preference`, and the loader APK are still installed correctly

---

### Requirement: ProGuard / R8 Keep Rules

The system MUST include a ProGuard keep rule for `WaylandCmdEntryPoint` so it survives code shrinking.

#### Scenario: Keep rule present in proguard-rules.pro

- GIVEN `app/proguard-rules.pro` is read
- WHEN the file contains keep rules for entrypoint classes
- THEN a `-keep class com.termux.x11.WaylandCmdEntryPoint` rule (or equivalent) is present

---

### Requirement: Runtime Directory Resolution

The system MUST determine `XDG_RUNTIME_DIR` using the following precedence, and MUST create the directory with mode `0700` before socket creation:

1. JNI-provided app-private files directory path (if passed and valid).
2. Existing `XDG_RUNTIME_DIR` environment variable (if set and non-empty).
3. Existing `TMPDIR` environment variable (if set and non-empty).
4. Termux `$PREFIX/tmp` if accessible.
5. `/tmp` as a final fallback.

#### Scenario: JNI path passed from WaylandActivity

- GIVEN `WaylandActivity` starts the compositor and passes `getFilesDir().getAbsolutePath()` to native start
- WHEN the native layer initializes
- THEN the runtime dir is set to that app-private directory, created with mode `0700`, and the socket is created there

#### Scenario: XDG_RUNTIME_DIR already set

- GIVEN `XDG_RUNTIME_DIR=/run/user/1000` is exported
- WHEN the compositor initializes
- THEN the runtime dir is `/run/user/1000`, it is created with `mkdir` and `chmod 0700` if needed, and the socket is created there

#### Scenario: TMPDIR used when XDG_RUNTIME_DIR unset

- GIVEN `XDG_RUNTIME_DIR` is unset and `TMPDIR=/data/data/com.termux/files/usr/tmp`
- WHEN the compositor initializes
- THEN `XDG_RUNTIME_DIR` is set to `/data/data/com.termux/files/usr/tmp`, the directory is created with mode `0700`, and the socket is created there

#### Scenario: Termux fallback when env vars unset

- GIVEN neither `XDG_RUNTIME_DIR` nor `TMPDIR` is set
- WHEN the compositor initializes on a standard Termux installation
- THEN `XDG_RUNTIME_DIR` falls back to `/data/data/com.termux/files/usr/tmp`, the directory is created with mode `0700`, and the socket is created there

#### Scenario: /tmp fallback

- GIVEN neither `XDG_RUNTIME_DIR` nor `TMPDIR` is set and Termux `$PREFIX/tmp` is not accessible
- WHEN the compositor initializes
- THEN `XDG_RUNTIME_DIR` falls back to `/tmp`, the directory is created with mode `0700`, and the socket is created there

#### Scenario: proot with --shared-tmp sees socket

- GIVEN Termux has `TMPDIR=$PREFIX/tmp` and the compositor creates a socket there
- WHEN `proot-distro login <distro> --shared-tmp` is used
- THEN the proot container sees the socket at `/tmp` and a Wayland client can connect

#### Scenario: chroot with bound /tmp sees socket

- GIVEN Termux has `XDG_RUNTIME_DIR=$PREFIX/tmp` and the compositor creates a socket there
- WHEN the host runtime dir is bind-mounted into the chroot at `/tmp`
- THEN the chroot container sees the socket and a Wayland client can connect

#### Scenario: RED test — hardcoded path ignores env

- GIVEN the old hardcoded path logic is active
- WHEN `XDG_RUNTIME_DIR=/custom/run` is set
- THEN the socket is still created at `/data/data/com.termux/files/usr/tmp` (RED phase confirms missing env awareness)

---

### Requirement: Wayland Display Name Resolution

The system MUST determine `WAYLAND_DISPLAY` using the following precedence:

1. Existing `WAYLAND_DISPLAY` environment variable (if set and non-empty).
2. Default to `wayland-0`.

The resolved value MUST be exported to the environment and passed to the compositor as the socket name.

#### Scenario: WAYLAND_DISPLAY already set

- GIVEN `WAYLAND_DISPLAY=wayland-1` is exported
- WHEN the compositor initializes
- THEN the socket name is `wayland-1` and the socket is created at `$XDG_RUNTIME_DIR/wayland-1`

#### Scenario: Default wayland-0

- GIVEN `WAYLAND_DISPLAY` is unset
- WHEN the compositor initializes
- THEN `WAYLAND_DISPLAY` is set to `wayland-0` and the socket is created at `$XDG_RUNTIME_DIR/wayland-0`

#### Scenario: Absolute path WAYLAND_DISPLAY

- GIVEN `WAYLAND_DISPLAY=/tmp/my-wayland.sock` is exported
- WHEN the compositor initializes
- THEN `wl_display_add_socket` treats it as an absolute path and creates the socket at `/tmp/my-wayland.sock`

---

### Requirement: -xstartup Execution

The system MUST support an optional `-xstartup <command>` argument passed through `WaylandCmdEntryPoint` that executes the given command after the Wayland socket is ready.

#### Scenario: -xstartup runs after socket ready

- GIVEN the user runs `termux-wayland -xstartup "dbus-launch --exit-with-session xfce4-session"`
- WHEN the compositor has created the socket and `connected()` returns true
- THEN the startup command is executed in a subprocess with `XSTARTUP_LD_LIBRARY_PATH`, `XSTARTUP_LD_PRELOAD`, and `XSTARTUP_CLASSPATH` restored

#### Scenario: -xstartup omitted

- GIVEN the user runs `termux-wayland` without `-xstartup`
- WHEN the compositor is ready
- THEN the command process remains alive (listening for connections) and no startup command is executed

#### Scenario: RED test — -xstartup ignored

- GIVEN `-xstartup` handling is not implemented
- WHEN the user passes `-xstartup "echo hello"`
- THEN the argument is ignored and nothing is executed (RED phase confirms missing implementation)

---

### Requirement: Activity-Bound Compositor Model (Phase 1)

The system MUST, in PR 1/2, use an Activity-bound compositor model where `WaylandCmdEntryPoint` sets environment variables and broadcasts an intent to start `WaylandActivity`, and `WaylandActivity` owns the compositor lifecycle and `Surface`.

#### Scenario: Command launches Activity

- GIVEN `termux-wayland` is run from a Termux shell
- WHEN `WaylandCmdEntryPoint` initializes
- THEN it broadcasts `ACTION_START` to the `com.termux.x11` package, and `WaylandActivity` is brought to the foreground (or created if not running)

#### Scenario: Activity owns compositor and surface

- GIVEN `WaylandActivity` receives the `ACTION_START` broadcast
- WHEN it creates or resumes its `Surface`
- THEN the compositor and renderer are initialized in the Activity process, drawing to the Activity-owned `ANativeWindow`

#### Scenario: Command process may exit after Activity launch

- GIVEN `WaylandCmdEntryPoint` has broadcast the start intent
- WHEN the Activity is confirmed running (or after a reasonable timeout)
- THEN the command process MAY exit, leaving the compositor running in the Activity process

---

### Requirement: Cross-Process Surface Handoff — Design Gate

The system MUST NOT implement a command-process compositor (where the compositor runs in the `app_process` and receives the Activity `Surface` over IPC) until a design gate explicitly evaluates and approves the architecture.

#### Scenario: Design gate evaluates options

- GIVEN PR 1 and PR 2 are complete and stable
- WHEN the design gate document is written
- THEN it evaluates at least three options: (A) Activity-bound compositor, (B) command-process compositor with Binder/IPC surface handoff, and (C) renderer streaming with independent Activity display

#### Scenario: Design gate documents risks

- GIVEN the design gate evaluates Option B or C
- WHEN the document is reviewed
- THEN it documents `Surface`/`IGraphicBufferProducer` cross-process delivery risks, Android version compatibility, and performance implications of cross-process buffer submission

#### Scenario: No surface handoff without approved design

- GIVEN the design gate has not approved a command-process compositor
- WHEN a developer proposes PR 3 implementation
- THEN the PR MUST be blocked until the design gate is approved

---

## Test Plan

| Test | Location | Type | What It Verifies |
|------|----------|------|------------------|
| `test_termux_wayland_script_exists` | `test_companion.sh` (new) | Integration | `$PREFIX/bin/termux-wayland` is present and executable after package install |
| `test_termux_wayland_guard_headless` | `test_companion.sh` (new) | Integration | Script exits with error on headless / API < 26 |
| `test_wayland_cmd_entrypoint_class_loadable` | `test_wayland_entrypoint.java` (new) | Unit | `WaylandCmdEntryPoint` is not stripped by R8 |
| `test_wayland_cmd_entrypoint_broadcast_action` | `test_wayland_entrypoint.java` (new) | Unit | Broadcast intent action matches `WaylandCmdEntryPoint.ACTION_START` |
| `test_wayland_cmd_entrypoint_native_start` | `test_wayland_entrypoint.c` (new) | Unit | `Java_com_termux_x11_WaylandCmdEntryPoint_start` initializes compositor |
| `test_xdg_runtime_dir_from_env` | `test_runtime_env.c` (new) | Unit | `XDG_RUNTIME_DIR` respects existing env var |
| `test_xdg_runtime_dir_from_tmpdir` | `test_runtime_env.c` (new) | Unit | Falls back to `TMPDIR` when `XDG_RUNTIME_DIR` unset |
| `test_xdg_runtime_dir_termux_fallback` | `test_runtime_env.c` (new) | Unit | Falls back to `$PREFIX/tmp` when both unset |
| `test_xdg_runtime_dir_tmp_fallback` | `test_runtime_env.c` (new) | Unit | Falls back to `/tmp` when Termux path unavailable |
| `test_runtime_dir_created_with_0700` | `test_runtime_env.c` (new) | Unit | Directory is created with `mkdir` + `chmod 0700` |
| `test_wayland_display_from_env` | `test_runtime_env.c` (new) | Unit | `WAYLAND_DISPLAY` respects existing env var |
| `test_wayland_display_defaults_to_wayland_0` | `test_runtime_env.c` (new) | Unit | Defaults to `wayland-0` when unset |
| `test_socket_created_at_xdg_runtime_dir` | `test_runtime_env.c` (new) | Unit | Socket exists at `$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY` after compositor start |
| `test_xstartup_executed_after_ready` | `test_companion.sh` (new) | Integration | `-xstartup` command runs after `connected()` is true |
| `test_proot_shared_tmp_sees_socket` | `test_companion.sh` (new) | Integration | Socket visible inside `proot-distro --shared-tmp` |
| `./gradlew assembleDebug` | CI / local | Integration | APK builds for all ABIs with `WaylandCmdEntryPoint` |
| `./build_termux_package` | CI / local | Integration | Package contains `termux-wayland` script |
| `test_x11_flow_unregressed` | `test_x11_regression.sh` (existing) | Integration | `termux-x11` command and X11 mode still work |

### Requirement: XDG-Shell Configure Lifecycle

The compositor SHALL send the initial configure event immediately upon surface role assignment during the `xdg_surface.get_toplevel` and `xdg_surface.get_popup` requests. The client MUST acknowledge this configure event using `xdg_surface.ack_configure` before committing the surface, though the compositor SHALL fallback to a standard configuration flow if needed for compatibility.

#### Scenario: Initial configure sent on get_toplevel
- GIVEN a client has bound to `xdg_shell` and created an `xdg_surface`
- WHEN the client requests a toplevel role by calling `xdg_surface.get_toplevel`
- THEN the compositor SHALL immediately send an initial configure event to the client
- AND the compositor SHALL set the `configured` flag for the surface to indicate it has been configured

#### Scenario: Initial configure sent on get_popup
- GIVEN a client has bound to `xdg_shell` and created an `xdg_surface`
- WHEN the client requests a popup role by calling `xdg_surface.get_popup`
- THEN the compositor SHALL immediately send an initial configure event to the client
- AND the compositor SHALL set the `configured` flag for the surface to indicate it has been configured

#### Scenario: Acknowledged configure commit lifecycle
- GIVEN a surface has been assigned an `xdg_surface` role and the initial configure event was sent
- WHEN the client sends `xdg_surface.ack_configure` followed by `wl_surface.commit`
- THEN the compositor SHALL process the commit and map the surface without protocol deadlock

---

### Requirement: XDG-Shell Surface Role Presence Validation

The compositor MUST validate that an `xdg_surface` has been assigned a role (either toplevel or popup) when a commit is performed on its associated `wl_surface`. If no role is assigned at the time of commit, the compositor MUST raise a protocol error.

#### Scenario: Surface commit without role raises protocol error
- GIVEN an `xdg_surface` is created from a `wl_surface` but no role has been assigned
- WHEN the client performs a `wl_surface.commit` on the surface
- THEN the compositor SHALL raise an `XDG_SURFACE_ERROR_NOT_CONSTRUCTED` protocol error
- AND the client connection SHALL be terminated

#### Scenario: Surface commit with role succeeds
- GIVEN an `xdg_surface` is created and assigned a role via `xdg_surface.get_toplevel`
- WHEN the client performs a `wl_surface.commit` on the surface
- THEN the compositor SHALL successfully process the commit without raising any protocol error

---

## Acceptance Criteria

1. `termux-wayland` script is installable via `termux-x11-nightly` and executable from Termux shell.
2. Running `termux-wayland` without flags defaults to pure Wayland mode (passing `--pure-wayland`), launches `WaylandCmdEntryPoint` via `Loader`, sets `XDG_RUNTIME_DIR` and `WAYLAND_DISPLAY`, and creates a socket at `$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY`.
3. Running `termux-x11` without flags defaults to XWayland mode (passing `-xwayland`).
4. A Wayland client (e.g., `weston-terminal` or `foot`) launched from the same shell can connect to the socket.
5. `WaylandActivity` passes its app-private files directory path via JNI to set `XDG_RUNTIME_DIR` inside the sandboxed process, displaying the compositor output when launched (either by the command or manually).
6. `proot-distro` with `--shared-tmp` can see and connect to the Wayland socket.
7. chroot with bound `/tmp` can see and connect to the Wayland socket.
8. `-xstartup` executes the provided command after socket readiness.
9. No regressions in existing X11 `termux-x11` flow.
10. `WaylandCmdEntryPoint` survives R8/ProGuard shrinking with explicit keep rules.
11. No attempt to implement cross-process `Surface` handoff occurs without an approved design gate document.
12. Compositor runs and launches without permission/directory creation crashes under both startup modes.
13. The compositor sends initial configure events immediately upon surface role assignment (toplevel or popup).
14. The compositor validates role presence on surface commit, raising `XDG_SURFACE_ERROR_NOT_CONSTRUCTED` if missing.
