/* Lorie Wayland Compositor — Cross-Thread Teardown Guard Tests
 *
 * Covers the compositor-teardown-safety spec's guard requirements
 * (input-focus-uaf-teardown-race change, Phase 1+2):
 *   - Cross-thread destroy while running trips the guard (death-test, RED->GREEN)
 *   - Client-initiated destroy on the event-loop thread proceeds untouched
 *   - Creation-time error-path teardown before the loop starts proceeds untouched
 *   - Helper-based teardown leaves the guard untripped
 *
 * ISOLATION RATIONALE (mirrors test_main_protocols_only.c's precedent for the
 * EXACT SAME crash symptom — see engram bugfixes #163/#164 and the discovery
 * "LORIE_TEARDOWN_GUARD immediately aborts full lorie-wayland-tests binary"):
 * once LORIE_TEARDOWN_GUARD is wired in, the FULL lorie-wayland-tests binary
 * deterministically SIGABRTs on the very first unmigrated unsafe destroy
 * (input::test_pointer_focus_set_on_motion — confirmed via real run, exit 134
 * / WTERMSIG=SIGABRT). That is the CORRECT, designed behavior: it converts the
 * pre-existing intermittent `wl_client_destroy -> wl_map_release ->
 * wl_array_release -> free() on garbage pointer` UAF crash into a deterministic,
 * loud, attributable abort — but it also means the full binary cannot run to
 * completion (and thus cannot prove THIS suite's RED->GREEN cleanly) until
 * Phase 3 migrates every running-compositor direct-destroy onto
 * lorie_test_safe_destroy_client. This suite is registered BOTH here (own
 * isolated runner, see test_teardown_guard_main.c /
 * lorie-wayland-teardown-guard-tests) AND in the full lorie-wayland-tests
 * binary's registration list — zero coverage lost, exactly mirroring the
 * `protocols`/`input` precedent's stated rationale ("both are registered with
 * CTest so no coverage is lost ... until the corruption is independently
 * triaged and fixed" — which, for THIS suite's slice of the corruption, Phase 3
 * of this very change does).
 */

#include "lorie_test.h"
#include "compositor.h"
#include "input.h"
#include "lorie_test_teardown.h"

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static struct lorie_compositor *g_comp = NULL;

static void setup(void) {
    g_comp = lorie_compositor_create();
    ASSERT_NOT_NULL(g_comp);
}

static void teardown(void) {
    if (g_comp) { lorie_compositor_destroy(g_comp); g_comp = NULL; }
}

/* IMPORTANT — do NOT close the peer fd while the client is still in use by a
 * RUNNING compositor (unlike test_input.c's make_client, which closes it
 * immediately): wl_client_connection_data treats a closed peer as
 * WL_EVENT_HANGUP and autonomously calls wl_client_destroy(client) on the
 * event-loop thread (wayland-server.c:379-380) — often on literally the FIRST
 * dispatch after creation, since EOF is already pending. Any later explicit
 * wl_client_destroy(client) from the test thread then races (or, worse, lands
 * strictly AFTER) that autonomous free — a genuine UAF whose "lucky" outcome
 * is reading freed-but-still-list-shaped memory and hitting the
 * "encountered re-entrant client destruction" guard inside wl_client_destroy
 * itself (wayland-server.c:1016-1019); its "unlucky" outcome is the exact
 * `wl_client_destroy -> wl_map_release -> wl_array_release` crash the spec
 * names. CONFIRMED via an actual run that printed that exact log line from
 * this exact fixture shape (real evidence; see engram bugfix "make_client
 * peer-fd-close races autonomous HANGUP client destroy"). The fix: keep the
 * peer fd OPEN (out *peer_fd) for the caller to close ONLY AFTER the client
 * has been explicitly, safely destroyed — eliminating the autonomous-destroy
 * race window for the test's whole lifetime. */
static struct wl_client *make_client(struct lorie_compositor *c, int *peer_fd) {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0) return NULL;
    struct wl_client *client = wl_client_create(c->display, fds[0]);
    if (!client) {
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }
    if (peer_fd)
        *peer_fd = fds[1];
    else
        close(fds[1]); /* Caller doesn't need it (e.g. forked child that _exit()s). */
    return client;
}

