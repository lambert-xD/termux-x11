# Lorie Wayland Compositor

Native Wayland compositor for Android, coexisting with the existing X11 server in termux-x11.

## Architecture

```
┌─────────────────┐     ┌─────────────────┐
│   Java Layer    │     │  Native Layer   │
│ WaylandActivity │────▶│ lorie_wayland_  │
│ LorieWaylandView│     │   main()        │
└─────────────────┘     └────────┬────────┘
                                 │
        ┌────────────────────────┼────────────────────────┐
        ▼                        ▼                        ▼
┌───────────────┐      ┌───────────────┐      ┌───────────────┐
│  Compositor   │◄────►│    Input      │      │   Renderer    │
│ wl_display    │      │ wl_seat       │      │  GLES2/EGL    │
│ wl_compositor │      │ pointer/keyboard/touch│  compositing │
│ wl_output     │      │               │      │               │
└───────┬───────┘      └───────────────┘      └───────────────┘
        │
        ▼
┌─────────────────────────────────────────────────────────────┐
│                         Protocols                            │
│  xdg-shell │ linux-dmabuf │ wl_data_device │ XWayland       │
└─────────────────────────────────────────────────────────────┘
```

## Building

```bash
# Initialize submodules
git submodule update --init --recursive

# Build with Gradle (Android)
./gradlew assembleDebug

# Build tests only (requires CMake + Android NDK)
cd app/src/main/cpp/lorie-wayland/tests
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26
cmake --build build
```

## Running Tests

```bash
# Native tests (on device or emulator)
adb push build/lorie-wayland-tests /data/local/tmp/
adb shell /data/local/tmp/lorie-wayland-tests
```

## Launching Wayland Mode

From Android:

1. Install the APK
2. Launch **WaylandActivity** from the launcher
3. Connect Wayland clients via the auto-assigned socket:
   ```bash
   WAYLAND_DISPLAY=wayland-0 <client>
   ```

From ADB:

```bash
adb shell am start -n com.termux.x11/.WaylandActivity
```

## Known Limitations

- **SHM buffer import**: Not yet wired to `LorieBuffer` (placeholder in `wl_shm`)
- **DMA-BUF import**: Stub implementation in `linux-dmabuf.c`
- **Clipboard**: Receives data safely but not forwarded to Wayland protocol yet
- **Text input**: Sends synthetic key events; Unicode requires xkbcommon
- **Z-index / stacking**: All surfaces at z=0; no explicit ordering API
- **AImageReader**: Not used; renderer composites directly via GLES2
