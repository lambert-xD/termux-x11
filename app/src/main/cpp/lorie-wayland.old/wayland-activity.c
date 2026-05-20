#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <errno.h>
#include <jni.h>
#include <android/looper.h>
#include <wchar.h>
#include <linux/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include "wayland-lorie.h"

#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma ide diagnostic ignored "cppcoreguidelines-narrowing-conversions"
#pragma ide diagnostic ignored "ConstantFunctionResult"
#define log(prio, ...) __android_log_print(ANDROID_LOG_ ## prio, "WaylandNative", __VA_ARGS__)

// Wayland compositor connection file descriptor
static volatile int wayland_conn_fd = -1;

static struct {
    jclass self;
    jmethodID getInstance, clientConnectedStateChanged, resetIme;
} WaylandActivity = {0};

static JNIEnv *guienv = NULL;
static jobject globalThiz = NULL;

static jclass FindClassOrDie(JNIEnv *env, const char* name) {
    jclass clazz = (*env)->FindClass(env, name);
    if (!clazz) {
        char buffer[1024] = {0};
        sprintf(buffer, "class %s not found", name);
        log(ERROR, "%s", buffer);
        (*env)->FatalError(env, buffer);
        return NULL;
    }
    return (*env)->NewGlobalRef(env, clazz);
}

static jclass FindMethodOrDie(JNIEnv *env, jclass clazz, const char* name, const char* signature, jboolean isStatic) {
    __typeof__((*env)->GetMethodID) getMethodID = isStatic ? (*env)->GetStaticMethodID : (*env)->GetMethodID;
    jmethodID method = getMethodID(env, clazz, name, signature);
    if (!method) {
        char buffer[1024] = {0};
        sprintf(buffer, "method %s %s not found", name, signature);
        log(ERROR, "%s", buffer);
        (*env)->FatalError(env, buffer);
        return NULL;
    }
    return method;
}

static void nativeInit(JNIEnv *env, jobject thiz) {
    JavaVM* vm;
    if (!WaylandActivity.self) {
        WaylandActivity.self = FindClassOrDie(env, "com/termux/x11/WaylandActivity");
        WaylandActivity.getInstance = FindMethodOrDie(env, WaylandActivity.self, "getInstance", "()Lcom/termux/x11/WaylandActivity;", JNI_TRUE);
        WaylandActivity.clientConnectedStateChanged = FindMethodOrDie(env, WaylandActivity.self, "clientConnectedStateChanged", "()V", JNI_FALSE);
        WaylandActivity.resetIme = FindMethodOrDie(env, (*env)->GetObjectClass(env, thiz), "resetIme", "()V", JNI_FALSE);
    }

    (*env)->GetJavaVM(env, &vm);
    (*vm)->AttachCurrentThread(vm, &guienv, NULL);
    globalThiz = (*guienv)->NewGlobalRef(env, thiz);
}

