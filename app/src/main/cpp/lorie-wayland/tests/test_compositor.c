/*
 * Lorie Wayland Compositor — Compositor Core Tests (PR #2)
 *
 * TDD cycle:
 *   RED  : These tests call lorie_compositor_* and lorie_output_*
 *          APIs that do not exist yet.
 *   GREEN: compositor.c, compositor.h, and output.c implement
 *          the APIs; tests compile and pass.
 */

#include "lorie_test.h"
#include "../compositor.h"
#include "../renderer.h"
#include <unistd.h>

static void test_compositor_create_returns_non_null(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    lorie_compositor_destroy(c);
}

static void test_compositor_create_initializes_lists(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    /* wl_list should be initialized (next != NULL) */
    ASSERT_NOT_NULL(c->outputs.next);
    ASSERT_NOT_NULL(c->surfaces.next);
    ASSERT_NOT_NULL(c->clients.next);
    lorie_compositor_destroy(c);
}

static void test_compositor_start_stop_lifecycle(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    ASSERT_EQ_INT(0, lorie_compositor_start(c));
    /* After start, display should have a socket name */
    ASSERT_NOT_NULL(c->display);
    lorie_compositor_stop(c);
    lorie_compositor_destroy(c);
}

static void test_compositor_start_fails_without_display(void) {
    /* If create returns NULL, start should not be called */
    struct lorie_compositor *c = lorie_compositor_create();
    if (c) {
        ASSERT_EQ_INT(0, lorie_compositor_start(c));
        lorie_compositor_stop(c);
        lorie_compositor_destroy(c);
    }
}

static void test_compositor_set_window_null_safe(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    /* Setting NULL should be safe (no window yet) */
    lorie_compositor_set_window(c, NULL);
    ASSERT_EQ_PTR(NULL, c->native_window);
    lorie_compositor_destroy(c);
}

static void test_output_create_destroy(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);

    struct lorie_output *output = lorie_output_create(c, 1080, 2400, 2);
    ASSERT_NOT_NULL(output);
    ASSERT_EQ_INT(1080, output->width);
    ASSERT_EQ_INT(2400, output->height);
    ASSERT_EQ_INT(2, output->scale);

    lorie_output_destroy(output);
    lorie_compositor_destroy(c);
}

static void test_output_zero_dimensions_rejected(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);

    struct lorie_output *output = lorie_output_create(c, 0, 0, 1);
    ASSERT_NULL(output);

    lorie_compositor_destroy(c);
}

static void test_compositor_has_output_global_after_start(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);

    /* Create output before start */
    struct lorie_output *output = lorie_output_create(c, 1080, 2400, 2);
    ASSERT_NOT_NULL(output);

    ASSERT_EQ_INT(0, lorie_compositor_start(c));

    /* wl_display should have been created */
    ASSERT_NOT_NULL(c->display);

    /* event_loop should exist */
    ASSERT_NOT_NULL(c->event_loop);

    lorie_compositor_stop(c);
    lorie_output_destroy(output);
    lorie_compositor_destroy(c);
}

static void test_compositor_render_loop_lifecycle(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    lorie_renderer_init(r);
    c->renderer = r;

    ASSERT_EQ_INT(0, lorie_compositor_start(c));
    /* Let event/render loop run briefly */
    usleep(50000);
    ASSERT_TRUE(atomic_load(&c->running));

    lorie_compositor_stop(c);
    ASSERT_TRUE(!atomic_load(&c->running));

    lorie_compositor_destroy(c);
    lorie_renderer_fini(r);
    lorie_renderer_destroy(r);
}

int lorie_test_compositor_suite(struct lorie_test_suite* suite) {
    lorie_suite_init(suite, "compositor", NULL, NULL);
    SUITE_ADD(suite, test_compositor_create_returns_non_null);
    SUITE_ADD(suite, test_compositor_create_initializes_lists);
    SUITE_ADD(suite, test_compositor_start_stop_lifecycle);
    SUITE_ADD(suite, test_compositor_start_fails_without_display);
    SUITE_ADD(suite, test_compositor_set_window_null_safe);
    SUITE_ADD(suite, test_output_create_destroy);
    SUITE_ADD(suite, test_output_zero_dimensions_rejected);
    SUITE_ADD(suite, test_compositor_has_output_global_after_start);
    SUITE_ADD(suite, test_compositor_render_loop_lifecycle);
    return 0;
}
