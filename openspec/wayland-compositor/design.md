# Design: Native Wayland Compositor for Android

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    Android App (Java/Kotlin)                 │
│  ┌─────────────────┐  ┌──────────────────┐                  │
│  │ WaylandActivity │  │ MainActivity (X11)│                 │
│  └────────┬────────┘  └──────────────────┘                  │
│           │                                                 │
│  ┌────────▼────────┐                                        │
│  │ LorieWaylandView│ ← SurfaceView + JNI                    │
│  └────────┬────────┘                                        │
└───────────┼─────────────────────────────────────────────────┘
            │ JNI
┌───────────▼─────────────────────────────────────────────────┐
│                   Native Layer (C/C++)                       │
│  ┌─────────────────────────────────────────────────────────┐│
│  │              lorie-wayland/ Compositor                   ││
│  │  ┌─────────┐  ┌──────────┐  ┌─────────┐  ┌──────────┐  ││
│  │  │ compositor│  │ output   │  │ surface │  │ seat     │  ││
│  │  │  core   │  │ (screen) │  │  mgmt   │  │ (input)  │  ││
│  │  └────┬────┘  └────┬─────┘  └────┬────┘  └────┬─────┘  ││
│  │       └─────────────┴─────────────┴────────────┘        ││
│  │                         │                               ││
│  │  ┌──────────────────────▼──────────────────────────┐    ││
│  │  │                  renderer.c                      │    ││
│  │  │  GLES2 multi-surface compositor → ANativeWindow │    ││
│  │  └─────────────────────────────────────────────────┘    ││
│  │                         │                               ││
│  │  ┌──────────────────────▼──────────────────────────┐    ││
│  │  │              wayland-activity.c                  │    ││
│  │  │     JNI bridge (Java events → C input)          │    ││
│  │  └─────────────────────────────────────────────────┘    ││
│  │                         │                               ││
│  │  ┌──────────────────────▼──────────────────────────┐    ││
│  │  │   xwayland.c  ←  X11 compatibility layer         │    ││
│  │  └─────────────────────────────────────────────────┘    ││
│  └─────────────────────────────────────────────────────────┘│
│                           │                                  │
│  ┌────────────────────────▼──────────────────────────────┐  │
│  │           libwayland-server (submodule)                │  │
│  │              wl_display, wl_event_loop                 │  │
│  └────────────────────────────────────────────────────────┘  │
│                           │                                  │
│  ┌────────────────────────▼──────────────────────────────┐  │
│  │           Protocols (generated from XML)               │  │
│  │   xdg-shell, linux-dmabuf, pointer-constraints, ...    │  │
│  └────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘

Reused Components (from existing X11 backend):
- LorieBuffer (buffer.c/h) — AHardwareBuffer, FD, shared memory
- GLES2 shader code (adapted from renderer.c)
- android_to_linux_keycode[] mapping
- JNI event patterns
- Shared memory / mutex patterns
```

## Threading Model

| Thread             | Responsibility                                                  |
| ------------------ | --------------------------------------------------------------- |
| Android Main       | UI events, SurfaceView lifecycle, JNI calls                     |
| Wayland Event Loop | `wl_event_loop` — client connections, protocol dispatch         |
| Renderer           | GLES2 compositing, `eglSwapBuffers`, vsync via `AChoreographer` |
| Input Bridge       | Converts Android MotionEvent/KeyEvent → Wayland protocol        |

### Synchronization

- Renderer and Wayland event loop share `lorie_shared_server_state`-style mutex
- `wl_surface` damage regions are protected by compositor lock
- Frame callbacks are dispatched from renderer thread

## Data Flow

```
Wayland Client (cairo, GTK, etc.)
    │
    ▼ wl_surface.attach(wl_buffer)
┌─────────────────┐
│  surface.c      │ ← Creates LorieBuffer from wl_shm or dma-buf
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  renderer.c     │ ← Queues surface for compositing
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  output.c       │ ← eglSwapBuffers to ANativeWindow
└─────────────────┘
```

## Protocol Priority

| Priority | Protocol            | Why                         |
| -------- | ------------------- | --------------------------- |
| P0       | xdg-shell           | Essential for any real app  |
| P0       | wl_shm              | Software rendering fallback |
| P1       | linux-dmabuf        | GPU zero-copy               |
| P1       | wl_data_device      | Clipboard                   |
| P2       | relative-pointer    | Games                       |
| P2       | pointer-constraints | Games                       |
| P3       | idle-inhibit        | Video playback              |

## Build Integration

```
CMake
├── recipes/wayland.cmake → libwayland-server, libwayland-client
├── recipes/wayland-protocols.cmake → generated headers from XML
├── patches/wayland-android.patch → Bionic compatibility
└── lorie-wayland/CMakeLists.txt → compositor static library
```