/* --- 1.2: RED death-test — cross-thread destroy while running aborts ---
 *
 * Spec scenario "Cross-thread destroy while running trips the guard": from a
 * thread that is NOT the event-loop thread, lorie_surface_destroy_internal on
 * a live surface while c->running is true MUST abort immediately, before any
 * client/resource state mutation, with a diagnostic naming the violated
 * contract.
 *
 * Implemented as a fork()+waitpid death-test (per tasks.md 1.2): the CHILD
 * process builds a running compositor + surface, spawns a plain (non-loop)
 * pthread that calls lorie_surface_destroy_internal on that live surface, and
 * the PARENT asserts the child died by SIGABRT (WIFSIGNALED + WTERMSIG ==
 * SIGABRT) — i.e. the guard fired and abort() ran — rather than exiting
 * normally (which would mean either no guard, or a silent corruption that
 * happened not to crash this run).
 *
 * RED (pre-guard): no abort() call exists -> child exits 0 (or crashes
 * unpredictably from heap corruption, NOT deterministically via SIGABRT) ->
 * WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT is false -> test FAILS.
 * GREEN (post-guard): guard fires LOGE + abort() before any mutation -> child
 * dies by SIGABRT deterministically -> assertion holds -> test PASSES.
 */
struct cross_thread_destroy_ctx {
    struct lorie_compositor *c;
    struct lorie_surface *s;
};

static void *cross_thread_destroy_fn(void *arg) {
    struct cross_thread_destroy_ctx *ctx = arg;
    /* Sanity: this thread must NOT be the event-loop thread — that is the
     * entire point of the repro (the unsafe cross-thread pattern). */
    lorie_surface_destroy_internal(ctx->s);
    return NULL;
}

static void test_cross_thread_destroy_while_running_aborts(void) {
    pid_t pid = fork();
    ASSERT_TRUE(pid >= 0);

    if (pid == 0) {
        /* Child: build a running compositor + live surface, then destroy that
         * surface from a plain pthread (NOT the event-loop thread) while
         * c->running is true — the exact unsafe pattern the guard forbids. */
        struct lorie_compositor *c = lorie_compositor_create();
        if (!c) _exit(2);
        if (lorie_compositor_start(c) != 0) _exit(3);

        struct wl_client *client = make_client(c, NULL);
        if (!client) _exit(4);

        struct lorie_surface *s = lorie_surface_create_internal(c, client, 0);
        if (!s) _exit(5);

        struct cross_thread_destroy_ctx ctx = { .c = c, .s = s };
        pthread_t worker;
        if (pthread_create(&worker, NULL, cross_thread_destroy_fn, &ctx) != 0)
            _exit(6);
        pthread_join(worker, NULL);

        /* If we get here, the guard did NOT abort — the unsafe destroy
         * "succeeded" (i.e. corrupted state without crashing this run).
         * Exit distinctly non-abort so the parent's WIFSIGNALED/SIGABRT
         * assertion fails clearly (RED) instead of accidentally matching. */
        _exit(0);
    }

    /* Parent: wait for the child and assert it died by SIGABRT — the guard
     * fired (LOGE diagnostic + abort()) before any client/resource mutation. */
    int status = 0;
    pid_t waited = waitpid(pid, &status, 0);
    ASSERT_EQ_INT(pid, waited);
    ASSERT_TRUE(WIFSIGNALED(status));
    ASSERT_EQ_INT(SIGABRT, WTERMSIG(status));
}

/* --- 1.6: no-false-positive — on-loop destroy while running does not trip ---
 *
 * Spec scenario "Client-initiated destroy on the event-loop thread proceeds
 * untouched": when teardown executes ON the event-loop thread itself (the
 * real wl_surface.destroy dispatch path) while the compositor is running, the
 * guard must not trip and must emit no diagnostic — teardown proceeds exactly
 * as before.
 *
 * We can't easily force a real client wl_surface.destroy to land mid-test, so
 * we directly exercise the guard's on-loop branch: schedule
 * lorie_surface_destroy_internal to run via wl_event_loop_add_idle, then start
 * the compositor — the idle fires on the FIRST wl_event_loop_dispatch inside
 * event_loop_thread_fn, i.e. SYNCHRONOUSLY on the event-loop thread itself,
 * while c->running is already true (self == event_loop_thread, running == true
 * -> predicate's !pthread_equal(...) is false -> no trip).
 *
 * IMPORTANT — registration ordering avoids a real cross-thread data race:
 * wl_event_loop_add_idle does a plain (non-atomic) wl_list_insert into
 * loop->idle_list (event-loop.c:792); calling it concurrently from the test
 * thread WHILE event_loop_thread_fn's wl_event_loop_dispatch is iterating that
 * same list is itself an unguarded cross-thread race — confirmed by an actual
 * SIGSEGV in wl_map_lookup_flags / wl_resource_destroy when this test
 * registered the idle source AFTER lorie_compositor_start (real crash, real
 * backtrace — see engram bugfix "test_onloop_destroy idle-list race"). The
 * fix: register the idle BEFORE lorie_compositor_start spawns the loop thread
 * — single-threaded at registration time (c->event_loop exists right after
 * lorie_compositor_create, well before start; compositor.c:246 vs. :456+527),
 * so there is no concurrent list mutation, and the idle still fires ON the
 * loop thread on its first dispatch — exactly the on-loop case under test.
 *
 * The test thread then blocks on a condvar until the on-loop destroy
 * completes, and asserts the surface was torn down without any abort/
 * diagnostic (the whole process is still alive — if the guard had mis-fired
 * on the on-loop path, this process would already be dead from SIGABRT). */
