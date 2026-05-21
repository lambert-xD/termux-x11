# Bidirectional Clipboard Specification

## Purpose

Implement full bidirectional clipboard synchronization between the Android system clipboard and Wayland clients. The Wayland → Android path forwards client selections into `ClipboardManager`. The Android → Wayland path exposes Android clipboard text as a `wl_data_source` selection. Loop prevention ensures that echo updates do not cause infinite ping-pong.

## Requirements

### Requirement: Wayland → Android Forwarding

The system MUST implement `data_offer_receive` to accept `text/plain;charset=utf-8`, create a pipe pair, and forward the received text to Android via JNI.

#### Scenario: Wayland client copies text

- GIVEN a Wayland client has called `wl_data_source_offer("text/plain;charset=utf-8")` and the compositor holds the selection
- WHEN the user pastes on Android
- THEN `ClipboardManager.setPrimaryClip` is called with the Wayland client’s text

#### Scenario: Pipe-based transfer

- GIVEN `data_offer_receive` is invoked with an fd
- WHEN the Wayland client writes text into the offered fd
- THEN a worker thread reads the pipe and forwards the text to Java

#### Scenario: RED test — data_offer_receive is no-op

- GIVEN `data_offer_receive` does nothing
- WHEN a test calls it with a mock fd
- THEN no text is forwarded (RED phase confirms missing implementation)

### Requirement: Android → Wayland Forwarding

The system MUST, upon receiving clipboard bytes from Java (`sendClipboardEvent`), create a `wl_data_source`, offer `text/plain;charset=utf-8`, and set it as the compositor selection.

#### Scenario: Android clipboard change triggers Wayland selection

- GIVEN `sendClipboardEvent` receives bytes from Java
- WHEN the bytes are valid and within size limits
- THEN a `wl_data_source` is created, mime type is offered, and `data_device_set_selection` is called

#### Scenario: Wayland client reads Android clipboard

- GIVEN Android clipboard contains "hello"
- WHEN a Wayland client requests the selection via `wl_data_offer_receive`
- THEN "hello" is written into the fd provided by the client

#### Scenario: RED test — clipboard bytes are freed

- GIVEN `sendClipboardEvent` receives clipboard bytes and immediately frees them
- WHEN a unit test inspects the compositor state
- THEN no `wl_data_source` exists (RED phase confirms no-op before GREEN)

### Requirement: Loop Prevention

The system MUST tag every clipboard update with its origin (`android` or `wayland`) and ignore updates that echo back from the same origin.

#### Scenario: Android → Wayland → Android echo blocked

- GIVEN Android clipboard is updated with text "A"
- WHEN the compositor forwards "A" to Wayland and the Wayland client echoes it back
- THEN the echo update is ignored; `ClipboardManager` is not called again

#### Scenario: Wayland → Android → Wayland echo blocked

- GIVEN a Wayland client sets selection to text "B"
- WHEN the compositor forwards "B" to Android and Android echoes it back
- THEN the echo update is ignored; `wl_data_source` is not recreated

#### Scenario: RED test — no loop prevention

- GIVEN there is no source tagging
- WHEN Android and Wayland clipboards are both updated
- THEN updates ping-pong indefinitely (RED phase confirms missing loop prevention)

### Requirement: Size and Safety Limits

The system MUST reject clipboard payloads larger than 1 MiB and MUST use `calloc` (not VLA) for clipboard buffers.

#### Scenario: Oversized clipboard rejected

- GIVEN a clipboard payload of 2 MiB
- WHEN it is received from either direction
- THEN it is rejected and no memory is allocated for it

#### Scenario: Null-terminated text

- GIVEN a clipboard payload of N bytes
- WHEN it is stored or forwarded
- THEN it is guaranteed to be null-terminated before string operations

## Test Plan

| Test | Location | Type | What It Verifies |
|------|----------|------|------------------|
| `test_clipboard_wayland_to_android` | `test_clipboard.c` (new) | Unit | Wayland selection forwarded to Android via JNI mock |
| `test_clipboard_android_to_wayland` | `test_clipboard.c` (new) | Unit | Android bytes create `wl_data_source` and offer mime type |
| `test_clipboard_read_from_source` | `test_clipboard.c` (new) | Unit | Wayland client fd receives Android clipboard text |
| `test_clipboard_loop_prevention` | `test_clipboard.c` (new) | Unit | Echo updates are ignored in both directions |
| `test_clipboard_size_cap` | `test_clipboard.c` (new) | Unit | Payloads > 1 MiB are rejected |
| `test_clipboard_null_terminated` | `test_clipboard.c` (new) | Unit | All text buffers are null-terminated |
| End-to-end manual test | Device / emulator | Integration | Copy on Android pastes in Wayland client and vice versa |

## Acceptance Criteria

1. `test_clipboard.c` suite passes (RED before implementation, GREEN after).
2. Copying text in a Wayland client makes it available to paste on Android.
3. Copying text on Android makes it available to paste in a Wayland client.
4. Loop updates are suppressed; no infinite ping-pong occurs.
5. Clipboard payloads larger than 1 MiB are rejected.
6. No VLA is used; all clipboard buffers are `calloc`'d.
7. FDs are closed in all success and error paths.
8. Existing keyboard and input tests continue to pass (no regression).
