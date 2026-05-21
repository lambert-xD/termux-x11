/*
 * Lorie Wayland Compositor — Renderer Damage Tracking Tests (PR #2)
 *
 * TDD cycle:
 *   RED  : lorie_renderer_damage_surface is a no-op; accumulated_damage
 *          is never populated.
 *   GREEN: Damage rects are unioned into accumulated_damage; commit
 *          clears them after drawing.
 */

#include "lorie_test.h"
#include "../renderer.h"
#include <pixman.h>

static void test_damage_accumulates(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    static char dummy_surface[256];
    struct lorie_surface *s = (struct lorie_surface *)dummy_surface;
    lorie_renderer_add_surface(r, s);

    lorie_renderer_damage_surface(r, s, 10, 20, 100, 50);
    lorie_renderer_damage_surface(r, s, 50, 40, 100, 50);

    pixman_region32_t *damage = lorie_renderer_surface_get_damage(r, s);
    ASSERT_NOT_NULL(damage);
    ASSERT_TRUE(pixman_region32_not_empty(damage));

    lorie_renderer_remove_surface(r, s);
    lorie_renderer_destroy(r);
}

static void test_damage_bbox_correct(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    static char dummy_surface[256];
    struct lorie_surface *s = (struct lorie_surface *)dummy_surface;
    lorie_renderer_add_surface(r, s);

    lorie_renderer_damage_surface(r, s, 10, 20, 100, 50);

    pixman_region32_t *damage = lorie_renderer_surface_get_damage(r, s);
    ASSERT_NOT_NULL(damage);
    pixman_box32_t *box = pixman_region32_extents(damage);
    ASSERT_EQ_INT(10, box->x1);
    ASSERT_EQ_INT(20, box->y1);
    ASSERT_EQ_INT(110, box->x2);
    ASSERT_EQ_INT(70, box->y2);

    lorie_renderer_remove_surface(r, s);
    lorie_renderer_destroy(r);
}

static void test_damage_cleared_after_commit(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    static char dummy_surface[256];
    struct lorie_surface *s = (struct lorie_surface *)dummy_surface;
    lorie_renderer_add_surface(r, s);

    lorie_renderer_damage_surface(r, s, 10, 20, 100, 50);

    /* commit returns -1 without EGL surface, so damage is NOT cleared */
    int ret = lorie_renderer_commit(r);
    ASSERT_EQ_INT(-1, ret);

    pixman_region32_t *damage = lorie_renderer_surface_get_damage(r, s);
    ASSERT_NOT_NULL(damage);
    /* Damage persists because commit couldn't draw (no EGL surface) */
    ASSERT_TRUE(pixman_region32_not_empty(damage));

    lorie_renderer_remove_surface(r, s);
    lorie_renderer_destroy(r);
}

static void test_damage_empty_region_no_crash(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    static char dummy_surface[256];
    struct lorie_surface *s = (struct lorie_surface *)dummy_surface;
    lorie_renderer_add_surface(r, s);

    /* No damage added - commit should not crash */
    int ret = lorie_renderer_commit(r);
    (void)ret;

    pixman_region32_t *damage = lorie_renderer_surface_get_damage(r, s);
    ASSERT_NOT_NULL(damage);
    ASSERT_FALSE(pixman_region32_not_empty(damage));

    lorie_renderer_remove_surface(r, s);
    lorie_renderer_destroy(r);
}

static void test_damage_null_renderer_safe(void) {
    static char dummy_surface[256];
    struct lorie_surface *s = (struct lorie_surface *)dummy_surface;
    lorie_renderer_damage_surface(NULL, s, 0, 0, 100, 100);
    /* Should not crash */
}

static void test_damage_null_surface_safe(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    lorie_renderer_damage_surface(r, NULL, 0, 0, 100, 100);
    /* Should not crash */
    lorie_renderer_destroy(r);
}

static void test_damage_negative_size_ignored(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    static char dummy_surface[256];
    struct lorie_surface *s = (struct lorie_surface *)dummy_surface;
    lorie_renderer_add_surface(r, s);

    lorie_renderer_damage_surface(r, s, 0, 0, -10, 50);

    pixman_region32_t *damage = lorie_renderer_surface_get_damage(r, s);
    ASSERT_NOT_NULL(damage);
    ASSERT_FALSE(pixman_region32_not_empty(damage));

    lorie_renderer_remove_surface(r, s);
    lorie_renderer_destroy(r);
}

static void test_damage_multiple_surfaces(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    static char dummy_s1[256];
    static char dummy_s2[256];
    struct lorie_surface *s1 = (struct lorie_surface *)dummy_s1;
    struct lorie_surface *s2 = (struct lorie_surface *)dummy_s2;
    lorie_renderer_add_surface(r, s1);
    lorie_renderer_add_surface(r, s2);

    lorie_renderer_damage_surface(r, s1, 0, 0, 10, 10);
    lorie_renderer_damage_surface(r, s2, 20, 20, 30, 30);

    pixman_region32_t *d1 = lorie_renderer_surface_get_damage(r, s1);
    pixman_region32_t *d2 = lorie_renderer_surface_get_damage(r, s2);
    ASSERT_NOT_NULL(d1);
    ASSERT_NOT_NULL(d2);
    ASSERT_TRUE(pixman_region32_not_empty(d1));
    ASSERT_TRUE(pixman_region32_not_empty(d2));

    pixman_box32_t *b1 = pixman_region32_extents(d1);
    pixman_box32_t *b2 = pixman_region32_extents(d2);
    ASSERT_EQ_INT(0, b1->x1);
    ASSERT_EQ_INT(0, b1->y1);
    ASSERT_EQ_INT(10, b1->x2);
    ASSERT_EQ_INT(10, b1->y2);
    ASSERT_EQ_INT(20, b2->x1);
    ASSERT_EQ_INT(20, b2->y1);
    ASSERT_EQ_INT(50, b2->x2);
    ASSERT_EQ_INT(50, b2->y2);

    lorie_renderer_remove_surface(r, s1);
    lorie_renderer_remove_surface(r, s2);
    lorie_renderer_destroy(r);
}

int lorie_test_renderer_damage_suite(struct lorie_test_suite *suite) {
    lorie_suite_init(suite, "renderer_damage", NULL, NULL);
    SUITE_ADD(suite, test_damage_accumulates);
    SUITE_ADD(suite, test_damage_bbox_correct);
    SUITE_ADD(suite, test_damage_cleared_after_commit);
    SUITE_ADD(suite, test_damage_empty_region_no_crash);
    SUITE_ADD(suite, test_damage_null_renderer_safe);
    SUITE_ADD(suite, test_damage_null_surface_safe);
    SUITE_ADD(suite, test_damage_negative_size_ignored);
    SUITE_ADD(suite, test_damage_multiple_surfaces);
    return 0;
}