static int wayland_callback(int fd, int events, __unused void* data) {
    JNIEnv *env = guienv;
    jobject thiz = globalThiz;

    if (events & (ALOOPER_EVENT_ERROR | ALOOPER_EVENT_HANGUP)) {
        jobject instance = (*env)->CallStaticObjectMethod(env, WaylandActivity.self, WaylandActivity.getInstance);
        if (instance)
            (*env)->CallVoidMethod(env, instance, WaylandActivity.clientConnectedStateChanged);

        ALooper_removeFd(ALooper_forThread(), fd);
        close(wayland_conn_fd);
        wayland_conn_fd = -1;
        rendererSetSharedState(NULL);
        rendererRemoveAllBuffers();
        log(DEBUG, "Wayland disconnected");
        return 1;
    }

    if (wayland_conn_fd != -1) {
        lorieEvent e = {0};

        again:
        if (read(wayland_conn_fd, &e, sizeof(e)) == sizeof(e)) {
            switch(e.type) {
                case EVENT_CLIPBOARD_SEND: {
                    if (!e.clipboardSend.count)
                        break;
                    char clipboard[e.clipboardSend.count + 1];
                    memset(clipboard, 0, e.clipboardSend.count + 1);
                    read(wayland_conn_fd, clipboard, sizeof(clipboard));
                    clipboard[e.clipboardSend.count] = 0;
                    log(DEBUG, "Wayland clipboard content (%zu symbols) is %s", strlen(clipboard), clipboard);
                    jmethodID id = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, thiz), "setClipboardText","(Ljava/lang/String;)V");
                    (*env)->CallVoidMethod(env, thiz, id, (*env)->NewStringUTF(env, clipboard));
                    break;
                }
                case EVENT_CLIPBOARD_REQUEST: {
                    (*env)->CallVoidMethod(env, thiz, (*env)->GetMethodID(env, (*env)->GetObjectClass(env, thiz), "requestClipboard", "()V"));
                    break;
                }
                case EVENT_SHARED_SERVER_STATE: {
                    struct lorie_shared_server_state* state = NULL;
                    int stateFd = ancil_recv_fd(wayland_conn_fd);

                    if (stateFd < 0)
                        break;

                    state = mmap(NULL, sizeof(*state), PROT_READ|PROT_WRITE, MAP_SHARED, stateFd, 0);
                    if (!state || state == MAP_FAILED) {
                        log(ERROR, "Failed to map Wayland server state: %s", strerror(errno));
                        state = NULL;
                    }

                    rendererSetSharedState(state);
                    close(stateFd);
                    break;
                }
                case EVENT_ADD_BUFFER: {
                    static LorieBuffer* buffer = NULL;
                    const LorieBuffer_Desc* desc;
                    LorieBuffer_recvHandleFromUnixSocket(wayland_conn_fd, &buffer);
                    desc = LorieBuffer_description(buffer);
                    log(INFO, "Wayland received shared buffer width %d stride %d height %d format %d type %d id %llu", desc->width, desc->stride, desc->height, desc->format, desc->type, desc->id);
                    rendererAddBuffer(buffer);
                    break;
                }
                case EVENT_REMOVE_BUFFER: {
                    rendererRemoveBuffer(e.removeBuffer.id);
                    break;
                }
                case EVENT_WINDOW_FOCUS_CHANGED: {
                    (*env)->CallVoidMethod(env, thiz, WaylandActivity.resetIme);
                }
            }
        }

        int n;
        if (ioctl(wayland_conn_fd, FIONREAD, &n) >= 0 && n > sizeof(e))
            goto again;
    }

    return 1;
}

static void connect_(__unused JNIEnv* env, __unused jobject cls, jint fd) {
    if (wayland_conn_fd != -1) {
        ALooper_removeFd(ALooper_forThread(), wayland_conn_fd);
        close(wayland_conn_fd);
        rendererSetSharedState(NULL);
        rendererRemoveAllBuffers();
        log(DEBUG, "Wayland disconnected");
    }

    if ((wayland_conn_fd = fd) != -1) {
        ALooper_addFd(ALooper_forThread(), fd, 0, ALOOPER_EVENT_INPUT | ALOOPER_EVENT_ERROR | ALOOPER_EVENT_HANGUP, wayland_callback, NULL);
        log(DEBUG, "Wayland connection is successful");
    }
}

static jboolean connected(__unused JNIEnv* env,__unused jclass clazz) {
    return wayland_conn_fd != -1;
}

static void setClipboardSyncEnabled(__unused JNIEnv* env, __unused jobject cls, jboolean enable, __unused jboolean ignored) {
    if (wayland_conn_fd != -1) {
        lorieEvent e = { .clipboardEnable = { .t = EVENT_CLIPBOARD_ENABLE, .enable = enable } };
        write(wayland_conn_fd, &e, sizeof(e));
    }
}

static void sendClipboardAnnounce(__unused JNIEnv *env, __unused jobject thiz) {
    if (wayland_conn_fd != -1) {
        lorieEvent e = { .type = EVENT_CLIPBOARD_ANNOUNCE };
        write(wayland_conn_fd, &e, sizeof(e));
    }
}