struct onloop_destroy_ctx {
    struct lorie_surface *s;
    pthread_mutex_t lock;
    pthread_cond_t done_cond;
    int done;
};

static void onloop_destroy_idle(void *data) {
    struct onloop_destroy_ctx *ctx = data;
    /* Runs ON the event-loop thread (wl_event_loop_dispatch_idle is invoked
     * from inside wl_event_loop_dispatch, called from event_loop_thread_fn)
     * on its FIRST dispatch — c->running is already true by the time the loop
     * thread starts dispatching (lorie_compositor_start sets it before
     * pthread_create; compositor.c:526-527): self == event_loop_thread,
     * running == true -> guard predicate's thread-affinity check
     * (!pthread_equal(...)) is false -> no trip. */
    lorie_surface_destroy_internal(ctx->s);

    pthread_mutex_lock(&ctx->lock);
    ctx->done = 1;
    pthread_cond_signal(&ctx->done_cond);
    pthread_mutex_unlock(&ctx->lock);
}

static void test_onloop_destroy_while_running_does_not_trip(void) {
    /* Compositor exists (and c->event_loop is valid) but is NOT running yet —
     * registering the idle here is single-threaded, race-free. */
    ASSERT_FALSE(atomic_load(&g_comp->running));
    ASSERT_NOT_NULL(g_comp->event_loop);

    int peer_fd = -1;
    struct wl_client *client = make_client(g_comp, &peer_fd);
    ASSERT_NOT_NULL(client);
    struct lorie_surface *s = lorie_surface_create_internal(g_comp, client, 0);
    ASSERT_NOT_NULL(s);

    struct onloop_destroy_ctx ctx;
    ctx.s = s;
    ctx.done = 0;
    pthread_mutex_init(&ctx.lock, NULL);
    pthread_cond_init(&ctx.done_cond, NULL);

    struct wl_event_source *idle = wl_event_loop_add_idle(g_comp->event_loop,
                                                           onloop_destroy_idle, &ctx);
    ASSERT_NOT_NULL(idle);

    /* NOW start the loop thread — c->running flips to true (compositor.c:526)
     * BEFORE pthread_create (compositor.c:527), and the idle we registered
     * above fires on that thread's very first wl_event_loop_dispatch. */
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    ASSERT_TRUE(atomic_load(&g_comp->running));

    /* Wait for the on-loop destroy to complete. The condvar/mutex pair is
     * plain pthread synchronization (not a Wayland API) — safe to use from
     * both threads concurrently. */
    pthread_mutex_lock(&ctx.lock);
    while (!ctx.done) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 10 * 1000 * 1000; /* 10ms */
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&ctx.done_cond, &ctx.lock, &ts);
    }
    pthread_mutex_unlock(&ctx.lock);

    /* If the guard had mis-fired on the on-loop path, this process would
     * already be dead (SIGABRT). Reaching here IS the "did not trip, no
     * diagnostic, teardown proceeded" proof — the surface is gone and the
     * process is alive and well. */
    ASSERT_TRUE(ctx.done);

    pthread_mutex_destroy(&ctx.lock);
    pthread_cond_destroy(&ctx.done_cond);

    /* Cleanup MUST NOT call wl_client_destroy directly here: g_comp->running
     * is still true (the loop thread is alive and actively dispatching).
     * lorie_test_safe_destroy_client joins the loop thread first (running ->
     * false, pthread_join — c->event_loop_thread fully exited), so by the time
     * it calls wl_client_destroy the loop is provably quiescent — single-
     * threaded access, the documented-safe "post-stop teardown" ordering, and
     * exactly the "Helper-based teardown leaves the guard untripped" contract
     * this helper exists to provide (used here even though this test's primary
     * subject is the on-loop guard path, because ANY teardown after this point
     * must be safe — that is the whole point of the change).
     *
     * peer_fd is closed only AFTER the client is destroyed — see make_client's
     * doc comment: closing it earlier (the test_input.c-style "close(fds[1])
     * immediately" pattern this suite deliberately does NOT use) makes
     * wl_client_connection_data treat the connection as WL_EVENT_HANGUP and
     * autonomously wl_client_destroy(client) on the loop thread (wayland-
     * server.c:379-380) at an unpredictable moment — a genuine race against
     * any later explicit destroy of the same pointer, whose observable symptom
     * is precisely "wl_client_destroy: encountered re-entrant client
     * destruction" (wayland-server.c:1016-1019, the EXACT log line the spec
     * names as a UAF symptom — confirmed firing from this fixture shape in a
     * real run before this fix; see engram bugfix "make_client peer-fd-close
     * races autonomous HANGUP client destroy"). Keeping peer_fd open for the
     * client's whole lifetime removes that race window entirely. */
    lorie_test_safe_destroy_client(g_comp, client);
    close(peer_fd);
}

