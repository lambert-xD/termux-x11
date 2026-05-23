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
#include <sys/stat.h>
#include <errno.h>
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
static JavaVM *g_jvm = NULL;
static jobject g_lorie_view = NULL;
static pthread_mutex_t g_jni_mutex = PTHREAD_MUTEX_INITIALIZER;

static void clipboard_callback(const char *text, size_t len, void *user_data) {
    (void)user_data;
    if (!text || len == 0) return;

    JNIEnv *env = NULL;
    JavaVMAttachArgs args = {JNI_VERSION_1_6, "WaylandClipboardThread", NULL};

    int result = (*g_jvm)->AttachCurrentThread(g_jvm, &env, &args);
    if (result != 0 || !env) {
        LOGE("Failed to attach JNI thread for clipboard");
        return;
    }

    pthread_mutex_lock(&g_jni_mutex);
    jbyteArray jbytes = (*env)->NewByteArray(env, (jsize)len);
    if (jbytes) {
        (*env)->SetByteArrayRegion(env, jbytes, 0, (jsize)len, (jbyte*)text);
        (*env)->CallStaticVoidMethod(env, g_lorie_view,
            (*env)->GetStaticMethodID(env, g_lorie_view,
                "setClipboardText", "([B)V"),
            jbytes);
        (*env)->DeleteLocalRef(env, jbytes);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionDescribe(env);
            (*env)->ExceptionClear(env);
        }
    }
    pthread_mutex_unlock(&g_jni_mutex);

    (*g_jvm)->DetachCurrentThread(g_jvm);
}

/* Forward declarations for dynamic registration */
JNIEXPORT void JNICALL Java_com_termux_x11_LorieWaylandView_surfaceChanged(JNIEnv*, jobject, jobject, jint, jint);
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
        {"surfaceChanged", "(Landroid/view/Surface;II)V",
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
                                                     jobject surface,
                                                     jint width, jint height) {
    (void)thiz;
    ANativeWindow *win = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    if (g_compositor) {
        lorie_compositor_set_window(g_compositor, win);
        if (width > 0 && height > 0 && !wl_list_empty(&g_compositor->outputs)) {
            struct lorie_output *output =
                wl_container_of(g_compositor->outputs.next, output, link);
            lorie_output_update_size(output, width, height, output->scale);
        }
    }
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
    if (g_compositor && g_compositor->clipboard) {
        lorie_clipboard_send_android_text(g_compositor->clipboard,
                                          (const char *)bytes, (size_t)length);
        LOGI("Clipboard forwarded to Wayland (%zd bytes)", (size_t)length);
    }
    (*env)->ReleaseByteArrayElements(env, text, bytes, JNI_ABORT);
}



int lorie_setup_wayland_runtime_dir(void) {
    const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
    const char *tmpdir = getenv("TMPDIR");
    char runtime_dir[1024] = {0};

    if (xdg_runtime && xdg_runtime[0]) {
        strncpy(runtime_dir, xdg_runtime, sizeof(runtime_dir) - 1);
    } else if (tmpdir && tmpdir[0]) {
        strncpy(runtime_dir, tmpdir, sizeof(runtime_dir) - 1);
    } else if (access("/data/data/com.termux/files/usr/tmp", F_OK) == 0) {
        strcpy(runtime_dir, "/data/data/com.termux/files/usr/tmp");
    } else if (access("/tmp", F_OK) == 0) {
        strcpy(runtime_dir, "/tmp");
    }

    if (runtime_dir[0]) {
        if (mkdir(runtime_dir, 0700) != 0 && errno != EEXIST) {
            LOGE("Failed to create runtime dir %s: %s", runtime_dir, strerror(errno));
            return -1;
        }
        if (chmod(runtime_dir, 0700) != 0) {
            LOGE("Failed to chmod runtime dir %s: %s", runtime_dir, strerror(errno));
            return -1;
        }
        setenv("XDG_RUNTIME_DIR", runtime_dir, 1);
    }

    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    if (!wayland_display || !wayland_display[0]) {
        wayland_display = "wayland-0";
        setenv("WAYLAND_DISPLAY", wayland_display, 1);
    }
    return 0;
}

