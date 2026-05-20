# Wayland Compositor Test Plan for Termux:X11

## Overview

This document outlines the testing strategy for the new Wayland compositor integration in Termux:X11.

## Test Environment Setup

### Prerequisites

- Android device with Android 8+ (API 26+)
- Termux app installed
- Wayland client applications installed in Termux

### Build Steps

```bash
# Clone with submodules
git clone --recurse-submodules https://github.com/termux/termux-x11
cd termux-x11

# Build the project
./gradlew assembleDebug

# Install APK
adb install app/build/outputs/apk/debug/app-arm64-v8a-debug.apk
```

## Test Scenarios

### 1. Basic Compositor Startup

**Objective**: Verify the Wayland compositor starts correctly.

**Steps**:

1. Launch Termux:X11 app
2. Select "Wayland Mode" in preferences
3. Start the compositor

**Expected Results**:

- Compositor starts without crashes
- Logcat shows: "Wayland compositor started successfully"
- Wayland socket created at `$TMPDIR/wayland/wayland-0`

### 2. Wayland Client Connection

**Objective**: Verify Wayland clients can connect to the compositor.

**Test Clients**:

- `weston-terminal` (from weston package)
- `foot` (Wayland-native terminal)
- `gtk3-demo` (GTK3 Wayland demo)

**Steps**:

```bash
# In Termux shell
export WAYLAND_DISPLAY=wayland-0
weston-terminal
```

**Expected Results**:

- Client window appears on screen
- No connection errors in logcat

### 3. XWayland Integration

**Objective**: Verify X11 applications work via XWayland.

**Steps**:

1. Start compositor with XWayland enabled (`--xwayland` flag)
2. Run X11 client:

```bash
export DISPLAY=:0
xterm
```

**Expected Results**:

- XWayland starts successfully
- X11 client window appears
- Window decorations/management work

### 4. Input Handling

#### 4.1 Touch Input

**Steps**:

1. Start any Wayland client
2. Touch the screen

**Expected Results**:

- Touch events registered in compositor
- Client receives touch events

#### 4.2 Mouse/Trackpad

**Steps**:

1. Connect mouse/trackpad via OTG
2. Move cursor and click

**Expected Results**:

- Cursor visible and moves
- Click events work

#### 4.3 Keyboard

**Steps**:

1. Connect physical keyboard via OTG
2. Type in a terminal client

**Expected Results**:

- Key events transmitted
- Characters appear in client

#### 4.4 Stylus

**Steps**:

1. Use stylus on screen
2. Test pressure sensitivity

**Expected Results**:

- Stylus events work
- Pressure data transmitted (if supported by client)

### 5. Output Configuration

**Objective**: Test output resolution and scaling.

**Steps**:

```bash
# Start with custom resolution
termux-wayland --width 1920 --height 1080 --scale 1.5
```

**Expected Results**:

- Output configured to specified resolution
- Scaling applied correctly
- Clients render at correct size

### 6. Multi-Window Support

**Objective**: Test multiple Wayland clients simultaneously.

**Steps**:

1. Start compositor
2. Open multiple clients:

```bash
weston-terminal &
foot &
gtk3-demo &
```

**Expected Results**:

- All clients visible
- Focus switching works
- No rendering artifacts

### 7. Clipboard Integration

**Objective**: Test clipboard between Android and Wayland clients.

**Steps**:

1. Copy text from Android app
2. Paste in Wayland client
3. Copy text in Wayland client
4. Paste in Android app

**Expected Results**:

- Clipboard sync works bidirectionally

### 8. Performance Testing

**Objective**: Verify compositor performance.

**Metrics**:

- Frame rate (target: 60 FPS)
- Input latency (target: <16ms)
- Memory usage

**Tools**:

```bash
# Monitor FPS in logcat
adb logcat -s gles-renderer:D

# Monitor memory
adb shell dumpsys meminfo com.termux.x11
```

### 9. Stress Testing

**Objective**: Test compositor stability under load.

**Steps**:

1. Open 10+ clients
2. Rapid focus switching
3. Resize operations
4. Long-running test (24 hours)

**Expected Results**:

- No crashes
- No memory leaks
- Consistent performance

### 10. Error Handling

**Objective**: Verify graceful error handling.

**Test Cases**:

- Invalid socket paths
- Missing WAYLAND_DISPLAY
- Client crashes
- Comitor restart

**Expected Results**:

- Graceful error messages
- Automatic recovery where possible
- No system crashes

## Regression Testing

### X11 Mode

**Objective**: Ensure X11 mode still works.

**Steps**:

1. Switch to X11 mode
2. Run existing X11 clients
3. Verify all features work

**Expected Results**:

- No regressions in X11 mode
- Same performance as before

## Test Checklist

- [ ] Basic startup
- [ ] Client connection (weston-terminal)
- [ ] Client connection (foot)
- [ ] Client connection (GTK3)
- [ ] XWayland startup
- [ ] X11 client via XWayland
- [ ] Touch input
- [ ] Mouse input
- [ ] Keyboard input
- [ ] Stylus input
- [ ] Custom resolution
- [ ] Scaling
- [ ] Multi-window
- [ ] Clipboard Android→Wayland
- [ ] Clipboard Wayland→Android
- [ ] Performance 60 FPS
- [ ] 24-hour stability
- [ ] X11 regression test

## Known Limitations

1. **XWayland**: Requires X server submodule to be built
2. **Hardware acceleration**: Limited by Android GPU drivers
3. **Multi-output**: Single output only (Android limitation)
4. **Seat management**: Basic seat implementation

## Debug Information

### Log Tags

- `LorieWayland`: Main compositor logs
- `gles-renderer`: Renderer logs
- `LorieNative`: JNI bridge logs

### Debug Build

```bash
export TERMUX_X11_DEBUG=1
./gradlew assembleDebug
```

### Useful ADB Commands

```bash
# View compositor logs
adb logcat -s LorieWayland:D

# View all logs
adb logcat | grep -i wayland

# Check Wayland socket
adb shell ls -la /data/data/com.termux/files/usr/tmp/wayland/

# Monitor processes
adb shell ps | grep wayland
```

## Reporting Issues

When reporting issues, include:

1. Android version and device model
2. Termux:X11 version
3. Wayland client used
4. Logcat output
5. Steps to reproduce
