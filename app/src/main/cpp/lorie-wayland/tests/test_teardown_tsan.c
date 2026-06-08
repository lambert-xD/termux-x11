/* Lorie Wayland Compositor — TSan Cross-Thread Teardown Race Repro
 *
 * compositor-teardown-safety / input-focus-uaf-teardown-race, Phase 4
 * ("Race-Repro Target", design D4).
 *
 * This is intentionally NOT a CTest-framework test (it links no test
 * framework, registers no suite). It is a tiny, focused standalone binary
 * compiled ONLY with -fsanitize=thread -fsanitize=undefined, built and run
 * by a dedicated CTest entry whose PASS/FAIL criterion is the *sanitizer's*
 * exit status, not an internal assertion count:
 *
 *   - "unsafe" mode (default; argv[1] absent or "unsafe"): runs a tight
 *     create -> focus -> destroy-while-running loop using the ORIGINAL
 *     unsafe pattern this whole change exists to eliminate — calls
 *     lorie_surface_destroy_internal()/wl_client_destroy() directly on the
 *     calling (non-loop) thread while c->running is true, racing the
 *     autonomous event-loop thread's concurrent dispatch/flush of the very
 *     same client/resource/display state. TSan is expected to report a data
 *     race on FIRST occurrence (RED — confirms the unsafe pattern genuinely
 *     races, not merely "is discouraged").
 *
 *   - "safe" mode (argv[1] == "safe"): runs the IDENTICAL create -> focus ->
 *     destroy loop but tears down via lorie_test_safe_destroy_client (join
 *     event-loop thread first, THEN destroy — the documented stop-then-
 *     destroy contract). TSan is expected to report a CLEAN run (GREEN —
 *     confirms the helper sequence is genuinely race-free, not merely
 *     "passes the guard").
 *
 * LORIE_TEARDOWN_GUARD is intentionally left UNDEFINED for this target: an
 * abort() mid-race would short-circuit TSan's race detection window and
 * produce a SIGABRT instead of a TSan report — the opposite of what this
 * target needs to demonstrate (the guard's job is to convert the race into
 * a loud deterministic failure in normal builds; THIS target's job is to
 * let TSan observe and report the underlying race directly, contrasted with
 * its absence on the safe path). See tests/CMakeLists.txt for the dedicated
 * (non-LORIE_TEARDOWN_GUARD) compile/link wiring.
 */

#include "compositor.h"
#include "input.h"
#include "lorie_test_teardown.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Mirrors test_input.c's make_client(c, &peer_fd) idiom (engram bugfix
 * #180): defer close(peer_fd) until after the client is fully, safely
 * destroyed, so the autonomous WL_EVENT_HANGUP-driven reap never races this
 * repro's own explicit destroy on the same pointer — keeping the race this
 * target demonstrates EXACTLY the cross-thread teardown-ordering race the
 * spec describes, not an unrelated fixture race. */
static struct wl_client *make_client(struct lorie_compositor *c, int *peer_fd) {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0)
        return NULL;
    struct wl_client *client = wl_client_create(c->display, fds[0]);
    if (!client) {
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }
    *peer_fd = fds[1];
    return client;
}

/* One create -> focus -> destroy cycle.
 * unsafe=1: raw lorie_surface_destroy_internal + wl_client_destroy on THIS
 *           (non-loop) thread while c->running is true — the documented
 *           UAF-racing pattern (cross-thread mutation of wl_client/
 *           wl_resource/wl_display state the loop thread concurrently
 *           dispatches and flushes).
 * unsafe=0: lorie_test_safe_destroy_client — join the loop thread first
 *           (provably exited, can no longer touch this state), THEN
 *           destroy. The documented-safe stop-then-destroy ordering. */
