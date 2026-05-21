/*
 * Lorie Wayland Compositor — Viewporter Protocol Tests (PR #3)
 *
 * TDD cycle:
 *   RED  : wp_viewporter protocol is not implemented; viewport state does
 *          not exist on lorie_surface.
 *   GREEN: viewporter global created, set_source/set_destination stored in
 *          pending_viewport, applied on commit, bad_value errors validated.
 */

#include "lorie_test.h"
#include "../compositor.h"
#include "../protocols/viewporter.h"
#include "stable-viewporter-viewporter.h"

static void test_viewporter_global_exists(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    ASSERT_NOT_NULL(c->viewporter_global);
    lorie_compositor_destroy(c);
}

static void test_viewporter_set_source(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 0);
    ASSERT_NOT_NULL(s);

    lorie_viewport_set_source(s, 0.0, 0.0, 50.0, 50.0);

    ASSERT_TRUE(s->pending_viewport.has_src);
    ASSERT_EQ_INT(0, (int)s->pending_viewport.src_x);
    ASSERT_EQ_INT(0, (int)s->pending_viewport.src_y);
    ASSERT_EQ_INT(50, (int)s->pending_viewport.src_w);
    ASSERT_EQ_INT(50, (int)s->pending_viewport.src_h);

    lorie_surface_destroy_internal(s);
    lorie_compositor_destroy(c);
}

static void test_viewporter_bad_value_error(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 0);
    ASSERT_NOT_NULL(s);

    /* Negative x */
    int ret = lorie_viewport_validate_source(-1.0, 0.0, 50.0, 50.0);
    ASSERT_EQ_INT(WP_VIEWPORT_ERROR_BAD_VALUE, ret);

    /* Negative y */
    ret = lorie_viewport_validate_source(0.0, -1.0, 50.0, 50.0);
    ASSERT_EQ_INT(WP_VIEWPORT_ERROR_BAD_VALUE, ret);

    /* Zero width */
    ret = lorie_viewport_validate_source(0.0, 0.0, 0.0, 50.0);
    ASSERT_EQ_INT(WP_VIEWPORT_ERROR_BAD_VALUE, ret);

    /* Zero height */
    ret = lorie_viewport_validate_source(0.0, 0.0, 50.0, 0.0);
    ASSERT_EQ_INT(WP_VIEWPORT_ERROR_BAD_VALUE, ret);

    /* Valid values */
    ret = lorie_viewport_validate_source(0.0, 0.0, 50.0, 50.0);
    ASSERT_EQ_INT(0, ret);

    lorie_surface_destroy_internal(s);
    lorie_compositor_destroy(c);
}

static void test_viewporter_commit_applies(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 0);
    ASSERT_NOT_NULL(s);

    s->width = 100;
    s->height = 100;
    s->buffer_scale = 1;

    lorie_viewport_set_source(s, 0.0, 0.0, 50.0, 50.0);
    lorie_viewport_set_destination(s, 200, 200);

    /* Simulate commit: apply pending viewport and compute logical size */
    s->viewport = s->pending_viewport;
    lorie_surface_compute_logical_size(s);

    /* destination overrides logical size */
    ASSERT_EQ_INT(200, s->logical_width);
    ASSERT_EQ_INT(200, s->logical_height);

    lorie_surface_destroy_internal(s);
    lorie_compositor_destroy(c);
}

static void test_viewporter_destination_only(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 0);
    ASSERT_NOT_NULL(s);

    s->width = 100;
    s->height = 200;
    s->buffer_scale = 1;

    lorie_viewport_set_destination(s, 50, 100);

    s->viewport = s->pending_viewport;
    lorie_surface_compute_logical_size(s);

    ASSERT_EQ_INT(50, s->logical_width);
    ASSERT_EQ_INT(100, s->logical_height);

    lorie_surface_destroy_internal(s);
    lorie_compositor_destroy(c);
}

int lorie_test_viewporter_suite(struct lorie_test_suite *suite) {
    lorie_suite_init(suite, "viewporter", NULL, NULL);
    SUITE_ADD(suite, test_viewporter_global_exists);
    SUITE_ADD(suite, test_viewporter_set_source);
    SUITE_ADD(suite, test_viewporter_bad_value_error);
    SUITE_ADD(suite, test_viewporter_commit_applies);
    SUITE_ADD(suite, test_viewporter_destination_only);
    return 0;
}
