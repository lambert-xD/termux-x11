#include "lorie_test.h"
#include "compositor.h"
#include "input.h"
#include "lorie_test_teardown.h"
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

static struct lorie_compositor *g_comp = NULL;

static void setup(void) {
    g_comp = lorie_compositor_create();
    ASSERT_NOT_NULL(g_comp);
}

static void teardown(void) {
    if (g_comp) { lorie_compositor_destroy(g_comp); g_comp = NULL; }
}

static struct lorie_surface *make_surface(struct wl_client *client, int32_t x, int32_t y, int32_t w, int32_t h) {
    struct lorie_surface *s = lorie_surface_create_internal(g_comp, client, 0);
    if (s) { s->x = x; s->y = y; s->logical_width = w; s->logical_height = h; }
    return s;
}

/* compositor-teardown-safety (engram bugfix #180, "make_client peer-fd
 * race"): closing the peer fd immediately after wl_client_create makes the
 * HANGUP condition pending right away, so the running compositor's
 * event-loop thread can autonomously wl_client_destroy() this client (via
 * wl_client_connection_data's WL_EVENT_HANGUP path, wayland-server.c:379-380)
 * at any later moment — racing this file's own explicit teardown calls on
 * the very same pointer (the EXACT cross-thread UAF this whole change
 * targets; observed as "re-entrant client destruction" log lines or a
 * genuine SIGSEGV). Returning the peer fd via out-parameter instead lets
 * every caller defer close(*peer_fd) until AFTER the client has been safely,
 * fully destroyed (lorie_test_safe_destroy_client joins the loop first),
 * eliminating the autonomous-reap race window for the client's entire test
 * lifetime. Pass NULL when the caller never destroys the client itself
 * (e.g. relies on lorie_compositor_destroy at suite teardown). */
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
        close(fds[1]);
    return client;
}

static void test_input_init_creates_seat(void) {
    struct lorie_input *in = lorie_input_init(g_comp);
    ASSERT_NOT_NULL(in);
    ASSERT_EQ_PTR(g_comp, in->compositor);
    lorie_input_destroy(in);
}

