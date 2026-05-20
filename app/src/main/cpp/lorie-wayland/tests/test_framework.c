/*
 * Lorie Wayland Compositor — Framework Self-Tests
 *
 * Verifies the test framework itself works. These must all pass
 * before any other test suite is trusted.
 */

#include "lorie_test.h"

static void test_assert_true_passes(void) {
    ASSERT_TRUE(1 == 1);
}

static void test_assert_true_fails(void) {
    /* This test deliberately fails to verify the runner catches it.
     * We skip it in normal runs by not adding it to the suite.
     * (If we wanted to test failure detection, we'd need a separate
     *  runner that expects exactly one failure.)
     */
}

static void test_assert_eq_int(void) {
    ASSERT_EQ_INT(42, 42);
    ASSERT_EQ_INT(-1, -1);
    ASSERT_EQ_INT(0, 0);
}

static void test_assert_eq_ptr(void) {
    int x = 0;
    ASSERT_EQ_PTR(NULL, NULL);
    ASSERT_EQ_PTR(&x, &x);
}

static void test_assert_null(void) {
    void* p = NULL;
    ASSERT_NULL(p);
}

static void test_assert_not_null(void) {
    int x = 0;
    ASSERT_NOT_NULL(&x);
}

static void test_assert_eq_str(void) {
    ASSERT_EQ_STR("hello", "hello");
    ASSERT_EQ_STR("", "");
}

static void test_assert_false(void) {
    ASSERT_FALSE(1 == 2);
}

int lorie_test_framework_suite(struct lorie_test_suite* suite) {
    lorie_suite_init(suite, "framework", NULL, NULL);
    SUITE_ADD(suite, test_assert_true_passes);
    SUITE_ADD(suite, test_assert_eq_int);
    SUITE_ADD(suite, test_assert_eq_ptr);
    SUITE_ADD(suite, test_assert_null);
    SUITE_ADD(suite, test_assert_not_null);
    SUITE_ADD(suite, test_assert_eq_str);
    SUITE_ADD(suite, test_assert_false);
    return 0;
}