/* --- 1.7 lives in test_surface.c (test_prestart_destroy_does_not_trip),
 * alongside the existing prestart create/destroy pattern
 * (test_surface_create_and_destroy) it mirrors — see tasks.md 1.7. --- */

/* --- 2.4: helper-based teardown leaves the guard untripped ---
 *
 * Spec scenario "Helper-based teardown leaves the guard untripped": using
 * lorie_test_safe_destroy_client (stop-then-destroy, joins event_loop_thread
 * first) must not trip the guard and must emit no diagnostic, because
 * teardown only happens after the event-loop thread has exited
 * (running == false at destroy time -> predicate's atomic_load(&c->running)
 * is false -> short-circuits before the thread-affinity check). */
static void test_helper_safe_destroy_client_does_not_trip_guard(void) {
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    struct lorie_input *in = g_comp->input;
    ASSERT_NOT_NULL(in);

    int peer_fd = -1;
    struct wl_client *client = make_client(g_comp, &peer_fd);
    ASSERT_NOT_NULL(client);
    struct lorie_surface *s = lorie_surface_create_internal(g_comp, client, 0);
    ASSERT_NOT_NULL(s);
    s->x = 0; s->y = 0; s->logical_width = 100; s->logical_height = 100;

    lorie_input_pointer_motion(in, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    ASSERT_EQ_PTR(s, in->pointer_focus);
    ASSERT_EQ_PTR(s, in->keyboard_focus);

    /* lorie_test_safe_destroy_client joins event_loop_thread FIRST
     * (atomic_store running=0, pthread_join — mirrors lorie_compositor_stop's
     * join step exactly, compositor.c:558-563, WITHOUT also calling
     * wl_display_destroy_clients: that would destroy `client` out from under
     * this caller's explicit wl_client_destroy(client) below it — see
     * lorie_test_teardown.h/.c's "join-without-destroy-clients" design note).
     * THEN wl_client_destroy(client) runs single-threaded, loop fully
     * quiescent. At the moment lorie_surface_destroy_internal executes
     * (reached via wl_client_destroy -> wl_resource_destroy ->
     * surface_handle_resource_destroy), c->running == false, so the guard's
     * predicate short-circuits to "no trip" regardless of which thread calls
     * it — the documented-safe sequence by construction. If the guard HAD
     * mis-fired here, this process would already be dead (SIGABRT); reaching
     * the assertions below IS the "guard did not trip, no diagnostic" proof.
     *
     * peer_fd stays open until after the client is destroyed — see
     * make_client's doc comment for why closing it earlier would itself race
     * an autonomous loop-thread HANGUP destroy of the same client pointer. */
    lorie_test_safe_destroy_client(g_comp, client);
    close(peer_fd);

    ASSERT_NULL(in->pointer_focus);
    ASSERT_NULL(in->keyboard_focus);
    ASSERT_FALSE(atomic_load(&g_comp->running));
}

int lorie_test_teardown_guard_suite(struct lorie_test_suite* suite) {
    lorie_suite_init(suite, "teardown_guard", setup, teardown);
    SUITE_ADD(suite, test_cross_thread_destroy_while_running_aborts);
    SUITE_ADD(suite, test_onloop_destroy_while_running_does_not_trip);
    SUITE_ADD(suite, test_helper_safe_destroy_client_does_not_trip_guard);
    return 0;
}
