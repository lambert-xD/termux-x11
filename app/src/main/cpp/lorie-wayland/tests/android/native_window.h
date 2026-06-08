/* Mock android/native_window.h for host unit tests.
 * Provides minimal type definitions to satisfy compositor.h includes without
 * requiring the Android NDK. */

#ifndef ANDROID_NATIVE_WINDOW_H
#define ANDROID_NATIVE_WINDOW_H

#include <stdint.h>

struct ANativeWindow;
typedef struct ANativeWindow ANativeWindow;

#ifdef __cplusplus
extern "C" {
#endif

void ANativeWindow_acquire(ANativeWindow *window);
void ANativeWindow_release(ANativeWindow *window);
int32_t ANativeWindow_getWidth(ANativeWindow *window);
int32_t ANativeWindow_getHeight(ANativeWindow *window);

#ifdef __cplusplus
}
#endif

#endif /* ANDROID_NATIVE_WINDOW_H */
