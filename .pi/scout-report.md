# Termux:X11 Project Architecture Report

## 1. Overview

Termux:X11 is a **full X11 server implementation for Android** that runs the Xorg X server natively using the Android NDK. It consists of:

- An Android app (Java/Kotlin UI layer)
- A native C/C++ X11 server compiled into `libXlorie.so`
- A custom Android backend (`lorie/`) that bridges X11 to Android surfaces

**Key insight:** The project does NOT use XWayland or any Wayland code. It is a pure X11 server with a custom Android-specific DDX (Device Dependent X) backend.

## 2. Build System

### Gradle (Android App)

- **Root build.gradle**: Standard Android Gradle Plugin 9.2.1
- **app/build.gradle**:
  - compileSdk 34, minSdk 26, targetSdk 34
  - ABI splits: x86, x86_64, armeabi-v7a, arm64-v8a + universal APK
  - Uses `externalNativeBuild.cmake.path "src/main/cpp/CMakeLists.txt"`
  - ProGuard minification enabled even for debug builds
  - Generates `Prefs.java` dynamically from `preferences.xml`

### CMake (Native Code)

- **Entry**: `app/src/main/cpp/CMakeLists.txt`
- **Builds**: `libXlorie.so` (shared library)
- **Standard**: C11, C++17, RelWithDebInfo
- **Recipe-based**: Each dependency has a `.cmake` recipe in `app/src/main/cpp/recipes/`

### Submodules Used in Build

| Submodule    | Recipe                         | Purpose                                         |
| ------------ | ------------------------------ | ----------------------------------------------- |
| pixman       | pixman.cmake                   | Pixel manipulation library (low-level graphics) |
| xserver      | xserver.cmake                  | The actual Xorg X server source code            |
| xorgproto    | xorgproto.cmake                | X protocol definitions                          |
| libfontenc   | fontenc.cmake                  | Font encoding                                   |
| libepoxy     | (patched)                      | GL dispatch (used for generating GL headers)    |
| libxcvt      | (inline)                       | CVT timing calculations for display modes       |
| libx11       | Xau.cmake                      | X11 client library (for auth)                   |
| libxau       | Xau.cmake                      | X authorization                                 |
| libxdmcp     | Xdmcp.cmake                    | X Display Manager Control Protocol              |
| libxfont     | Xfont2.cmake                   | X font handling                                 |
| libxkbfile   | xkbcomp.cmake                  | XKB keyboard handling                           |
| libxshmfence | xshmfence.cmake                | Shared memory fences                            |
| libxtrans    | tirpc.cmake                    | X transport (patched to use TI-RPC)             |
| libtirpc     | tirpc.cmake                    | Transport-Independent RPC                       |
| xkbcomp      | xkbcomp.cmake                  | XKB keymap compiler                             |
| bzip2        | (not in recipes, used by deps) | Compression                                     |

## 3. Native Code Architecture

### Directory: `app/src/main/cpp/lorie/`

This is the **Android-specific DDX backend** for Xorg. Files:

| File              | Purpose                                                                                                                       |
| ----------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| `cmdentrypoint.c` | JNI entry point from Java. Starts the X server in a separate thread. Handles command-line args, env setup, socket connections |
| `InitOutput.c`    | X server screen initialization. Sets up framebuffer, RandR, Present, DRI3, EXA acceleration hooks                             |
| `InitInput.c`     | X input device initialization (mouse, keyboard, touch, stylus, eraser)                                                        |
| `InputXKB.c`      | XKB keyboard handling                                                                                                         |
| `renderer.c`      | **GLES2/EGL renderer thread**. Renders X11 framebuffer to Android SurfaceView via OpenGL ES                                   |
| `buffer.c`        | `LorieBuffer` - shared memory buffer management (AHardwareBuffer, FD, regular memory)                                         |
| `buffer.h`        | Buffer API definitions                                                                                                        |
| `activity.c`      | JNI methods for `LorieView` class. Handles input events, clipboard, surface changes                                           |
| `clipboard.c`     | Clipboard sync between Android and X11                                                                                        |
| `lorie.h`         | Main header with shared state, event types, keycode mappings                                                                  |
| `shm/shmem.c`     | Shared memory implementation for Android                                                                                      |
| `fbconfigs.h`     | Framebuffer configurations for GLX                                                                                            |

### Key Architecture Pattern: **Dual-Process Communication**

The X server and the Android renderer run as **separate threads/processes** communicating via:

1. **Unix domain socket pair** (conn_fd) - for events
2. **Shared memory** (memfd/ashmem) - for `lorie_shared_server_state`
3. **AHardwareBuffer/FD passing** - for GPU textures (via `ancil_send_fd`)

### X Server Integration

The X server is built from the `xserver/` submodule with these major components:

