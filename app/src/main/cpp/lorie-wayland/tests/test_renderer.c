/*
 * Lorie Wayland Compositor — Renderer Tests
 *
 * TDD: tests written before renderer.c implementation.
 */

#include "lorie_test.h"
#include "../renderer.h"

static void test_renderer_create_destroy(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    lorie_renderer_destroy(r);
}

static void test_renderer_init_fini(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    int ret = lorie_renderer_init(r);
    /* May fail without display; just verify no crash */
    (void)ret;
    lorie_renderer_fini(r);
    lorie_renderer_destroy(r);
}

static void test_renderer_set_window_null_safe(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    lorie_renderer_init(r);
    lorie_renderer_set_window(r, NULL);
    lorie_renderer_fini(r);
    lorie_renderer_destroy(r);
}

static void test_renderer_add_remove_surface(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    lorie_renderer_init(r);

    /* Use opaque pointer — renderer only stores it as a key */
    static char dummy_surface[256];
    struct lorie_surface *s = (struct lorie_surface *)dummy_surface;
    lorie_renderer_add_surface(r, s);
    lorie_renderer_remove_surface(r, s);

    lorie_renderer_fini(r);
    lorie_renderer_destroy(r);
}

static void test_renderer_damage_surface(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    lorie_renderer_init(r);

    static char dummy_surface[256];
    struct lorie_surface *s = (struct lorie_surface *)dummy_surface;
    lorie_renderer_add_surface(r, s);
    lorie_renderer_damage_surface(r, s, 0, 0, 100, 100);
    lorie_renderer_remove_surface(r, s);

    lorie_renderer_fini(r);
    lorie_renderer_destroy(r);
}

static void test_renderer_commit_no_crash(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    lorie_renderer_init(r);

    int ret = lorie_renderer_commit(r);
    (void)ret;

    lorie_renderer_fini(r);
    lorie_renderer_destroy(r);
}

int lorie_test_renderer_suite(struct lorie_test_suite* suite) {
    lorie_suite_init(suite, "renderer", NULL, NULL);
    SUITE_ADD(suite, test_renderer_create_destroy);
    SUITE_ADD(suite, test_renderer_init_fini);
    SUITE_ADD(suite, test_renderer_set_window_null_safe);
    SUITE_ADD(suite, test_renderer_add_remove_surface);
    SUITE_ADD(suite, test_renderer_damage_surface);
    SUITE_ADD(suite, test_renderer_commit_no_crash);
    return 0;
}
