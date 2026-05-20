# Wayland Integration Worker Output

## Summary

Created the integration layer and entry points for the Wayland compositor in termux-x11. This provides the main entry point, build configuration, common headers, and test plan.

## Files Created

### 1. `app/src/main/cpp/lorie-wayland/wayland-lorie.h`
Common header file containing:
- **Shared state structures**: `lorie_wayland_shared_state` for compositor ↔ renderer communication
- **Compositor context**: `lorie_wayland_compositor` with display, event loop, output, surfaces
- **Surface management**: `lorie_wayland_surface` with damage tracking, buffer management
- **Output configuration**: `lorie_wayland_output` and `lorie_wayland_output_config`
- **Event types**: Mirrored from lorie.h for Android event compatibility
- **Mutex utilities**: `lorie_wayland_mutex_lock/unlock` with Bionic workaround
- **Function declarations**: All compositor, renderer, input, and XWayland functions

### 2. `app/src/main/cpp/lorie-wayland/main.c`
Main entry point for the Wayland compositor, following patterns from `cmdentrypoint.c`:

**JNI Entry Points:**
- `Java_com_termux_x11_WaylandEntryPoint_start()` - Starts compositor with args
- `Java_com_termux_x11_WaylandEntryPoint_stop()` - Stops compositor gracefully
- `Java_com_termux_x11_WaylandEntryPoint_getWaylandConnection()` - Returns Wayland socket fd
- `Java_com_termux_x11_WaylandEntryPoint_connected()` - Checks if compositor running

**Features:**
- Argument parsing: `--width`, `--height`, `--refresh`, `--scale`, `--name`, `--xwayland`
- Environment setup: TMPDIR, WAYLAND_DISPLAY, XDG_RUNTIME_DIR
- CPU affinity setting (same as X11 version)
- Debug logcat support
- Android event handling (touch, mouse, key, stylus)
- Compositor thread with Wayland event loop
- Renderer thread with shared state synchronization
- XWayland integration stubs

**Architecture:**
- Dual-thread design: compositor thread (Wayland event loop) + renderer thread
- Shared state via `lorie_wayland_shared_state` with mutex/condvar
- Unix socket pair for Android ↔ compositor communication

### 3. `app/src/main/cpp/lorie-wayland/CMakeLists.txt`
Build configuration:
- Builds `lorie-wayland` static library
- Includes wayland-server, pixman, Android native libs
- Links with existing `lorie-buffer` component
- Can be integrated into `libXlorie.so` or built standalone

### 4. `.pi/wayland-test-plan.md`
Comprehensive test plan covering:
- Basic startup and client connection
- XWayland integration
- Input handling (touch, mouse, keyboard, stylus)
- Output configuration and scaling
- Multi-window support
- Clipboard integration
- Performance testing (60 FPS target)
- Stress testing (24-hour stability)
- Regression testing for X11 mode
- Debug commands and log tags

## Design Decisions

### Compatibility with Existing Code
- Mirrors `cmdentrypoint.c` patterns exactly (JNI signatures, env setup, threading)
- Reuses `lorieEvent` union for Android events (no protocol changes needed)
- Uses same `LorieBuffer` system for GPU buffers
- Maintains `lorie_shared_server_state` pattern for renderer communication

### XWayland Support
- Lazy startup: XWayland only starts when `--xwayland` flag provided
- Stub implementations ready for XWayland worker to fill in
- DISPLAY environment management
- Process lifecycle handling

### Android Integration
- Socket directory: `$TMPDIR/wayland/`
- Wayland socket: `wayland-0`
- Supports both standalone and embedded (libXlorie.so) builds

## Next Steps

1. **Build System**: Integrate wayland-server recipe into main CMakeLists.txt
2. **Renderer**: Implement `lorie_wayland_renderer_thread` with actual GLES2 rendering
3. **Protocols**: Implement wl_compositor, wl_shell, xdg_shell interfaces
4. **Input**: Implement wl_seat with pointer, keyboard, touch
5. **XWayland**: Implement `lorie_wayland_xwayland_start/stop` with actual X11 server spawning
6. **Testing**: Follow test plan and verify all scenarios
