/*
 * Host stubs for libandroid (NDK) functions referenced by production code.
 *
 * lorie-wayland-tests links and runs on a plain Linux host where libandroid
 * is unavailable. The compositor only needs these symbols to be *resolvable*
 * at link time — surface/window lifecycle is exercised through the lorie_*
 * abstractions in unit tests, not through real ANativeWindow instances.
 */

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <stdint.h>

void ANativeWindow_acquire(ANativeWindow *window) {
    (void)window;
}

void ANativeWindow_release(ANativeWindow *window) {
    (void)window;
}

int32_t ANativeWindow_getWidth(ANativeWindow *window) {
    (void)window;
    return 0;
}

int32_t ANativeWindow_getHeight(ANativeWindow *window) {
    (void)window;
    return 0;
}

ANativeWindow *ANativeWindow_fromSurface(JNIEnv *env, jobject surface) {
    (void)env;
    (void)surface;
    return NULL;
}