JNIEXPORT jboolean JNICALL
Java_com_termux_x11_WaylandEntryPoint_start(JNIEnv *env, jclass clazz,
                                            jobjectArray args) {
    (void)args;
    if (g_compositor) return JNI_TRUE;

    /* Store JavaVM for clipboard callback */
    (*env)->GetJavaVM(env, &g_jvm);
    jclass lorieViewClass = (*env)->FindClass(env, "com/termux/x11/LorieWaylandView");
    if (lorieViewClass) {
        g_lorie_view = (*env)->NewGlobalRef(env, lorieViewClass);
        (*env)->DeleteLocalRef(env, lorieViewClass);
    }

    g_compositor = lorie_compositor_create();
    if (!g_compositor) return JNI_FALSE;
    g_renderer = lorie_renderer_create();
    if (!g_renderer || lorie_renderer_init(g_renderer) != 0) goto fail;
    g_compositor->renderer = g_renderer;
    if (lorie_renderer_has_dmabuf_import(g_renderer)) {
        lorie_compositor_create_dmabuf_global(g_compositor);
    }

    /* Register clipboard callback for Wayland→Android forwarding */
    if (g_compositor->clipboard) {
        lorie_clipboard_set_text_callback(g_compositor->clipboard, clipboard_callback, NULL);
    }

    /* Create default output so wl_output global exists */
    if (wl_list_empty(&g_compositor->outputs)) {
        lorie_output_create(g_compositor, 1920, 1080, 1);
    }

    if (lorie_setup_wayland_runtime_dir() != 0) goto fail;
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    lorie_compositor_set_socket_name(g_compositor,
        wayland_display ? wayland_display : "wayland-0");

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
    if (g_lorie_view) {
        JNIEnv *env = NULL;
        if ((*g_jvm)->GetEnv(g_jvm, (void**)&env, JNI_VERSION_1_6) == JNI_OK) {
            (*env)->DeleteGlobalRef(env, g_lorie_view);
        }
        g_lorie_view = NULL;
    }
    g_jvm = NULL;
    return JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_termux_x11_WaylandEntryPoint_stop(JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    if (g_compositor) {
        g_compositor->renderer = NULL;
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
    return (g_compositor && g_compositor->running) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_termux_x11_WaylandCmdEntryPoint_start(JNIEnv *env, jclass clazz,
                                               jobjectArray args) {
    (void)args;
    if (g_compositor) return JNI_TRUE;

    /* Store JavaVM for clipboard callback */
    (*env)->GetJavaVM(env, &g_jvm);
    jclass lorieViewClass = (*env)->FindClass(env, "com/termux/x11/LorieWaylandView");
    if (lorieViewClass) {
        g_lorie_view = (*env)->NewGlobalRef(env, lorieViewClass);
        (*env)->DeleteLocalRef(env, lorieViewClass);
    }

    g_compositor = lorie_compositor_create();
    if (!g_compositor) return JNI_FALSE;
    g_renderer = lorie_renderer_create();
    if (!g_renderer || lorie_renderer_init(g_renderer) != 0) goto fail_cmd;
    g_compositor->renderer = g_renderer;
    if (lorie_renderer_has_dmabuf_import(g_renderer)) {
        lorie_compositor_create_dmabuf_global(g_compositor);
    }

    /* Register clipboard callback for Wayland→Android forwarding */
    if (g_compositor->clipboard) {
        lorie_clipboard_set_text_callback(g_compositor->clipboard, clipboard_callback, NULL);
    }

    /* Create default output so wl_output global exists */
    if (wl_list_empty(&g_compositor->outputs)) {
        lorie_output_create(g_compositor, 1920, 1080, 1);
    }

    if (lorie_setup_wayland_runtime_dir() != 0) goto fail_cmd;
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    lorie_compositor_set_socket_name(g_compositor,
        wayland_display ? wayland_display : "wayland-0");

    if (lorie_compositor_start(g_compositor) != 0) goto fail_cmd;
    return JNI_TRUE;
fail_cmd:
    if (g_renderer) {
        lorie_renderer_fini(g_renderer);
        lorie_renderer_destroy(g_renderer);
        g_renderer = NULL;
    }
    lorie_compositor_destroy(g_compositor);
    g_compositor = NULL;
    if (g_lorie_view) {
        JNIEnv *env2 = NULL;
        if ((*g_jvm)->GetEnv(g_jvm, (void**)&env2, JNI_VERSION_1_6) == JNI_OK) {
            (*env2)->DeleteGlobalRef(env2, g_lorie_view);
        }
        g_lorie_view = NULL;
    }
    g_jvm = NULL;
    return JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_termux_x11_WaylandCmdEntryPoint_stop(JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    if (g_compositor) {
        g_compositor->renderer = NULL;
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
Java_com_termux_x11_WaylandCmdEntryPoint_connected(JNIEnv *env, jclass clazz) {
    (void)env; (void)clazz;
    return (g_compositor && g_compositor->running) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobject JNICALL
Java_com_termux_x11_WaylandCmdEntryPoint_getWaylandConnection(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    return NULL;
}

JNIEXPORT jobject JNICALL
Java_com_termux_x11_WaylandCmdEntryPoint_getLogcatOutput(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    return NULL;
}

JNIEXPORT void JNICALL
Java_com_termux_x11_WaylandCmdEntryPoint_listenForConnections(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    /* No-op for PR 1/2; Wayland compositor runs in Activity process.
       The command process does not own the Wayland socket.
       Future PR 3 may add IPC bridge logic here. */
}
