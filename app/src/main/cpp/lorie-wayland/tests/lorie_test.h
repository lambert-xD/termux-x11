#ifndef LORIE_TEST_H
#define LORIE_TEST_H

#include <android/log.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LORIE_TEST_TAG "LorieTest"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LORIE_TEST_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LORIE_TEST_TAG, __VA_ARGS__)

/* Simple test framework for Android NDK — zero external dependencies.
 * Modeled after the classic xUnit pattern: test suites, setup/teardown,
 * assertions with file/line info, and a minimal runner.
 */

struct lorie_test_s;
struct lorie_test_suite;

struct lorie_test_case {
    const char* name;
    void (*fn)(void);
    const char* file;
    int line;
};

struct lorie_test_suite {
    const char* name;
    void (*setup)(void);
    void (*teardown)(void);
    struct lorie_test_case* cases;
    int case_count;
    int capacity;
};

/* Runner state */
struct lorie_test_runner {
    int total;
    int passed;
    int failed;
    const char* current_suite;
    const char* current_test;
    jmp_buf jump;
    int jumping;
};

/* SINGLE shared instance across every translation unit (defined once in
 * test_framework.c). This MUST have external linkage: assertions fire from
 * test_*.c TUs (e.g. test_protocols.c) while the runner loop — which owns
 * `jump`/`jumping` and inspects `passed`/`failed` — lives in the main-driver
 * TU (test_main.c / test_main_protocols_only.c). If each TU got its own
 * `static` copy (the previous bug — confirmed via `nm` to produce 21 distinct
 * `_lorie_runner` symbols in the full binary), a failing ASSERT_* would mutate
 * a private copy that the runner never observes: `jumping` would read back as
 * 0, `longjmp` would never fire across the TU boundary, and the test would be
 * unconditionally reported `[ OK ]` with the summary frozen at
 * "0 passed, 0 failed, 0 total assertions" — a false-GREEN harness that can
 * never report a failure. See engram bugfix #163. */
extern struct lorie_test_runner _lorie_runner;

/* --- Assertions --- */

#define _LORIE_ASSERT_FAIL(msg) do { \
    LOGE("ASSERTION FAILED at %s:%d in %s::%s", \
         __FILE__, __LINE__, _lorie_runner.current_suite, _lorie_runner.current_test); \
    LOGE("  %s", msg); \
    _lorie_runner.failed++; \
    if (_lorie_runner.jumping) longjmp(_lorie_runner.jump, 1); \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        char _buf[256]; \
        snprintf(_buf, sizeof(_buf), "ASSERT_TRUE(%s) failed", #cond); \
        _LORIE_ASSERT_FAIL(_buf); \
    } else { \
        _lorie_runner.passed++; \
    } \
} while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ_INT(expected, actual) do { \
    int _e = (expected); \
    int _a = (actual); \
    if (_e != _a) { \
        char _buf[256]; \
        snprintf(_buf, sizeof(_buf), "ASSERT_EQ_INT(%s, %s) expected %d got %d", \
                 #expected, #actual, _e, _a); \
        _LORIE_ASSERT_FAIL(_buf); \
    } else { \
        _lorie_runner.passed++; \
    } \
} while(0)

#define ASSERT_EQ_PTR(expected, actual) do { \
    const void* _e = (expected); \
    const void* _a = (actual); \
    if (_e != _a) { \
        char _buf[256]; \
        snprintf(_buf, sizeof(_buf), "ASSERT_EQ_PTR(%s, %s) expected %p got %p", \
                 #expected, #actual, _e, _a); \
        _LORIE_ASSERT_FAIL(_buf); \
    } else { \
        _lorie_runner.passed++; \
    } \
} while(0)

#define ASSERT_NULL(ptr) ASSERT_EQ_PTR(NULL, ptr)
#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        char _buf[256]; \
        snprintf(_buf, sizeof(_buf), "ASSERT_NOT_NULL(%s) failed", #ptr); \
        _LORIE_ASSERT_FAIL(_buf); \
    } else { \
        _lorie_runner.passed++; \
    } \
} while(0)

#define ASSERT_EQ_STR(expected, actual) do { \
    const char* _e = (expected); \
    const char* _a = (actual); \
    if (_e == NULL || _a == NULL || strcmp(_e, _a) != 0) { \
        char _buf[256]; \
        snprintf(_buf, sizeof(_buf), "ASSERT_EQ_STR(%s, %s) expected \"%s\" got \"%s\"", \
                 #expected, #actual, _e ? _e : "(null)", _a ? _a : "(null)"); \
        _LORIE_ASSERT_FAIL(_buf); \
    } else { \
        _lorie_runner.passed++; \
    } \
} while(0)

/* --- Suite helpers --- */

static inline void lorie_suite_init(struct lorie_test_suite* suite, const char* name,
                                     void (*setup)(void), void (*teardown)(void)) {
    suite->name = name;
    suite->setup = setup;
    suite->teardown = teardown;
    suite->cases = NULL;
    suite->case_count = 0;
    suite->capacity = 0;
}

static inline void lorie_suite_add(struct lorie_test_suite* suite,
                                    const char* name, void (*fn)(void),
                                    const char* file, int line) {
    if (suite->case_count >= suite->capacity) {
        suite->capacity = suite->capacity ? suite->capacity * 2 : 4;
        suite->cases = (struct lorie_test_case*)realloc(
            suite->cases, suite->capacity * sizeof(struct lorie_test_case));
    }
    suite->cases[suite->case_count++] = (struct lorie_test_case){name, fn, file, line};
}

#define SUITE_ADD(suite, fn) \
    lorie_suite_add((suite), #fn, (fn), __FILE__, __LINE__)

/* --- Runner ---
 *
 * Declared here, DEFINED ONCE in test_framework.c (single TU). They must NOT
 * be `static`/`static inline`: the runner loop owns `_lorie_runner.jump` /
 * `.jumping` and is invoked from the main-driver TU (test_main.c /
 * test_main_protocols_only.c), while `_LORIE_ASSERT_FAIL` (which calls
 * `longjmp(_lorie_runner.jump, ...)`) is expanded inside test_*.c TUs (e.g.
 * test_protocols.c). A single shared definition with external linkage is the
 * only way `longjmp` can unwind back into `lorie_run_suite`'s `setjmp` across
 * those TU boundaries. See engram bugfix #163. */

extern int lorie_run_suite(struct lorie_test_suite* suite);

extern int lorie_test_main(int argc, char** argv,
                           struct lorie_test_suite** suites, int suite_count);

#endif /* LORIE_TEST_H */