static void sendClipboardEvent(JNIEnv *env, __unused jobject thiz, jbyteArray text) {
    if (wayland_conn_fd != -1 && text) {
        jsize length = (*env)->GetArrayLength(env, text);
        jbyte* str = (*env)->GetByteArrayElements(env, text, NULL);
        lorieEvent e = { .clipboardSend = { .t = EVENT_CLIPBOARD_SEND, .count = length } };
        write(wayland_conn_fd, &e, sizeof(e));
        write(wayland_conn_fd, str, length);
        (*env)->ReleaseByteArrayElements(env, text, str, JNI_ABORT);
    }
}

static void sendWindowChange(__unused JNIEnv* env, __unused jobject cls, jint width, jint height, jint framerate, jstring jname) {
    if (wayland_conn_fd != -1) {
        const char *name = (!jname || width <= 0 || height <= 0) ? NULL : (*env)->GetStringUTFChars(env, jname, JNI_FALSE);
        lorieEvent e = { .screenSize = { .t = EVENT_SCREEN_SIZE, .width = width, .height = height, .framerate = framerate, .name_size = (name ? strlen(name) : 0) } };
        write(wayland_conn_fd, &e, sizeof(e));
        if (name) {
            write(wayland_conn_fd, name, strlen(name));
            (*env)->ReleaseStringUTFChars(env, jname, name);
        }
    }
}

static void sendMouseEvent(__unused JNIEnv* env, __unused jobject cls, jfloat x, jfloat y, jint which_button, jboolean button_down, jboolean relative) {
    if (wayland_conn_fd != -1) {
        if (which_button > 0)
            (*env)->CallVoidMethod(env, globalThiz, WaylandActivity.resetIme);
        lorieEvent e = { .mouse = { .t = EVENT_MOUSE, .x = x, .y = y, .detail = which_button, .down = button_down, .relative = relative } };
        write(wayland_conn_fd, &e, sizeof(e));
    }
}

static void sendTouchEvent(__unused JNIEnv* env, __unused jobject cls, jint action, jint id, jint x, jint y) {
    if (wayland_conn_fd != -1 && action != -1) {
        lorieEvent e = { .touch = { .t = EVENT_TOUCH, .type = action, .id = id, .x = x, .y = y } };
        write(wayland_conn_fd, &e, sizeof(e));
    }
}

static void sendStylusEvent(__unused JNIEnv *env, __unused jobject thiz, jfloat x, jfloat y,
                            jint pressure, jint tilt_x, jint tilt_y,
                            jint orientation, jint buttons, jboolean eraser, jboolean mouse) {
    if (wayland_conn_fd != -1) {
        (*env)->CallVoidMethod(env, globalThiz, WaylandActivity.resetIme);
        lorieEvent e = { .stylus = { .t = EVENT_STYLUS, .x = x, .y = y, .pressure = pressure, .tilt_x = tilt_x, .tilt_y = tilt_y, .orientation = orientation, .buttons = buttons, .eraser = eraser, .mouse = mouse } };
        write(wayland_conn_fd, &e, sizeof(e));
    }
}

static void requestStylusEnabled(__unused JNIEnv *env, __unused jclass clazz, jboolean enabled) {
    if (wayland_conn_fd != -1) {
        lorieEvent e = { .stylusEnable = { .t = EVENT_STYLUS_ENABLE, .enable = enabled } };
        write(wayland_conn_fd, &e, sizeof(e));
    }
}

static jboolean sendKeyEvent(__unused JNIEnv* env, __unused jobject cls, jint scan_code, jint key_code, jboolean key_down) {
    if (wayland_conn_fd != -1) {
        int code = (scan_code) ?: android_to_linux_keycode[key_code];
        log(DEBUG, "Wayland sending key: %d (%d %d %d)", code + 8, scan_code, key_code, key_down);
        lorieEvent e = { .key = { .t = EVENT_KEY, .key = code + 8, .state = key_down } };
        write(wayland_conn_fd, &e, sizeof(e));
    }
    return true;
}

