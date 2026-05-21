/*
 * Lorie Wayland Compositor — Transform + Buffer Scale Tests (PR #3)
 *
 * TDD cycle:
 *   RED  : buffer_transform is stored but ignored by renderer and surface commit.
 *   GREEN: logical size computed with transform swap, renderer uploads non-identity
 *          matrix per surface.
 */

#include "lorie_test.h"
#include "../compositor.h"
#include "../renderer.h"
#include <wayland-server-protocol.h>

static void test_transform_matrix_computed(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    static char dummy_surface[256];
    struct lorie_surface *s = (struct lorie_surface *)dummy_surface;
    memset(s, 0, sizeof(*s));
    s->buffer_transform = WL_OUTPUT_TRANSFORM_90;
    s->buffer_scale = 2;
    s->width = 100;
    s->height = 200;
    s->logical_width = 50;
    s->logical_height = 100;

    lorie_renderer_add_surface(r, s);
    lorie_renderer_damage_surface(r, s, 0, 0, 50, 100);

    /* commit computes transform matrix even without EGL surface */
    int ret = lorie_renderer_commit(r);
    (void)ret;

    const float *m = lorie_renderer_surface_get_transform(r, s);
    ASSERT_NOT_NULL(m);

    /* Verify non-identity: at least one element off the diagonal is non-zero
     * or at least one diagonal element is not 1.0 */
    int is_identity = 1;
    for (int i = 0; i < 16; i++) {
        float expected = (i % 5 == 0) ? 1.0f : 0.0f;
        if (m[i] != expected) {
            is_identity = 0;
            break;
        }
    }
    ASSERT_FALSE(is_identity);

    lorie_renderer_remove_surface(r, s);
    lorie_renderer_destroy(r);
}

static void test_transform_rotated_buffer_size(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 0);
    ASSERT_NOT_NULL(s);

    s->width = 100;
    s->height = 200;
    s->buffer_transform = WL_OUTPUT_TRANSFORM_90;
    s->buffer_scale = 1;

    lorie_surface_compute_logical_size(s);

    /* 100x200 buffer rotated 90° → logical size swapped to 200x100 */
    ASSERT_EQ_INT(200, s->logical_width);
    ASSERT_EQ_INT(100, s->logical_height);

    lorie_surface_destroy_internal(s);
    lorie_compositor_destroy(c);
}

static void test_transform_scaled_buffer_size(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 0);
    ASSERT_NOT_NULL(s);

    s->width = 200;
    s->height = 400;
    s->buffer_transform = WL_OUTPUT_TRANSFORM_NORMAL;
    s->buffer_scale = 2;

    lorie_surface_compute_logical_size(s);

    /* 200x400 buffer with scale=2 → logical size 100x200 */
    ASSERT_EQ_INT(100, s->logical_width);
    ASSERT_EQ_INT(200, s->logical_height);

    lorie_surface_destroy_internal(s);
    lorie_compositor_destroy(c);
}

static void test_transform_180_no_swap(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 0);
    ASSERT_NOT_NULL(s);

    s->width = 100;
    s->height = 200;
    s->buffer_transform = WL_OUTPUT_TRANSFORM_180;
    s->buffer_scale = 1;

    lorie_surface_compute_logical_size(s);

    /* 180° rotation does not swap width/height */
    ASSERT_EQ_INT(100, s->logical_width);
    ASSERT_EQ_INT(200, s->logical_height);

    lorie_surface_destroy_internal(s);
    lorie_compositor_destroy(c);
}

int lorie_test_transform_suite(struct lorie_test_suite *suite) {
    lorie_suite_init(suite, "transform", NULL, NULL);
    SUITE_ADD(suite, test_transform_matrix_computed);
    SUITE_ADD(suite, test_transform_rotated_buffer_size);
    SUITE_ADD(suite, test_transform_scaled_buffer_size);
    SUITE_ADD(suite, test_transform_180_no_swap);
    return 0;
}
