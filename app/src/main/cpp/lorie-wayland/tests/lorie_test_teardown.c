/* Lorie Wayland Compositor — Reusable Safe-Teardown Test Helpers
 *
 * Implements the "Reusable safe-teardown test primitive" requirement of the
 * compositor-teardown-safety spec (input-focus-uaf-teardown-race change,
 * Phase 2). See lorie_test_teardown.h for the full rationale and the join-
 * without-destroy-clients design note (calling the literal
 * lorie_compositor_stop here would double-destroy the caller's client/surface
 * via wl_display_destroy_clients before this helper got to it).
 */

#include "lorie_test_teardown.h"

#include <stdatomic.h>

/* Mirrors lorie_compositor_stop's join step ONLY (compositor.c:538-543):
 *   if (!c || !atomic_load(&c->running)) return;
 *   atomic_store(&c->running, 0);
 *   pthread_join(c->event_loop_thread, NULL);
 * After this returns, c->running == false and event_loop_thread has fully
 * exited — the documented-safe precondition for any internal teardown. The
 * guard's predicate (c && atomic_load(&c->running) && !pthread_equal(...))
 * short-circuits on the now-false `running` regardless of which thread calls
 * the subsequent destroy — exactly the "post-stop() teardown after join"
 * no-false-positive case the spec and design enumerate. */
static void lorie_test_join_event_loop(struct lorie_compositor *c) {
    if (!c || !atomic_load(&c->running))
        return;
    atomic_store(&c->running, 0);
    pthread_join(c->event_loop_thread, NULL);
}

void lorie_test_safe_destroy_client(struct lorie_compositor *c, struct wl_client *client) {
    lorie_test_join_event_loop(c);
    if (client)
        wl_client_destroy(client);
}

void lorie_test_safe_destroy_surface(struct lorie_compositor *c, struct lorie_surface *s) {
    lorie_test_join_event_loop(c);
    lorie_surface_destroy_internal(s);
}
