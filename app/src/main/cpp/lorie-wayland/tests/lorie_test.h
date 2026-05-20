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

static struct lorie_test_runner _lorie_runner = {0, 0, 0, NULL, NULL, {{0}}, 0};

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

/* --- Runner --- */

static inline int lorie_run_suite(struct lorie_test_suite* suite) {
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

static inline int lorie_test_main(int argc, char** argv,
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

#endif /* LORIE_TEST_H */