- **dix/**: Device-Independent X (core protocol)
- **fb/**: Framebuffer implementation
- **mi/**: Machine-Independent helpers
- **os/**: OS abstraction layer
- **randr/**: Resize and Rotate extension
- **render/**: Render extension (compositing)
- **xfixes/**, **damageext/**, **composite/**, **present/**: Modern X extensions
- **glx/**, **exa/**: GLX and EXA acceleration
- **dri3/**: DRI3 direct rendering

The `lorie/` backend replaces the standard Xorg DDX:

- `InitOutput()` → `lorieScreenInit()`
- `InitInput()` → `lorieMouse`, `lorieKeyboard`, `lorieTouch`
- Screen buffer → `LorieBuffer` backed by AHardwareBuffer or shared memory

## 4. JNI Interface

### Java → Native (LorieView native methods in `activity.c`)

```java
// In LorieView.java:
native void nativeInit();
native void surfaceChanged(Surface surface);
native void setViewport(int x, int y, int w, int h, int ew, int eh);
native void setFiltering(int filtering);
static native void connect(int fd);
static native boolean connected();
static native void sendWindowChange(int w, int h, int framerate, String name);
native void sendMouseEvent(float x, float y, int button, boolean down, boolean relative);
native void sendTouchEvent(int action, int id, int x, int y);
native void sendStylusEvent(...);
native boolean sendKeyEvent(int scanCode, int keyCode, boolean down, int flags);
native void sendTextEvent(byte[] text);
```

### Native → Java (callbacks)

- `setClipboardText(String)` - X11 clipboard → Android
- `requestClipboard()` - Android clipboard → X11
- `clientConnectedStateChanged()` - connection status UI update
- `resetIme()` - reset input method editor

### Entry Point Flow

1. `CmdEntryPoint.main()` (Java) loads `libXlorie.so`
2. `JNI_OnLoad()` registers all native methods, starts renderer thread
3. `Java_com_termux_x11_CmdEntryPoint_start()` → creates X server thread → `dix_main()`
4. X server initializes via `lorieScreenInit()` and `InitInput()`
5. Android Activity connects via socket pair to X server

## 5. X11 Rendering on Android Surfaces

### The Rendering Pipeline

```
X11 root window pixmap
  ↓ (EXA/FB driver)
LorieBuffer (AHardwareBuffer or shared memory FD)
  ↓ (Unix socket, passed to renderer)
Renderer thread (GLES2)
  ↓ (eglCreateImageKHR → glEGLImageTargetTexture2DOES)
Android SurfaceView (ANativeWindow/EGLSurface)
```

### Key Mechanism: `lorie_shared_server_state`

A shared memory structure containing:

- `pthread_mutex_t lock` - synchronizes X server drawing vs renderer reading
- `pthread_cond_t cond` - wakes renderer when new frame is ready
- `rootWindowTextureID` - ID of current buffer to render
- `drawRequested` - flag: X server has new content
- `surfaceAvailable` - flag: Android surface is valid
- `cursor` - cursor bitmap, position, hot spot

### Renderer Thread (`renderer.c`)

1. Initializes EGL on a default AImageReader surface (1x1 pixel)
2. Waits on `stateCond` for work
3. When `drawRequested` or `cursor.moved/updated`:
   - Locks `state->lock` (blocks X server from writing)
   - Binds `LorieBuffer` as GL texture
   - Draws full-screen quad with texture
   - Draws cursor as overlay (blended)
   - Unlocks `state->lock`
   - `eglSwapBuffers()`
4. FPS counter logged every 5 seconds

### Buffer Types (`LorieBuffer`)

| Type                          | Usage                                           |
| ----------------------------- | ----------------------------------------------- |
| `LORIEBUFFER_REGULAR`         | Regular malloc'd memory (fallback)              |
| `LORIEBUFFER_FD`              | Shared memory file descriptor (legacy/fallback) |
| `LORIEBUFFER_AHARDWAREBUFFER` | GPU-shared DMA buffer (preferred, zero-copy)    |

## 6. Input Handling

### Input Devices (from `InitInput.c`)

- `lorieMouse` - 10 buttons, 4 axes (X, Y, HScroll, VScroll)
- `lorieTouch` - 20 touchpoints, absolute coordinates
- `lorieKeyboard` - Standard XI keyboard
- `loriePen` / `lorieEraser` - Stylus (6 axes: X, Y, pressure, tilt_x, tilt_y, orientation)

### Event Flow

```
Android MotionEvent/KeyEvent
  ↓ (Java MainActivity/TouchInputHandler)
LorieView.sendMouseEvent/sendTouchEvent/sendKeyEvent
  ↓ (JNI → activity.c)
Unix socket write (lorieEvent)
  ↓ (X server thread → cmdentrypoint.c → handleLorieEvents)
QueuePointerEvents/QueueKeyboardEvents/QueueTouchEvents
  ↓ (X server input queue)
XI2 events to X clients
```

### Key Code Mapping

Android keycodes are mapped to Linux input event codes via `android_to_linux_keycode[]` array in `lorie.h`, then +8 offset for X11 keycodes.

## 7. Key Directories and Purposes

```
termux-x11/
├── app/src/main/
│   ├── cpp/
│   │   ├── CMakeLists.txt          # Native build entry
│   │   ├── lorie/                  # Android X11 DDX backend
│   │   │   ├── cmdentrypoint.c     # X server startup
│   │   │   ├── InitOutput.c        # Screen/RandR/Present/DRI3
│   │   │   ├── InitInput.c         # Input devices
│   │   │   ├── renderer.c          # GLES2 rendering thread
│   │   │   ├── buffer.c/h          # Shared GPU/CPU buffers
│   │   │   ├── activity.c          # LorieView JNI bindings
│   │   │   └── lorie.h             # Main definitions
│   │   ├── recipes/                # CMake recipes for deps
│   │   ├── patches/                # Patches applied to submodules
│   │   └── xserver/                # Xorg X server source
│   ├── java/com/termux/x11/
│   │   ├── MainActivity.java       # Main Android activity
│   │   ├── LorieView.java          # SurfaceView + JNI calls
│   │   ├── CmdEntryPoint.java      # X server launcher (CLI or service)
│   │   ├── input/                  # Touch/mouse/keyboard input handling
│   │   └── utils/                  # Helpers (fullscreen, extra keys, etc.)
│   └── res/                        # Android UI resources
├── shell-loader/                   # Separate module for shell integration
├── wayland/                        # Wayland repos (NEW - not yet integrated)
└── build_termux_package            # Script to build .deb package
```

## 8. Critical Observations for Wayland Integration

### Current State: Pure X11

- The entire rendering pipeline is built around X11's `Pixmap`, `Screen`, `Damage`, `Present`, and `EXA` abstractions
- The `lorie/` backend is a full Xorg DDX (like xf86-video-fbdev but for Android)
- Input is handled through X server's `mieq` (Machine-Independent Event Queue)

### What Would Need to Change for Wayland

**Option A: Add Wayland alongside X11 (Dual Server)**

- Create a new `wayland/` backend directory
- Implement a Wayland compositor using `wayland/` submodules:
  - `wayland/wayland` - core Wayland library
  - `wayland/wayland-protocols` - standard protocols
  - `wayland/weston` - reference compositor (for code reuse)
- Share the same GLES2 renderer (`renderer.c`) but with Wayland surface protocol
- Wayland compositor would manage `wl_surface` → `LorieBuffer` → Android Surface
- Much of `activity.c` JNI could be reused for input events

**Option B: Run XWayland under a Wayland Compositor**

- Build `wayland/weston` as the compositor
- Run X server as XWayland client
- Weston would render to Android SurfaceView
- Requires Weston backend for Android (no DRM/KMS)

**Option C: Convert to Pure Wayland Compositor**

- Replace entire X server with a Wayland compositor
- Run X apps via XWayland
- Most complex option, essentially a new project

### Reusable Components

| Component                                | Reusability                                             |
| ---------------------------------------- | ------------------------------------------------------- |
| `buffer.c` (LorieBuffer/AHardwareBuffer) | **High** - buffer management is display-server agnostic |
| `renderer.c` (GLES2 texture renderer)    | **High** - just needs different surface source          |
| `activity.c` (JNI event bridge)          | **Medium** - event protocol needs Wayland equivalents   |
| `InitInput.c` (input devices)            | **Low** - X-specific input API                          |
| `InitOutput.c` (screen/RandR)            | **Low** - deeply X11-specific                           |
| EXA/Present/DRI3                         | **None** - X11-only abstractions                        |

## 9. Important Build Notes

1. **Patches**: Several submodules are patched during build:
   - `patches/xserver.patch` - X server modifications for Android
   - `patches/libepoxy.patch` - GL dispatch patches
   - `patches/pixman.patch` - Pixel manipulation patches

2. **GLX**: GLX is stubbed out (`glXDRIscreenProbe` returns a minimal software raster config). No real GPU acceleration for X11 clients.

3. **DRI3**: DRI3 is enabled but works through `loriePixmapFromFds()` which imports AHardwareBuffers or FDs into pixmaps.

4. **Shared Memory**: Uses `ASharedMemory_create()` (Android 8+) with fallback to `memfd_create` or `/dev/ashmem`.

5. **Mutex Recovery**: Bionic doesn't support robust mutexes, so `lorie_mutex_lock()` has a timeout-based recovery mechanism that reinitializes stuck mutexes.
