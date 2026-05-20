# Proposal: Native Wayland Compositor for Android

## Problem

Termux:X11 is a pure X11 server for Android. Users want to run modern Wayland-native applications (GTK4, Qt6, wlroots-based apps) without relying on XWayland translation, which has performance and compatibility limitations.

## Goal

Add a native Wayland compositor to termux-x11 that coexists with the existing X11 server. Users should be able to choose between X11 mode and Wayland mode at startup.

## Non-Goals

- Do NOT remove or break the existing X11 server
- Do NOT implement a full desktop environment (no window decorations, no panels)
- Do NOT support multiple physical outputs (Android has one screen)
- Do NOT implement every Wayland protocol — only the essential ones

## Success Criteria

1. A Wayland client can connect and display a surface
2. Input events (touch, keyboard, mouse) reach the client
3. Xdg-shell protocol works for toplevel windows
4. The compositor renders to Android SurfaceView via GLES2
5. XWayland can run under the compositor for X11 app compatibility
6. Build succeeds for all Android ABIs (arm64-v8a, armeabi-v7a, x86_64, x86)

## Risks

| Risk                                             | Mitigation                                     |
| ------------------------------------------------ | ---------------------------------------------- |
| Wayland submodules use meson, project uses CMake | Write CMake wrapper recipes                    |
| Bionic lacks some POSIX functions                | Create Android compatibility patches           |
| Large diff risks reviewer burnout                | Split into chained PRs ≤400 lines each         |
| Thread safety between compositor and renderer    | Audit mutex usage, use existing lorie patterns |

## Estimated Scope

~8,000 lines of new code across C, Java, and build files.

## Delivery Strategy

Chained PRs (Feature Branch Chain with tracker) due to size exceeding 400-line budget.