static void test_pointer_focus_set_on_motion(void) {
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    struct lorie_input *in = lorie_input_init(g_comp);
    int peer_fd;
    struct wl_client *client = make_client(g_comp, &peer_fd);
    ASSERT_NOT_NULL(client);
    struct lorie_surface *s = make_surface(client, 10, 20, 200, 100);
    ASSERT_NOT_NULL(s);
    ASSERT_NULL(in->pointer_focus);
    lorie_input_pointer_motion(in, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    ASSERT_EQ_PTR(s, in->pointer_focus);
    lorie_input_destroy(in);
    /* compositor-teardown-safety: lorie_surface_destroy_internal /
     * wl_client_destroy must not run cross-thread while the loop is alive
     * (lorie_compositor_assert_event_loop_thread aborts on the unsafe
     * pattern) — join-then-destroy via the safe-teardown helper instead. */
    lorie_test_safe_destroy_client(g_comp, client);
    close(peer_fd);
}

static void test_pointer_focus_cleared_on_leave(void) {
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    struct lorie_input *in = lorie_input_init(g_comp);
    int peer_fd;
    struct wl_client *client = make_client(g_comp, &peer_fd);
    ASSERT_NOT_NULL(client);
    struct lorie_surface *s = make_surface(client, 10, 20, 200, 100);
    ASSERT_NOT_NULL(s);
    lorie_input_pointer_motion(in, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    ASSERT_EQ_PTR(s, in->pointer_focus);
    lorie_input_pointer_motion(in, 500.0f, 500.0f);
    lorie_input_dispatch(in);
    ASSERT_NULL(in->pointer_focus);
    lorie_input_destroy(in);
    lorie_test_safe_destroy_client(g_comp, client);
    close(peer_fd);
}

static void test_keyboard_follows_pointer_focus(void) {
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    struct lorie_input *in = lorie_input_init(g_comp);
    int peer_fd;
    struct wl_client *client = make_client(g_comp, &peer_fd);
    ASSERT_NOT_NULL(client);
    struct lorie_surface *s = make_surface(client, 0, 0, 100, 100);
    ASSERT_NOT_NULL(s);
    ASSERT_NULL(in->keyboard_focus);
    lorie_input_pointer_motion(in, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    ASSERT_EQ_PTR(s, in->keyboard_focus);
    lorie_input_pointer_motion(in, 200.0f, 200.0f);
    lorie_input_dispatch(in);
    ASSERT_NULL(in->keyboard_focus);
    lorie_input_destroy(in);
    lorie_test_safe_destroy_client(g_comp, client);
    close(peer_fd);
}

static void test_focus_cleared_on_surface_destroy(void) {
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    struct lorie_input *in = g_comp->input;
    int peer_fd;
    struct wl_client *client = make_client(g_comp, &peer_fd);
    ASSERT_NOT_NULL(client);
    struct lorie_surface *s = make_surface(client, 0, 0, 100, 100);
    ASSERT_NOT_NULL(s);
    lorie_input_pointer_motion(in, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    /* Assert focus is live BEFORE teardown (compositor-teardown-safety:
     * "Helper tears down a focused surface's client without racing the
     * loop" — the helper must not merely avoid tripping the guard, it must
     * still correctly clear focus that was genuinely held). */
    ASSERT_EQ_PTR(s, in->pointer_focus);
    ASSERT_EQ_PTR(s, in->keyboard_focus);
    /* lorie_surface_destroy_internal(s) directly here would trip
     * lorie_compositor_assert_event_loop_thread (cross-thread destroy while
     * running -> abort()). The safe-teardown helper joins the event-loop
     * thread first (running becomes false, guard's predicate short-circuits
     * to "no trip"), THEN destroys client+surface together via the
     * resource-destroy chain — exactly the documented stop-then-destroy
     * contract lorie_compositor_stop itself follows. */
    lorie_test_safe_destroy_client(g_comp, client);
    /* Assert focus is cleared AFTER teardown — same postcondition as before
     * migration, now reached via the safe path instead of the racy one. */
    ASSERT_NULL(in->pointer_focus); ASSERT_NULL(in->keyboard_focus);
    close(peer_fd);
}

static void test_touch_focus_set_on_down(void) {
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    struct lorie_input *in = lorie_input_init(g_comp);
    int peer_fd;
    struct wl_client *client = make_client(g_comp, &peer_fd);
    ASSERT_NOT_NULL(client);
    struct lorie_surface *s = make_surface(client, 0, 0, 100, 100);
    ASSERT_NOT_NULL(s);
    ASSERT_NULL(in->touch_focus);
    ASSERT_NULL(in->keyboard_focus);
    lorie_input_touch_down(in, 0, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    ASSERT_EQ_PTR(s, in->touch_focus);
    ASSERT_EQ_PTR(s, in->keyboard_focus);
    lorie_input_destroy(in);
    lorie_test_safe_destroy_client(g_comp, client);
    close(peer_fd);
}

static void test_keyboard_dispatch_android_keycode(void) {
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    struct lorie_input *in = g_comp->input;
    int peer_fd;
    struct wl_client *client = make_client(g_comp, &peer_fd);
    ASSERT_NOT_NULL(client);
    struct lorie_surface *s = make_surface(client, 0, 0, 100, 100);
    ASSERT_NOT_NULL(s);
    lorie_input_pointer_motion(in, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    ASSERT_EQ_PTR(s, in->keyboard_focus);
    lorie_input_keyboard_key(in, 29, 1);
    lorie_input_dispatch(in);
    lorie_input_keyboard_key(in, 29, 0);
    lorie_input_dispatch(in);
    lorie_test_safe_destroy_client(g_comp, client);
    close(peer_fd);
}

static void test_keyboard_dispatch_rejects_unmapped(void) {
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    struct lorie_input *in = g_comp->input;
    int peer_fd;
    struct wl_client *client = make_client(g_comp, &peer_fd);
    ASSERT_NOT_NULL(client);
    struct lorie_surface *s = make_surface(client, 0, 0, 100, 100);
    ASSERT_NOT_NULL(s);
    lorie_input_pointer_motion(in, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    lorie_input_keyboard_key(in, 304, 1);
    lorie_input_dispatch(in);
    lorie_input_keyboard_key(in, 303, 1);
    lorie_input_dispatch(in);
    lorie_test_safe_destroy_client(g_comp, client);
    close(peer_fd);
}

static void test_no_crash_when_no_focus(void) {
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    struct lorie_input *in = lorie_input_init(g_comp);
    lorie_input_pointer_motion(in, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    ASSERT_NULL(in->pointer_focus);
    lorie_input_pointer_button(in, 0x110, 1);
    lorie_input_dispatch(in);
    lorie_input_keyboard_key(in, 29, 1);
    lorie_input_dispatch(in);
    lorie_input_touch_down(in, 0, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    lorie_input_destroy(in);
}

int lorie_test_input_suite(struct lorie_test_suite* suite) {
    lorie_suite_init(suite, "input", setup, teardown);
    SUITE_ADD(suite, test_input_init_creates_seat);
    SUITE_ADD(suite, test_pointer_focus_set_on_motion);
    SUITE_ADD(suite, test_pointer_focus_cleared_on_leave);
    SUITE_ADD(suite, test_keyboard_follows_pointer_focus);
    SUITE_ADD(suite, test_focus_cleared_on_surface_destroy);
    SUITE_ADD(suite, test_touch_focus_set_on_down);
    SUITE_ADD(suite, test_keyboard_dispatch_android_keycode);
    SUITE_ADD(suite, test_keyboard_dispatch_rejects_unmapped);
    SUITE_ADD(suite, test_no_crash_when_no_focus);
    return 0;
}
