# Compositor Teardown Safety Specification

## Purpose

The compositor's internal teardown API (`lorie_surface_destroy_internal`,
`wl_client_destroy` on compositor-owned clients, and siblings — documented in
`compositor.h` as "exposed for tests") mutates `wl_client`/`wl_resource`/
`wl_display` state that the running event-loop thread concurrently dispatches
and flushes. Calling this API from any other thread while the loop runs is a
data race that corrupts heap state non-deterministically (observed as
`wl_client_destroy -> wl_map_release -> wl_array_release` crashes and
"re-entrant client destruction" log lines). This capability defines the
observable safety contract that makes that hazard impossible to trigger
silently: either teardown is provably ordered so the race cannot occur, or any
attempt to bypass that ordering fails loudly and immediately instead of
corrupting memory.

## Requirements

### Requirement: Stop-then-destroy ordering on compositor shutdown

The compositor MUST guarantee that, when stopped via `lorie_compositor_stop`,
the event-loop thread has fully exited (joined) before any client or surface
is destroyed, so client/surface teardown cannot race the running event/render
loop's traversal of `wl_client`/`wl_resource`/`display->client_list` state.

#### Scenario: Stop joins the event-loop thread before destroying clients

- GIVEN a running compositor with at least one connected client and a live
  surface
- WHEN `lorie_compositor_stop` is called
- THEN the event-loop thread is joined (fully exited) BEFORE any client is
  destroyed
- AND no client or surface teardown executes while the loop is still running

#### Scenario: Stop is a no-op on an already-stopped compositor

- GIVEN a compositor that is not currently running
- WHEN `lorie_compositor_stop` is called
- THEN the call returns without joining a thread or destroying clients again
- AND no guard trips and no diagnostic is emitted

### Requirement: Cross-thread unsafe-teardown detection

The compositor MUST detect when an internal teardown entry point
(`lorie_surface_destroy_internal`, or a direct `wl_client_destroy` of a
compositor-owned client reached through that internal API) is invoked while
the compositor is running (`c->running == true`) from a thread that is NOT the
registered event-loop thread, and MUST fail loudly — with an assertion or
abort and a clear diagnostic identifying the violated contract — rather than
allow the call to proceed and silently corrupt heap state.

#### Scenario: Cross-thread destroy while running trips the guard

- GIVEN a running compositor whose event-loop thread is alive and dispatching
- WHEN a thread other than the event-loop thread calls
  `lorie_surface_destroy_internal` (or the equivalent direct `wl_client_destroy`
  internal-teardown path) on a compositor-owned surface/client
- THEN the compositor detects the violation and aborts/asserts immediately,
  before any client/resource state is mutated
- AND the emitted diagnostic clearly names the contract being violated (e.g.
  identifies that internal teardown was invoked from a non-event-loop thread
  while the compositor is running) so the failure is unambiguous to a developer
  reading the log or crash report

### Requirement: No false positives on legitimate teardown paths

The cross-thread unsafe-teardown guard MUST allow teardown to proceed normally,
without tripping or emitting any diagnostic, for the two paths that are known
to be safe by construction: a real client-initiated destroy processed on the
event-loop thread while the compositor is running, and internal teardown
invoked during the creation-time error path before the event loop has started.

#### Scenario: Client-initiated destroy on the event-loop thread proceeds untouched

- GIVEN a running compositor whose event-loop thread is alive
- WHEN a connected client sends `wl_surface.destroy` (or otherwise triggers
  resource destruction) and the resulting teardown executes on the event-loop
  thread itself
- THEN teardown proceeds to completion exactly as before
- AND the guard does not trip and emits no diagnostic

#### Scenario: Creation-time error-path teardown before the loop starts proceeds untouched

- GIVEN a compositor instance for which the event loop has not yet started
  (`c->running == false`), in the middle of `lorie_surface_create_internal`'s
  error-handling path
- WHEN that error path calls `lorie_surface_destroy_internal` to clean up the
  partially-created surface
- THEN teardown proceeds to completion exactly as before
- AND the guard does not trip and emits no diagnostic

### Requirement: Reusable safe-teardown test primitive

The test infrastructure MUST provide a reusable safe-teardown helper that
performs the documented-safe sequence (stop the compositor / join the
event-loop thread, THEN destroy the client) so individual tests do not need to
reimplement that ordering by hand, and so that using the helper is sufficient
to avoid the cross-thread teardown race entirely.

#### Scenario: Helper tears down a focused surface's client without racing the loop

- GIVEN a running compositor with a surface that currently holds
  pointer/keyboard focus, and a test that needs to destroy that surface's
  client
- WHEN the test uses the shared safe-teardown helper to destroy the client
  instead of calling `lorie_surface_destroy_internal`/`wl_client_destroy`
  directly while the loop runs
- THEN the helper joins the event-loop thread before performing the
  destruction, exactly mirroring `lorie_compositor_stop`'s ordering
- AND no data race between the destroying thread and the event-loop thread is
  observable (the sequence is clean under ThreadSanitizer)
- AND the test's focus-cleared assertions still hold after teardown completes

#### Scenario: Helper-based teardown leaves the guard untripped

- GIVEN a test that uses the shared safe-teardown helper to destroy a
  compositor-owned client
- WHEN the helper performs its stop-then-destroy sequence
- THEN the cross-thread unsafe-teardown guard does not trip and emits no
  diagnostic, because teardown only happens after the event-loop thread has
  exited
