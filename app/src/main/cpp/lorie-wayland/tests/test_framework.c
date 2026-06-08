/*
 * Lorie Wayland Compositor — Framework Self-Tests
 *
 * Verifies the test framework itself works. These must all pass
 * before any other test suite is trusted.
 */

#include "lorie_test.h"

/* --- Shared runner state and loop: SINGLE DEFINITION for the whole binary ---
 *
 * `_lorie_runner` (and the loop that owns its setjmp/longjmp machinery) MUST
 * exist exactly once with external linkage. Assertions are expanded inline in
 * test_*.c TUs and mutate `_lorie_runner` directly; the runner loop below
 * — invoked from the main-driver TU (test_main.c / test_main_protocols_only.c)
 * — owns the `jump`/`jumping` fields and is the only thing that may longjmp.
 * Defining it here (test_framework.c is compiled into every test binary,
 * official and diagnostic alike — see test_main_protocols_only.c's header
 * comment) guarantees one definition, shared, no ODR violation. Previously
 * this struct was `static` in lorie_test.h, giving every TU a private copy
 * (21 distinct `_lorie_runner` symbols confirmed via `nm` in the full binary):
 * a failing ASSERT_* in e.g. test_protocols.c mutated ITS OWN copy, the
 * runner's `jumping` read back as 0, `longjmp` never fired, and the test was
 * unconditionally printed `[ OK ]` — a harness that could never report FAIL.
 * See engram bugfix #163. */
struct lorie_test_runner _lorie_runner = {0, 0, 0, NULL, NULL, {{0}}, 0};

int lorie_run_suite(struct lorie_test_suite* suite) {
    _lorie_runner.current_suite = suite->name;
    LOGI("\n[==========] Suite: %s (%d tests)", suite->name, suite->case_count);

    int suite_failed = 0;
    for (int i = 0; i < suite->case_count; i++) {
        struct lorie_test_case* tc = &suite->cases[i];
        _lorie_runner.current_test = tc->name;
        _lorie_runner.total++;

        LOGI("[ RUN      ] %s::%s", suite->name, tc->name);

        _lorie_runner.jumping = 1;
        if (setjmp(_lorie_runner.jump) == 0) {
            if (suite->setup) suite->setup();
            tc->fn();
            if (suite->teardown) suite->teardown();
            LOGI("[       OK ] %s::%s", suite->name, tc->name);
        } else {
            /* Assertion failed and longjmp'd back */
            if (suite->teardown) suite->teardown();
            suite_failed++;
            LOGI("[  FAILED  ] %s::%s", suite->name, tc->name);
        }
        _lorie_runner.jumping = 0;
    }

    LOGI("[==========] Suite: %s done (%d/%d passed)",
         suite->name, suite->case_count - suite_failed, suite->case_count);
    return suite_failed;
}

int lorie_test_main(int argc, char** argv,
                    struct lorie_test_suite** suites, int suite_count) {
    (void)argc; (void)argv;
    LOGI("========================================");
    LOGI("  Lorie Wayland Test Runner");
    LOGI("========================================");

    int total_failed = 0;
    for (int i = 0; i < suite_count; i++) {
        total_failed += lorie_run_suite(suites[i]);
    }

    LOGI("\n========================================");
    LOGI("  Results: %d passed, %d failed, %d total assertions",
         _lorie_runner.passed, _lorie_runner.failed, _lorie_runner.passed + _lorie_runner.failed);
    LOGI("========================================");

    return total_failed > 0 ? 1 : 0;
}

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
