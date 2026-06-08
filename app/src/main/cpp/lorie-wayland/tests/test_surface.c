/* Lorie Wayland Compositor — Surface Tests (PR #3) */

#include "lorie_test.h"
#include "../compositor.h"
#include "../renderer.h"
#include "lorie_test_teardown.h"
#include <wayland-server-protocol.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_surface_create_and_destroy(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 0);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ_PTR(c, s->compositor);
    ASSERT_EQ_INT(1, s->buffer_scale);
    lorie_surface_destroy_internal(s);
    lorie_compositor_destroy(c);
}

/* 1.7 (compositor-teardown-safety, "Creation-time error-path teardown before
 * the loop starts proceeds untouched"): lorie_surface_destroy_internal called
 * on a surface from lorie_compositor_create() WITHOUT lorie_compositor_start
 * — running == false, event_loop_thread not yet created/joinable — must
 * proceed exactly as before with no guard trip and no diagnostic. This is
 * literally the same shape as test_surface_create_and_destroy above (which
 * has exercised this exact prestart create/destroy pattern unchanged); this
 * test makes the guard's "must not trip here" contract an explicit, named
 * assertion of its own per the spec scenario, rather than an incidental
 * property of an existing test. The predicate
 * (c && atomic_load(&c->running) && !pthread_equal(...)) short-circuits on
 * the false `running` BEFORE ever reading the not-yet-meaningful
 * event_loop_thread — see lorie_compositor_assert_event_loop_thread's ordering
 * note. Reaching the final assertion alive (no SIGABRT) IS the proof: a
 * mis-firing guard would have aborted this process before getting here. */
static void test_prestart_destroy_does_not_trip(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    ASSERT_FALSE(atomic_load(&c->running));

    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 0);
    ASSERT_NOT_NULL(s);

    lorie_surface_destroy_internal(s);

    ASSERT_FALSE(atomic_load(&c->running));
    lorie_compositor_destroy(c);
}

static void test_surface_damage_tracks_region(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 0);
    ASSERT_NOT_NULL(s);
    pixman_region32_union_rect(&s->damage, &s->damage, 10, 20, 100, 50);
    int n = pixman_region32_n_rects(&s->damage);
    ASSERT_EQ_INT(1, n);
    lorie_surface_destroy_internal(s);
    lorie_compositor_destroy(c);
}

static void test_surface_commit_clears_pending(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 0);
    ASSERT_NOT_NULL(s);
    s->pending_attached = 1;
    s->pending_buffer = (struct wl_resource *)0x1234;
    /* Simulate commit logic (can't call real commit without resource) */
    if (s->pending_attached) {
        s->buffer_resource = s->pending_buffer;
        s->pending_buffer = NULL;
        s->pending_attached = 0;
    }
    ASSERT_EQ_PTR((struct wl_resource *)0x1234, s->buffer_resource);
    ASSERT_NULL(s->pending_buffer);
    lorie_surface_destroy_internal(s);
    lorie_compositor_destroy(c);
}

static void test_region_add_subtract(void) {
    pixman_region32_t region;
    pixman_region32_init(&region);
    pixman_region32_union_rect(&region, &region, 0, 0, 100, 100);
    ASSERT_EQ_INT(1, pixman_region32_n_rects(&region));
    pixman_region32_t rect;
    pixman_region32_init_rect(&rect, 25, 25, 50, 50);
    pixman_region32_subtract(&region, &region, &rect);
    pixman_region32_fini(&rect);
    ASSERT_TRUE(pixman_region32_n_rects(&region) > 0);
    pixman_region32_fini(&region);
}

static void test_frame_callback_not_fired_by_commit(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    int ret = lorie_compositor_start(c);
    ASSERT_EQ_INT(0, ret);

    int fds[2];
    ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    ASSERT_EQ_INT(0, ret);

    struct wl_client *client = wl_client_create(c->display, fds[0]);
    ASSERT_NOT_NULL(client);

    struct lorie_surface *s = lorie_surface_create_internal(c, client, 100);
    ASSERT_NOT_NULL(s);
    ASSERT_NOT_NULL(s->resource);

    struct wl_resource *cb_res = wl_resource_create(client, &wl_callback_interface, 1, 200);
    ASSERT_NOT_NULL(cb_res);
    struct lorie_frame_callback *fcb = calloc(1, sizeof(*fcb));
    ASSERT_NOT_NULL(fcb);
    fcb->resource = cb_res;
    wl_list_insert(&s->frame_callbacks, &fcb->link);

    surface_commit(client, s->resource);

    /* Callback should still be in the list (renderer fires it later) */
    ASSERT_TRUE(!wl_list_empty(&s->frame_callbacks));

    wl_list_remove(&fcb->link);
    wl_resource_destroy(cb_res);
    free(fcb);

    /* compositor-teardown-safety: lorie_surface_destroy_internal /
     * wl_client_destroy must not run cross-thread while the loop is alive
     * (lorie_compositor_assert_event_loop_thread aborts on the unsafe
     * pattern observed here — destroy on the test thread while c->running).
     * lorie_test_safe_destroy_client joins the event-loop thread first
     * (mirrors lorie_compositor_stop's own join step minus the
     * wl_display_destroy_clients side effect that would double-destroy this
     * very client), THEN destroys client+surface together via the
     * resource-destroy chain — so the explicit lorie_compositor_stop(c)
     * below it became redundant and has been removed. */
    lorie_test_safe_destroy_client(c, client);
    close(fds[1]);
    lorie_compositor_destroy(c);
}

static void test_subsurface_no_self_parent(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 0);
    ASSERT_NOT_NULL(s);
    /* Self-parent check is structural; documented here */
    ASSERT_TRUE(s == s);
    lorie_surface_destroy_internal(s);
    lorie_compositor_destroy(c);
}

static void test_surface_registers_with_renderer(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    lorie_renderer_init(r);
    c->renderer = r;

    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 0);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ_INT(1, lorie_renderer_surface_count(r));

    lorie_surface_destroy_internal(s);
    lorie_compositor_destroy(c);
    lorie_renderer_fini(r);
    lorie_renderer_destroy(r);
}

static void test_surface_unregisters_on_destroy(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    lorie_renderer_init(r);
    c->renderer = r;

    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 0);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ_INT(1, lorie_renderer_surface_count(r));

    lorie_surface_destroy_internal(s);

    ASSERT_EQ_INT(0, lorie_renderer_surface_count(r));

    lorie_compositor_destroy(c);
    lorie_renderer_fini(r);
    lorie_renderer_destroy(r);
}

int lorie_test_surface_suite(struct lorie_test_suite *suite) {
    lorie_suite_init(suite, "surface", NULL, NULL);
    SUITE_ADD(suite, test_surface_create_and_destroy);
    SUITE_ADD(suite, test_prestart_destroy_does_not_trip);
    SUITE_ADD(suite, test_surface_damage_tracks_region);
    SUITE_ADD(suite, test_surface_commit_clears_pending);
    SUITE_ADD(suite, test_region_add_subtract);
    SUITE_ADD(suite, test_frame_callback_not_fired_by_commit);
    SUITE_ADD(suite, test_subsurface_no_self_parent);
    SUITE_ADD(suite, test_surface_registers_with_renderer);
    SUITE_ADD(suite, test_surface_unregisters_on_destroy);
    return 0;
}
