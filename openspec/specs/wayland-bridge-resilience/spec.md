# Wayland Bridge Resilience Specification

## Purpose

Defines the resilient connection bridge behavior and recovery from Binder failures for the Wayland display server, ensuring uninterrupted sessions during Android process freezes and kills.

## Requirements

### Requirement: Thread-Safe Interface Management

The system MUST manage the connection interface in a thread-safe manner to support dynamic interface updates.

#### Scenario: Injecting interface after death

- GIVEN the connection bridge is running with a null or dead interface
- WHEN a new `WaylandCmdEntryPoint` connects via `startConnectionBridge`
- THEN the system MUST atomically update the interface reference
- AND the background polling thread MUST resume processing using the newly injected interface

### Requirement: Process Kill Recovery

The system MUST detect when the external `app_process` is permanently killed and prepare for a new connection.

#### Scenario: Handling DeadObjectException

- GIVEN the connection bridge is actively polling the Wayland interface
- WHEN a `DeadObjectException` is caught during the Binder call
- THEN the system MUST clear the dead interface reference
- AND the system MUST NOT terminate the background polling thread, allowing it to wait for a new interface

### Requirement: Process Freeze Backoff

The system MUST implement a backoff mechanism to handle temporary unresponsiveness when the OS freezes the `app_process`.

#### Scenario: Handling generic RemoteException

- GIVEN the connection bridge is actively polling the Wayland interface
- WHEN a `RemoteException` (that is NOT a `DeadObjectException`) is caught
- THEN the system MUST sleep for a backoff duration (e.g., 500ms)
- AND the system MUST retry the operation on the next loop iteration without dropping the interface
