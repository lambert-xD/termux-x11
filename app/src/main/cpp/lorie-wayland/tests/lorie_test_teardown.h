#ifndef LORIE_TEST_TEARDOWN_H
#define LORIE_TEST_TEARDOWN_H

#include "compositor.h"

/* Reusable safe-teardown test primitives (compositor-teardown-safety spec,
 * "Reusable safe-teardown test primitive" requirement).
 *
 * lorie_surface_destroy_internal / wl_client_destroy on a compositor-owned
 * client mutate wl_client/wl_resource/wl_display state that the running
 * event-loop thread concurrently dispatches and flushes. Calling either while
 * the loop is alive from any thread but the loop itself is the cross-thread
 * UAF race this whole change exists to make impossible. These helpers own the
 * documented-safe ordering — JOIN the event-loop thread first (so it has
 * provably exited and can no longer touch client/resource/display state),
 * THEN destroy — so individual tests never have to reimplement it by hand and
 * the guard (lorie_compositor_assert_event_loop_thread) never trips on a
 * helper-based teardown (running == false at destroy time -> predicate
 * short-circuits to "no trip").
 *
 * Mirrors lorie_compositor_stop's join step (compositor.c:538-543:
 * atomic_store(&c->running, 0); pthread_join(c->event_loop_thread, NULL);)
 * WITHOUT also calling wl_display_destroy_clients — that would destroy the
 * very client/surface this helper is asked to destroy out from under it
 * (double-destroy / use-after-free on the caller's pointer). The helper joins
 * the loop, THEN destroys exactly the one client/surface the caller named —
 * nothing more, nothing less. */

/* Stop the event loop (join — same ordering as lorie_compositor_stop, minus
 * the wl_display_destroy_clients side effect), THEN destroy `client`.
 * No-op-safe on a NULL client; idempotent w.r.t. an already-stopped
 * compositor (mirrors lorie_compositor_stop's own running-check). */
void lorie_test_safe_destroy_client(struct lorie_compositor *c, struct wl_client *client);

/* Stop the event loop (same join as above), THEN destroy surface `s` via
 * lorie_surface_destroy_internal. Use when a surface must be torn down
 * directly (e.g. server-owned surfaces with s->resource == NULL) rather than
 * through its client's resource-destroy chain. */
void lorie_test_safe_destroy_surface(struct lorie_compositor *c, struct lorie_surface *s);

#endif /* LORIE_TEST_TEARDOWN_H */