static void sendTextEvent(JNIEnv *env, __unused jobject thiz, jbyteArray text) {
    if (wayland_conn_fd != -1 && text) {
        jsize length = (*env)->GetArrayLength(env, text);
        jbyte *str = (*env)->GetByteArrayElements(env, text, NULL);
        char *p = (char*) str;
        mbstate_t mbstate = { 0 };
        if (!length)
            return;

        log(DEBUG, "Wayland parsing text: %.*s", length, str);

        while (*p) {
            wchar_t wc;
            size_t len = mbrtowc(&wc, p, MB_CUR_MAX, &mbstate);

            if (len == (size_t)-1 || len == (size_t)-2) {
                log(ERROR, "Invalid UTF-8 sequence encountered");
                break;
            }

            if (len == 0)
                break;

            log(DEBUG, "Wayland sending unicode event: %lc (U+%X)", wc, wc);
            lorieEvent e = { .unicode = { .t = EVENT_UNICODE, .code = wc } };
            write(wayland_conn_fd, &e, sizeof(e));
            p += len;
            if (p - (char*) str >= length)
                break;
            usleep(2500);
        }

        (*env)->ReleaseByteArrayElements(env, text, str, JNI_ABORT);
    }
}

static jboolean requestConnection(__unused JNIEnv *env, __unused jclass clazz) {
    // TODO: Implement Wayland connection request
    return JNI_FALSE;
}

static void startLogcat(JNIEnv *env, __unused jobject cls, jint fd) {
    log(DEBUG, "Starting Wayland logcat with output to given fd");

    switch(fork()) {
        case -1:
            log(ERROR, "fork: %s", strerror(errno));
            return;
        case 0:
            dup2(fd, 1);
            dup2(fd, 2);

            prctl(PR_SET_PDEATHSIG, SIGTERM);
            char buf[64] = {0};
            sprintf(buf, "--pid=%d", getppid());
            execl("/system/bin/logcat", "logcat", buf, NULL);
            log(ERROR, "exec logcat: %s", strerror(errno));
            (*env)->FatalError(env, "Exiting");
    }
}

// JNI_OnLoad for Wayland - registers native methods for LorieWaylandView
// This is called explicitly when loading the Wayland library
JNIEXPORT jint JNI_OnLoad(JavaVM *vm, __unused void *reserved) {
    JNIEnv* env;
    static JNINativeMethod methods[] = {
            {"nativeInit", "()V", (void *)&nativeInit},
            {"surfaceChanged", "(Landroid/view/Surface;)V", (void *)&rendererSetWindow},
            {"setViewport", "(IIIIII)V", (void *)&rendererSetViewport},
            {"setFiltering", "(I)V", (void *)&rendererSetFiltering},
            {"connect", "(I)V", (void *)&connect_},
            {"connected", "()Z", (void *)&connected},
            {"startLogcat", "(I)V", (void *)&startLogcat},
            {"setClipboardSyncEnabled", "(ZZ)V", (void *)&setClipboardSyncEnabled},
            {"sendClipboardAnnounce", "()V", (void *)&sendClipboardAnnounce},
            {"sendClipboardEvent", "([B)V", (void *)&sendClipboardEvent},
            {"sendWindowChange", "(IIILjava/lang/String;)V", (void *)&sendWindowChange},
            {"sendMouseEvent", "(FFIZZ)V", (void *)&sendMouseEvent},
            {"sendTouchEvent", "(IIII)V", (void *)&sendTouchEvent},
            {"sendStylusEvent", "(FFIIIIIZZ)V", (void *)&sendStylusEvent},
            {"requestStylusEnabled", "(Z)V", (void *)&requestStylusEnabled},
            {"sendKeyEvent", "(IIZI)Z", (void *)&sendKeyEvent},
            {"sendTextEvent", "([B)V", (void *)&sendTextEvent},
            {"requestConnection", "()Z", (void *)&requestConnection},
    };
    (*vm)->AttachCurrentThread(vm, &env, NULL);
    jclass cls = (*env)->FindClass(env, "com/termux/x11/LorieWaylandView");
    (*env)->RegisterNatives(env, cls, methods, sizeof(methods)/sizeof(methods[0]));

    rendererInit(env);

    return JNI_VERSION_1_6;
}
