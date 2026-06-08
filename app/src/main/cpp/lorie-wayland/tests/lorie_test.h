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
    int skipped;
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

/* --- SKIP mechanism (host vs. on-device environment gating) ---
 *
 * Some tests legitimately require capabilities that ONLY exist on a real
 * Android device — e.g. a GPU/EGL driver exposing the
 * EGL_EXT_image_dma_buf_import extension (gates compositor->linux_dmabuf_global
 * creation, see lorie_compositor_create_dmabuf_global / wayland-activity.c),
 * or a real ANativeWindow/Surface to back an EGL window surface (gates
 * lorie_renderer_commit's success path — it returns -1 by design when
 * r->egl_surface == EGL_NO_SURFACE, see renderer.c). Asserting these as hard
 * requirements on a generic host (no GPU/DRI2, no Android Surface — confirmed
 * via the host-side `libEGL warning: egl: failed to create dri2 screen`) is
 * not testing a bug — it is testing "do I have a GPU", which is an
 * environment fact, not a correctness property of the code under test.
 *
 * SKIPPED is reported distinctly from FAILED (own counter, own [ SKIPPED ]
 * line, NOT counted toward `failed` / suite_failed / the process exit code)
 * so a clean host run can end "N passed, 0 failed, M skipped" — honest about
 * what ran vs. what the environment couldn't support, without papering over
 * real failures by mislabeling them "env" (see _LORIE_ASSERT_FAIL for the
 * FAIL path, which remains completely untouched/unweakened by this).
 *
 * Reuses the SAME shared `_lorie_runner` + setjmp/longjmp machinery that
 * _LORIE_ASSERT_FAIL relies on (external linkage, single definition in
 * test_framework.c — see bug #163's ODR lesson at the top of this file): a
 * SKIP unwinds the current test via `longjmp(_lorie_runner.jump, 2)`, and
 * lorie_run_suite distinguishes the setjmp return value (1 == failed,
 * 2 == skipped) to print/count/report each outcome correctly. No new global
 * state, no new TU, zero ODR risk — just one more longjmp code path through
 * machinery that is already proven cross-TU-safe. */
#define _LORIE_SKIP(msg) do { \
    LOGI("SKIPPED at %s:%d in %s::%s", \
         __FILE__, __LINE__, _lorie_runner.current_suite, _lorie_runner.current_test); \
    LOGI("  %s", msg); \
    _lorie_runner.skipped++; \
    if (_lorie_runner.jumping) longjmp(_lorie_runner.jump, 2); \
} while(0)

#define LORIE_SKIP(reason) _LORIE_SKIP(reason)

/* True only when explicitly opted in via LORIE_TEST_DEVICE=1 (or any
 * non-empty value) — e.g. when running on a real Android device/emulator
 * with a genuine GPU/EGL stack and Activity-provided Surface. Host CI and
 * local dev runs (this var unset) take the SKIP path for gated assertions;
 * on-device runs take the real-assertion path, so coverage is never lost,
 * only relocated to where it can actually be exercised. */
static inline int lorie_test_running_on_device(void) {
    const char* v = getenv("LORIE_TEST_DEVICE");
    return v && v[0] != '\0';
}

/* Skip the current test (distinctly, not as a failure) unless running with
 * LORIE_TEST_DEVICE set. Use this to gate assertions that require real
 * on-device GPU/EGL/Surface capabilities the host cannot provide. */
#define LORIE_SKIP_UNLESS_DEVICE(reason) do { \
    if (!lorie_test_running_on_device()) { \
        LORIE_SKIP(reason); \
    } \
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
