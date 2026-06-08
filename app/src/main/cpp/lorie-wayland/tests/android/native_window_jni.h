/* Mock android/native_window_jni.h for host unit tests.
 *
 * Provides the minimal JNI <-> ANativeWindow bridge declaration referenced by
 * wayland-activity.c so the file can be parsed on a host (non-Android)
 * toolchain. The real implementation lives in libandroid and is unavailable
 * on host; lorie-wayland-tests provides its own stub for ANativeWindow_fromSurface
 * (see test_jni.c / test_buffer_stubs.c) when needed at link time.
 */

#ifndef MOCK_ANDROID_NATIVE_WINDOW_JNI_H
#define MOCK_ANDROID_NATIVE_WINDOW_JNI_H

#include <jni.h>
#include <android/native_window.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ANativeWindow_acquire/_release are declared by android/native_window.h
 * (already included above); only the JNI bridge function lives here. */
ANativeWindow *ANativeWindow_fromSurface(JNIEnv *env, jobject surface);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_ANDROID_NATIVE_WINDOW_JNI_H */
