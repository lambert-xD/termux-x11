#ifndef MOCK_ANDROID_LOG_H
#define MOCK_ANDROID_LOG_H

#include <stdio.h>
#include <stdarg.h>

#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_ERROR 6

#ifdef __cplusplus
extern "C" {
#endif

/* Host mock for liblog's __android_log_print(): the test runner (lorie_test.h)
 * reports suite/case results and assertion failures exclusively through
 * LOGI()/LOGE(), which expand to this function. On Android these go to
 * logcat; on host builds (no liblog) we forward them to stdout/stderr so
 * `./lorie-wayland-tests` produces visible PASS/FAIL evidence instead of
 * silently swallowing all test output. */
static inline int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    va_list ap;
    /* Always write to stdout (regardless of priority) and flush immediately
     * so INFO and ERROR lines stay in chronological order — mixing buffered
     * stdout with unbuffered stderr would interleave them unpredictably. */
    FILE *out = stdout;
    (void)prio;
    va_start(ap, fmt);
    if (tag && *tag)
        fprintf(out, "[%s] ", tag);
    vfprintf(out, fmt, ap);
    fputc('\n', out);
    fflush(out);
    va_end(ap);
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif
