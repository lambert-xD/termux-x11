/*
 * Lorie Wayland Compositor — JNI Bridge
 *
 * Connects Java UI layer to native compositor.
 * Fixes from fresh review:
 *   - No JNI_OnLoad conflict (dynamic registration via nativeInit)
 *   - Clipboard bounded with calloc, VLA removed
 *   - Keycode bounds checked before array access
 *   - Text iteration bounded by length
 */

#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "compositor.h"
#include "input.h"
#include "renderer.h"
#include "keymap.h"

#define LOG_TAG "WaylandJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define MAX_CLIPBOARD_SIZE (1024 * 1024)

static struct lorie_compositor *g_compositor = NULL;
static struct lorie_renderer *g_renderer = NULL;

/* Forward declarations for dynamic registration */
JNIEXPORT void JNICALL Java_com_termux_x11_LorieWaylandView_surfaceChanged(JNIEnv*, jobject, jobject);
JNIEXPORT void JNICALL Java_com_termux_x11_LorieWaylandView_sendMouseEvent(JNIEnv*, jobject, jfloat, jfloat, jint, jboolean, jboolean);
JNIEXPORT void JNICALL Java_com_termux_x11_LorieWaylandView_sendTouchEvent(JNIEnv*, jobject, jint, jint, jint, jint);
JNIEXPORT jboolean JNICALL Java_com_termux_x11_LorieWaylandView_sendKeyEvent(JNIEnv*, jobject, jint, jint, jboolean);
JNIEXPORT void JNICALL Java_com_termux_x11_LorieWaylandView_sendTextEvent(JNIEnv*, jobject, jbyteArray);
JNIEXPORT void JNICALL Java_com_termux_x11_LorieWaylandView_sendClipboardEvent(JNIEnv*, jobject, jbyteArray);

/* Exported helpers for unit tests */
int lorie_clipboard_validate_size(uint32_t count) { return count <= MAX_CLIPBOARD_SIZE; }
int lorie_keycode_valid(int key_code) { return key_code >= 0 && key_code < 304; }
const int lorie_wayland_native_method_count = 6;

JNIEXPORT void JNICALL
Java_com_termux_x11_LorieWaylandView_nativeInit(JNIEnv *env, jclass clazz) {
    JNINativeMethod methods[] = {
        {"surfaceChanged", "(Landroid/view/Surface;)V",
         (void*)&Java_com_termux_x11_LorieWaylandView_surfaceChanged},
        {"sendMouseEvent", "(FFIZZ)V",
         (void*)&Java_com_termux_x11_LorieWaylandView_sendMouseEvent},
        {"sendTouchEvent", "(IIII)V",
         (void*)&Java_com_termux_x11_LorieWaylandView_sendTouchEvent},
        {"sendKeyEvent", "(IIZ)Z",
         (void*)&Java_com_termux_x11_LorieWaylandView_sendKeyEvent},
        {"sendTextEvent", "([B)V",
         (void*)&Java_com_termux_x11_LorieWaylandView_sendTextEvent},
        {"sendClipboardEvent", "([B)V",
         (void*)&Java_com_termux_x11_LorieWaylandView_sendClipboardEvent},
    };
    if ((*env)->RegisterNatives(env, clazz, methods,
                                sizeof(methods)/sizeof(methods[0])) != 0) {
        LOGE("Failed to register native methods");
    }
}

JNIEXPORT void JNICALL
Java_com_termux_x11_LorieWaylandView_surfaceChanged(JNIEnv *env, jobject thiz,
                                                     jobject surface) {
    (void)thiz;
    ANativeWindow *win = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    if (g_compositor) lorie_compositor_set_window(g_compositor, win);
    if (g_renderer)   lorie_renderer_set_window(g_renderer, win);
    if (win) ANativeWindow_release(win);
}

JNIEXPORT void JNICALL
Java_com_termux_x11_LorieWaylandView_sendMouseEvent(JNIEnv *env, jobject thiz,
    jfloat x, jfloat y, jint button, jboolean down, jboolean relative) {
    (void)env; (void)thiz; (void)relative;
    if (!g_compositor || !g_compositor->input) return;
    lorie_input_pointer_motion(g_compositor->input, x, y);
    if (button > 0)
        lorie_input_pointer_button(g_compositor->input,
                                   (uint32_t)button, down ? 1 : 0);
}

JNIEXPORT void JNICALL
Java_com_termux_x11_LorieWaylandView_sendTouchEvent(JNIEnv *env, jobject thiz,
    jint action, jint id, jint x, jint y) {
    (void)env; (void)thiz;
    if (!g_compositor || !g_compositor->input) return;
    switch (action) {
    case 0:
        lorie_input_touch_down(g_compositor->input, (uint32_t)id,
                               (float)x, (float)y);
        break;
    case 1:
        lorie_input_touch_up(g_compositor->input, (uint32_t)id);
        break;
    case 2:
        lorie_input_touch_motion(g_compositor->input, (uint32_t)id,
                                 (float)x, (float)y);
        break;
    }
}

JNIEXPORT jboolean JNICALL
Java_com_termux_x11_LorieWaylandView_sendKeyEvent(JNIEnv *env, jobject thiz,
    jint scanCode, jint keyCode, jboolean down) {
    (void)env; (void)thiz; (void)scanCode;
    if (!g_compositor || !g_compositor->input) return JNI_FALSE;
    if (!lorie_keycode_valid(keyCode)) return JNI_FALSE;
    uint32_t code = (uint32_t)android_to_linux_keycode[keyCode];
    lorie_input_keyboard_key(g_compositor->input, code + 8, down ? 1 : 0);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_termux_x11_LorieWaylandView_sendTextEvent(JNIEnv *env, jobject thiz,
                                                    jbyteArray text) {
    (void)thiz;
    if (!g_compositor || !g_compositor->input || !text) return;
    jsize length = (*env)->GetArrayLength(env, text);
    if (length <= 0) return;
    jbyte *bytes = (*env)->GetByteArrayElements(env, text, NULL);
    if (!bytes) return;
    for (jsize i = 0; i < length; i++) {
        uint32_t c = (uint8_t)bytes[i];
        if (c < 32) continue;
        lorie_input_keyboard_key(g_compositor->input, c, 1);
        lorie_input_keyboard_key(g_compositor->input, c, 0);
    }
    (*env)->ReleaseByteArrayElements(env, text, bytes, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_termux_x11_LorieWaylandView_sendClipboardEvent(JNIEnv *env, jobject thiz,
                                                         jbyteArray text) {
    (void)thiz;
    if (!text) return;
    jsize length = (*env)->GetArrayLength(env, text);
    if (length < 0 || !lorie_clipboard_validate_size((uint32_t)length)) return;
    jbyte *bytes = (*env)->GetByteArrayElements(env, text, NULL);
    if (!bytes) return;
    char *clipboard = calloc((size_t)length + 1, 1);
    if (clipboard) {
        memcpy(clipboard, bytes, (size_t)length);
        clipboard[length] = 0;
        LOGI("Clipboard received (%zd bytes)", (size_t)length);
        free(clipboard);
    }
    (*env)->ReleaseByteArrayElements(env, text, bytes, JNI_ABORT);
}

JNIEXPORT jboolean JNICALL
Java_com_termux_x11_WaylandEntryPoint_start(JNIEnv *env, jclass clazz,
                                            jobjectArray args) {
    (void)env; (void)clazz; (void)args;
    if (g_compositor) return JNI_TRUE;
    g_compositor = lorie_compositor_create();
    if (!g_compositor) return JNI_FALSE;
    g_renderer = lorie_renderer_create();
    if (!g_renderer || lorie_renderer_init(g_renderer) != 0) goto fail;
    if (lorie_compositor_start(g_compositor) != 0) goto fail;
    return JNI_TRUE;
fail:
    if (g_renderer) {
        lorie_renderer_fini(g_renderer);
        lorie_renderer_destroy(g_renderer);
        g_renderer = NULL;
    }
    lorie_compositor_destroy(g_compositor);
    g_compositor = NULL;
    return JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_termux_x11_WaylandEntryPoint_stop(JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    if (g_compositor) {
        lorie_compositor_stop(g_compositor);
        lorie_compositor_destroy(g_compositor);
        g_compositor = NULL;
    }
    if (g_renderer) {
        lorie_renderer_fini(g_renderer);
        lorie_renderer_destroy(g_renderer);
        g_renderer = NULL;
    }
}

JNIEXPORT jboolean JNICALL
Java_com_termux_x11_WaylandEntryPoint_connected(JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    return g_compositor ? JNI_TRUE : JNI_FALSE;
}