static void run_cycle(struct lorie_compositor *c, struct lorie_input *in, int unsafe) {
    int peer_fd = -1;
    struct wl_client *client = make_client(c, &peer_fd);
    if (!client) {
        fprintf(stderr, "make_client failed\n");
        _exit(2);
    }

    struct lorie_surface *s = lorie_surface_create_internal(c, client, 0);
    if (!s) {
        fprintf(stderr, "lorie_surface_create_internal failed\n");
        _exit(2);
    }
    s->x = 0; s->y = 0; s->logical_width = 100; s->logical_height = 100;

    /* Establish focus so the destroy path exercises
     * lorie_input_clear_focus_for_surface (the exact mutation the spec's
     * "observed as ... crashes" focus-teardown race targets) — mirrors
     * test_focus_cleared_on_surface_destroy's shape, BUT routes through the
     * thread-safe enqueue API only (lorie_input_pointer_motion -> enqueue(),
     * which is mutex-protected — see input.c) and lets the ALREADY-RUNNING
     * loop's own self-rescheduling 16ms timer
     * (wl_event_loop_add_timer(loop, lorie_input_dispatch, in) in
     * lorie_input_init) drain it, instead of calling lorie_input_dispatch()
     * directly from this (non-loop) thread.
     *
     * lorie_input_dispatch is registered AS the timer callback and is
     * documented-by-construction to run only on the event-loop thread (it
     * unconditionally self-reschedules via wl_event_source_timer_update at
     * its own tail — see input.c:295). Calling it directly from here would
     * race the loop thread's own concurrent invocation of the very same
     * function on the same struct lorie_input* — a test-harness-induced
     * pframe()/pointer_dirty data race that is a DIFFERENT bug class than
     * the cross-thread teardown race this target exists to demonstrate
     * (confirmed by direct experimentation: an earlier draft that called
     * lorie_input_dispatch(in) here made TSan report exactly that race,
     * input.c:78 pframe vs input.c:237 lorie_input_dispatch, NOT a
     * destroy-path race — a self-inflicted API-misuse race in the repro
     * itself, not a production finding). A short sleep gives the loop's own
     * timer >=1 tick (16ms) to drain the queue and update pointer_focus
     * through the single, correct, thread-safe path. */
    lorie_input_pointer_motion(in, 50.0f, 50.0f);
    struct timespec wait_for_dispatch = {.tv_sec = 0, .tv_nsec = 50 * 1000 * 1000L};
    nanosleep(&wait_for_dispatch, NULL);

    if (unsafe) {
        /* THE UNSAFE PATTERN — cross-thread destroy while running.
         * lorie_compositor_assert_event_loop_thread is a no-op here
         * (LORIE_TEARDOWN_GUARD undefined for this target — see file header)
         * so this races the loop thread for real instead of aborting first. */
        lorie_surface_destroy_internal(s);
        wl_client_destroy(client);
    } else {
        /* THE SAFE PATTERN — join, then destroy. */
        lorie_test_safe_destroy_client(c, client);
    }

    if (peer_fd >= 0)
        close(peer_fd);
}

int main(int argc, char **argv) {
    int unsafe = 1;
    if (argc > 1 && strcmp(argv[1], "safe") == 0)
        unsafe = 0;

    fprintf(stderr, "[tsan-repro] mode=%s\n", unsafe ? "unsafe" : "safe");

    /* Tight loop: TSan reports a race on FIRST occurrence (no need to spin
     * thousands of iterations like a probabilistic crash-loop — design D4
     * explicitly rejects "crash-loop-N" as probabilistic). A handful of
     * cycles is enough to give the scheduler genuine interleaving
     * opportunities across the create/focus/destroy/recreate boundary. */
    for (int i = 0; i < 8; i++) {
        struct lorie_compositor *c = lorie_compositor_create();
        if (!c) {
            fprintf(stderr, "lorie_compositor_create failed\n");
            return 2;
        }
        if (lorie_compositor_start(c) != 0) {
            fprintf(stderr, "lorie_compositor_start failed\n");
            return 2;
        }
        struct lorie_input *in = c->input;

        run_cycle(c, in, unsafe);

        /* lorie_compositor_destroy internally stops (joins) if still
         * running — safe regardless of which path run_cycle took (mirrors
         * the idempotent running-check both lorie_compositor_stop and
         * lorie_test_safe_destroy_client rely on). */
        lorie_compositor_destroy(c);
    }

    fprintf(stderr, "[tsan-repro] completed %d cycles without crashing\n", 8);
    return 0;
}
