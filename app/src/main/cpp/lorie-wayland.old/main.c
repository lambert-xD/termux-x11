#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma ide diagnostic ignored "bugprone-reserved-identifier"
#pragma ide diagnostic ignored "OCUnusedMacroInspection"
#pragma ide diagnostic ignored "EndlessLoop"

#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <libgen.h>
#include <errno.h>
#include <linux/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>

#include "wayland-lorie.h"
#include "../lorie/lorie.h"

#define log(prio, ...) __android_log_print(ANDROID_LOG_ ## prio, "LorieWayland", __VA_ARGS__)

static int argc = 0;
static char** argv = NULL;
static struct lorie_wayland_compositor *g_compositor = NULL;
static volatile int conn_fd = -1;

/* Forward declarations */
static void* compositor_thread_run(void* cookie);
static void handle_android_events(int fd, int ready, void *data);
static bool detectTracer(void);

/* cpu_set_t fallback for environments without sched.h */
#ifndef CPU_SETSIZE
#define CPU_SETSIZE 1024
#define __NCPUBITS  (8 * sizeof(unsigned long))
typedef struct { unsigned long __bits[CPU_SETSIZE / __NCPUBITS]; } cpu_set_t;
static inline void CPU_SET(int cpu, cpu_set_t *set) {
    set->__bits[cpu / __NCPUBITS] |= (1UL << (cpu % __NCPUBITS));
}
#endif
extern int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask);

/* Forward declarations for Wayland functions (headers available at build time) */
struct wl_display;
struct wl_event_loop;
struct wl_event_source;
struct wl_client;
struct wl_listener;
struct wl_list;
struct wl_resource;
extern struct wl_display *wl_display_create(void);
extern void wl_display_destroy(struct wl_display *display);
extern void wl_display_destroy_clients(struct wl_display *display);
extern struct wl_event_loop *wl_display_get_event_loop(struct wl_display *display);
extern const char *wl_display_add_socket(struct wl_display *display, const char *name);
extern void wl_display_run(struct wl_display *display);
extern void wl_display_terminate(struct wl_display *display);
extern int wl_event_loop_dispatch(struct wl_event_loop *loop, int timeout);
extern struct wl_event_source *wl_event_loop_add_fd(struct wl_event_loop *loop,
                                                     int fd, uint32_t mask,
                                                     int (*func)(int, uint32_t, void*),
                                                     void *data);
extern void wl_list_init(struct wl_list *list);

static bool detectTracer(void)
{
    FILE *fp;
    char  line[256];
    int pid = 0;

    fp = fopen("/proc/self/status", "r");
    if (!fp)
        return true;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "TracerPid:", 10) == 0) {
            sscanf(line+10, "%d", &pid);
            break;
        }
    }

    if (pid != 0)
        log(INFO, "Tracer detected");

    fclose(fp);
    return pid != 0;
}

/* Handle POLL variants for Wayland event loop */
#ifndef WL_EVENT_READABLE
#define WL_EVENT_READABLE 0x01
#endif
#ifndef WL_EVENT_WRITABLE
#define WL_EVENT_WRITABLE 0x02
#endif
#ifndef WL_EVENT_HANGUP
#define WL_EVENT_HANGUP 0x04
#endif
#ifndef WL_EVENT_ERROR
#define WL_EVENT_ERROR 0x08
#endif

/*
 * Compositor thread - runs the Wayland event loop
 */
static void* compositor_thread_run(__unused void* cookie) {
    struct lorie_wayland_compositor *compositor = (struct lorie_wayland_compositor*) cookie;
    
    log(INFO, "Wayland compositor thread started");
    
    /* Run the Wayland event loop */
    while (compositor->running) {
        wl_event_loop_dispatch(compositor->event_loop, -1);
    }
    
    log(INFO, "Wayland compositor thread stopped");
    return NULL;
}

/*
 * Android event handler - processes events from the Android UI thread
 */
static void handle_android_events(__unused int fd, __unused int ready, void *data) {
    struct lorie_wayland_compositor *compositor = (struct lorie_wayland_compositor*) data;
    lorieEvent e = {0};
    
    if (ready & POLLERR || ready & POLLHUP) {
        log(ERROR, "Android event socket error");
        return;
    }
    
    if (read(fd, &e, sizeof(e)) == sizeof(e)) {
        switch(e.type) {
            case EVENT_SCREEN_SIZE: {
                char *name = NULL;
                if (e.screenSize.name_size > 0) {
                    name = calloc(1, e.screenSize.name_size + 1);
                    read(fd, name, e.screenSize.name_size);
                }
                log(INFO, "Screen size changed: %dx%d@%dHz name=%s",
                    e.screenSize.width, e.screenSize.height, 
                    e.screenSize.framerate, name ? name : "(null)");
                
                /* Update output configuration */
                compositor->output_config.width = e.screenSize.width;
                compositor->output_config.height = e.screenSize.height;
                compositor->output_config.refresh_rate = e.screenSize.framerate;
                if (name) {
                    strncpy(compositor->output_config.name, name, 
                            sizeof(compositor->output_config.name) - 1);
                    free(name);
                }
                
                /* Notify renderer about the change */
                if (compositor->shared_state) {
                    lorie_wayland_mutex_lock(&compositor->shared_state->lock,
                                             &compositor->shared_state->lockingPid);
                    compositor->shared_state->output_width = e.screenSize.width;
                    compositor->shared_state->output_height = e.screenSize.height;
                    compositor->shared_state->output_refresh = e.screenSize.framerate;
                    compositor->shared_state->drawRequested = 1;
                    pthread_cond_signal(&compositor->shared_state->cond);
                    lorie_wayland_mutex_unlock(&compositor->shared_state->lock,
                                              &compositor->shared_state->lockingPid);
                }
                break;
            }
            
            case EVENT_TOUCH: {
                log(DEBUG, "Touch event: id=%d x=%d y=%d type=%d",
                    e.touch.id, e.touch.x, e.touch.y, e.touch.type);
                lorie_wayland_handle_touch(compositor, e.touch.id, 
                                           e.touch.x, e.touch.y, e.touch.type);
                break;
            }
            
            case EVENT_MOUSE: {
                log(DEBUG, "Mouse event: x=%f y=%f button=%d down=%d",
                    e.mouse.x, e.mouse.y, e.mouse.detail, e.mouse.down);
                if (e.mouse.detail == 0) {
                    /* Motion event */
                    lorie_wayland_handle_mouse(compositor, 
                                               (uint32_t)e.mouse.x, 
                                               (uint32_t)e.mouse.y,
                                               0, false);
                } else {
                    /* Button event */
                    lorie_wayland_handle_mouse(compositor,
                                               (uint32_t)e.mouse.x,
                                               (uint32_t)e.mouse.y,
                                               e.mouse.detail,
                                               e.mouse.down != 0);
                }
                break;
            }
            
            case EVENT_KEY: {
                log(DEBUG, "Key event: key=%d state=%d", e.key.key, e.key.state);
                lorie_wayland_handle_key(compositor, e.key.key, e.key.state != 0);
                break;
            }
            
            case EVENT_STYLUS: {
                log(DEBUG, "Stylus event: x=%f y=%f pressure=%d",
                    e.stylus.x, e.stylus.y, e.stylus.pressure);
                lorie_wayland_handle_stylus(compositor,
                                            e.stylus.x, e.stylus.y,
                                            e.stylus.pressure,
                                            e.stylus.tilt_x, e.stylus.tilt_y,
                                            e.stylus.orientation,
                                            e.stylus.buttons,
                                            e.stylus.eraser != 0);
                break;
            }
            
            case EVENT_STYLUS_ENABLE: {
                log(DEBUG, "Stylus enable: %d", e.stylusEnable.enable);
                /* TODO: Enable/disable stylus device */
                break;
            }
            
            case EVENT_UNICODE: {
                log(DEBUG, "Unicode event: code=%d", e.unicode.code);
                /* TODO: Convert unicode to keysym and send */
                break;
            }
            
            case EVENT_CLIPBOARD_ENABLE: {
                log(DEBUG, "Clipboard enable: %d", e.clipboardEnable.enable);
                /* TODO: Enable/disable clipboard sync */
                break;
            }
            
            case EVENT_CLIPBOARD_ANNOUNCE: {
                log(DEBUG, "Clipboard announce");
                /* TODO: Handle clipboard announce */
                break;
            }
            
            case EVENT_CLIPBOARD_SEND: {
                char *data = calloc(1, e.clipboardSend.count + 1);
                if (data) {
                    read(fd, data, e.clipboardSend.count);
                    log(DEBUG, "Clipboard data received: %zu bytes", e.clipboardSend.count);
                    /* TODO: Set Wayland clipboard */
                    free(data);
                }
                break;
            }
            
            case EVENT_WINDOW_FOCUS_CHANGED: {
                log(DEBUG, "Window focus changed");
                /* TODO: Update focus in compositor */
                break;
            }
            
            default:
                log(DEBUG, "Unknown event type: %d", e.type);
                break;
        }
        
        /* Check if more events are available */
        int n;
        if (ioctl(fd, FIONREAD, &n) >= 0 && n > sizeof(e)) {
            /* More events to process */
        }
    }
}

/*
 * Initialize environment similar to cmdentrypoint.c
 */
static void init_environment(void) {
    /* Handle LD_PRELOAD for termux-exec */
    if (access("/data/data/com.termux/files/usr/lib/libtermux-exec.so", F_OK) == 0 
            && !detectTracer()
            && !getenv("XSTARTUP_LD_PRELOAD"))
        setenv("LD_PRELOAD", "/data/data/com.termux/files/usr/lib/libtermux-exec.so", 1);

    /* Fix TMPDIR if set to /data/local/tmp */
    if (!strcmp("/data/local/tmp", getenv("TMPDIR") ?: ""))
        unsetenv("TMPDIR");

    if (!getenv("TMPDIR")) {
        if (access("/tmp", F_OK) == 0)
            setenv("TMPDIR", "/tmp", 1);
        else if (access("/data/data/com.termux/files/usr/tmp", F_OK) == 0)
            setenv("TMPDIR", "/data/data/com.termux/files/usr/tmp", 1);
    }

    if (!getenv("TMPDIR")) {
        char* error = (char*) "$TMPDIR is not set. Normally it is pointing to /tmp of a container.";
        log(ERROR, "%s", error);
        dprintf(2, "%s\n", error);
        return;
    }

    log(VERBOSE, "Using TMPDIR=\"%s\"", getenv("TMPDIR"));

    /* Set up Wayland socket directory */
    char *tmpdir = getenv("TMPDIR");
    char wayland_dir[1024] = {0};
    snprintf(wayland_dir, sizeof(wayland_dir), "%s/wayland", tmpdir);
    if (access(wayland_dir, F_OK) != 0) {
        mkdir(wayland_dir, 0755);
    }
    
    /* Set WAYLAND_DISPLAY */
    char wayland_display[1024] = {0};
    snprintf(wayland_display, sizeof(wayland_display), "%s/%s", wayland_dir, LORIE_WAYLAND_SOCKET_NAME);
    setenv("WAYLAND_DISPLAY", wayland_display, 1);
    
    /* Set XDG_RUNTIME_DIR if not set */
    if (!getenv("XDG_RUNTIME_DIR")) {
        setenv("XDG_RUNTIME_DIR", tmpdir, 1);
    }
}

/*
 * Parse command-line arguments for the compositor
 */
static void parse_arguments(struct lorie_wayland_compositor *compositor, int argc, char **argv) {
    /* Default configuration */
    compositor->output_config.width = LORIE_WAYLAND_DEFAULT_WIDTH;
    compositor->output_config.height = LORIE_WAYLAND_DEFAULT_HEIGHT;
    compositor->output_config.refresh_rate = LORIE_WAYLAND_DEFAULT_REFRESH_RATE;
    compositor->output_config.scale = LORIE_WAYLAND_DEFAULT_SCALE;
    strncpy(compositor->output_config.name, "LorieWayland-1", 
            sizeof(compositor->output_config.name) - 1);
    compositor->xwayland_enabled = false;
    
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--width") && i + 1 < argc) {
            compositor->output_config.width = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--height") && i + 1 < argc) {
            compositor->output_config.height = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--refresh") && i + 1 < argc) {
            compositor->output_config.refresh_rate = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            compositor->output_config.scale = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--name") && i + 1 < argc) {
            strncpy(compositor->output_config.name, argv[++i],
                    sizeof(compositor->output_config.name) - 1);
        } else if (!strcmp(argv[i], "--xwayland")) {
            compositor->xwayland_enabled = true;
        } else if (!strcmp(argv[i], "--help")) {
            log(INFO, "Usage: %s [options]", argv[0]);
            log(INFO, "Options:");
            log(INFO, "  --width WIDTH      Output width (default: %d)", LORIE_WAYLAND_DEFAULT_WIDTH);
            log(INFO, "  --height HEIGHT    Output height (default: %d)", LORIE_WAYLAND_DEFAULT_HEIGHT);
            log(INFO, "  --refresh HZ       Refresh rate (default: %d)", LORIE_WAYLAND_DEFAULT_REFRESH_RATE);
            log(INFO, "  --scale SCALE      Output scale (default: %.1f)", LORIE_WAYLAND_DEFAULT_SCALE);
            log(INFO, "  --name NAME        Output name (default: LorieWayland-1)");
            log(INFO, "  --xwayland         Enable XWayland support");
            log(INFO, "  --help             Show this help");
        }
    }
    
    log(INFO, "Configuration: %dx%d@%dHz scale=%.1f name=%s xwayland=%s",
        compositor->output_config.width,
        compositor->output_config.height,
        compositor->output_config.refresh_rate,
        compositor->output_config.scale,
        compositor->output_config.name,
        compositor->xwayland_enabled ? "enabled" : "disabled");
}

/*
 * JNI entry point - starts the Wayland compositor
 * Mirrors Java_com_termux_x11_CmdEntryPoint_start
 */
JNIEXPORT jboolean JNICALL
Java_com_termux_x11_WaylandEntryPoint_start(JNIEnv *env, __unused jclass cls, jobjectArray args) {
    pthread_t t;
    JavaVM* vm = NULL;
    
    /* Convert Java args to C argv */
    argc = (*env)->GetArrayLength(env, args) + 1;
    argv = (char**) calloc(argc, sizeof(char*));
    argv[0] = (char*) "LorieWayland";
    for(int i = 1; i < argc; i++) {
        jstring js = (jstring)((*env)->GetObjectArrayElement(env, args, i - 1));
        const char *pjc = (*env)->GetStringUTFChars(env, js, JNI_FALSE);
        argv[i] = (char *) calloc(strlen(pjc) + 1, sizeof(char));
        strcpy((char *) argv[i], pjc);
        (*env)->ReleaseStringUTFChars(env, js, pjc);
    }
    
    /* Set CPU affinity */
    {
        cpu_set_t mask;
        long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
        for (int i = num_cpus/2; i < num_cpus; i++)
            CPU_SET(i, &mask);
        if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1)
            log(ERROR, "Failed to set process affinity: %s", strerror(errno));
    }
    
    /* Start logcat if debugging */
    if (getenv("TERMUX_X11_DEBUG") && !fork()) {
        char pid[32] = {0};
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        sprintf(pid, "%d", getppid());
        execlp("logcat", "logcat", "--pid", pid, NULL);
    }
    
    /* Initialize environment */
    init_environment();
    
    /* Create compositor */
    g_compositor = lorie_wayland_compositor_create();
    if (!g_compositor) {
        log(ERROR, "Failed to create Wayland compositor");
        return JNI_FALSE;
    }
    
    /* Parse arguments */
    parse_arguments(g_compositor, argc, argv);
    
    /* Store JVM reference */
    (*env)->GetJavaVM(env, &vm);
    g_compositor->jvm = vm;
    
    /* Start choreographer for frame callbacks */
    AChoreographer *choreographer = AChoreographer_getInstance();
    AChoreographer_postFrameCallback(choreographer, 
                                     (AChoreographer_frameCallback) lorieChoreographerFrameCallback, 
                                     choreographer);
    
    /* Start compositor thread */
    g_compositor->running = true;
    if (pthread_create(&g_compositor->compositor_thread, NULL, 
                       compositor_thread_run, g_compositor) != 0) {
        log(ERROR, "Failed to create compositor thread: %s", strerror(errno));
        lorie_wayland_compositor_destroy(g_compositor);
        g_compositor = NULL;
        return JNI_FALSE;
    }
    
    /* Start renderer thread */
    if (pthread_create(&g_compositor->renderer_thread, NULL,
                       lorie_wayland_renderer_thread, g_compositor) != 0) {
        log(ERROR, "Failed to create renderer thread: %s", strerror(errno));
        g_compositor->running = false;
        pthread_join(g_compositor->compositor_thread, NULL);
        lorie_wayland_compositor_destroy(g_compositor);
        g_compositor = NULL;
        return JNI_FALSE;
    }
    
    /* Start XWayland if requested */
    if (g_compositor->xwayland_enabled) {
        if (lorie_wayland_xwayland_start(g_compositor) != 0) {
            log(WARN, "Failed to start XWayland, continuing without X11 support");
            g_compositor->xwayland_enabled = false;
        }
    }
    
    log(INFO, "Wayland compositor started successfully");
    return JNI_TRUE;
}

/*
 * JNI entry point - stops the Wayland compositor
 */
JNIEXPORT void JNICALL
Java_com_termux_x11_WaylandEntryPoint_stop(__unused JNIEnv *env, __unused jclass cls) {
    if (!g_compositor) {
        log(WARN, "Compositor not running");
        return;
    }
    
    log(INFO, "Stopping Wayland compositor...");
    
    /* Stop XWayland first */
    if (g_compositor->xwayland_enabled) {
        lorie_wayland_xwayland_stop(g_compositor);
    }
    
    /* Signal threads to stop */
    g_compositor->running = false;
    
    /* Wake up renderer */
    if (g_compositor->shared_state) {
        pthread_cond_signal(&g_compositor->shared_state->cond);
    }
    
    /* Wait for threads */
    pthread_join(g_compositor->renderer_thread, NULL);
    pthread_join(g_compositor->compositor_thread, NULL);
    
    /* Destroy compositor */
    lorie_wayland_compositor_destroy(g_compositor);
    g_compositor = NULL;
    
    log(INFO, "Wayland compositor stopped");
}

/*
 * JNI entry point - get Wayland connection file descriptor
 * Mirrors Java_com_termux_x11_CmdEntryPoint_getXConnection
 */
JNIEXPORT jobject JNICALL
Java_com_termux_x11_WaylandEntryPoint_getWaylandConnection(JNIEnv *env, __unused jclass cls) {
    int client[2];
    jclass ParcelFileDescriptorClass = (*env)->FindClass(env, "android/os/ParcelFileDescriptor");
    jmethodID adoptFd = (*env)->GetStaticMethodID(env, ParcelFileDescriptorClass, 
                                                   "adoptFd", "(I)Landroid/os/ParcelFileDescriptor;");
    
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, client) < 0) {
        log(ERROR, "Failed to create socket pair: %s", strerror(errno));
        return NULL;
    }
    
    /* Store the server side for event handling */
    conn_fd = client[1];
    
    /* Add to Wayland event loop for reading Android events */
    if (g_compositor && g_compositor->event_loop) {
        struct wl_event_source *source = wl_event_loop_add_fd(
            g_compositor->event_loop, client[1], WL_EVENT_READABLE,
            (int (*)(int, unsigned int, void*))handle_android_events, g_compositor);
        if (!source) {
            log(ERROR, "Failed to add fd to event loop");
            close(client[0]);
            close(client[1]);
            return NULL;
        }
    }
    
    return (*env)->CallStaticObjectMethod(env, ParcelFileDescriptorClass, adoptFd, client[0]);
}

/*
 * JNI entry point - check if compositor is running
 */
JNIEXPORT jboolean JNICALL
Java_com_termux_x11_WaylandEntryPoint_connected(__unused JNIEnv *env, __unused jclass clazz) {
    return g_compositor != NULL && g_compositor->running;
}

/*
 * Stub implementations for functions declared in wayland-lorie.h
 * These will be implemented by other workers
 */

struct lorie_wayland_compositor *lorie_wayland_compositor_create(void) {
    struct lorie_wayland_compositor *compositor = calloc(1, sizeof(*compositor));
    if (!compositor) {
        log(ERROR, "Failed to allocate compositor");
        return NULL;
    }
    
    /* Create Wayland display */
    compositor->display = wl_display_create();
    if (!compositor->display) {
        log(ERROR, "Failed to create Wayland display");
        free(compositor);
        return NULL;
    }
    
    compositor->event_loop = wl_display_get_event_loop(compositor->display);
    wl_list_init(&compositor->surfaces);
    
    /* Create shared state */
    compositor->shared_state = lorie_wayland_shared_state_create();
    if (!compositor->shared_state) {
        log(ERROR, "Failed to create shared state");
        wl_display_destroy(compositor->display);
        free(compositor);
        return NULL;
    }
    
    log(INFO, "Wayland compositor created");
    return compositor;
}

void lorie_wayland_compositor_destroy(struct lorie_wayland_compositor *compositor) {
    if (!compositor)
        return;
    
    if (compositor->display) {
        wl_display_destroy_clients(compositor->display);
        wl_display_destroy(compositor->display);
    }
    
    if (compositor->shared_state) {
        lorie_wayland_shared_state_destroy(compositor->shared_state);
    }
    
    free(compositor);
    log(INFO, "Wayland compositor destroyed");
}

int lorie_wayland_compositor_run(struct lorie_wayland_compositor *compositor) {
    if (!compositor || !compositor->display)
        return -1;
    
    const char *socket_name = wl_display_add_socket(compositor->display, LORIE_WAYLAND_SOCKET_NAME);
    if (!socket_name) {
        log(ERROR, "Failed to add Wayland socket");
        return -1;
    }
    
    log(INFO, "Wayland socket: %s", socket_name);
    wl_display_run(compositor->display);
    return 0;
}

void lorie_wayland_compositor_stop(struct lorie_wayland_compositor *compositor) {
    if (compositor && compositor->display) {
        wl_display_terminate(compositor->display);
    }
}

/* Shared state management */
struct lorie_wayland_shared_state *lorie_wayland_shared_state_create(void) {
    struct lorie_wayland_shared_state *state = calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&state->lock, &attr);
    pthread_mutex_init(&state->cursor.lock, &attr);
    pthread_cond_init(&state->cond, NULL);
    
    state->output_width = LORIE_WAYLAND_DEFAULT_WIDTH;
    state->output_height = LORIE_WAYLAND_DEFAULT_HEIGHT;
    state->output_refresh = LORIE_WAYLAND_DEFAULT_REFRESH_RATE;
    state->output_scale = LORIE_WAYLAND_DEFAULT_SCALE;
    
    return state;
}

void lorie_wayland_shared_state_destroy(struct lorie_wayland_shared_state *state) {
    if (state) {
        pthread_mutex_destroy(&state->lock);
        pthread_mutex_destroy(&state->cursor.lock);
        pthread_cond_destroy(&state->cond);
        free(state);
    }
}

void lorie_wayland_shared_state_send(struct lorie_wayland_compositor *compositor, int fd) {
    if (compositor && compositor->shared_state && fd >= 0) {
        /* Send shared state via memfd/ashmem */
        /* TODO: Implement actual shared memory sharing */
        log(DEBUG, "Shared state send requested");
    }
}

/* Stub implementations for input handling */
void lorie_wayland_handle_touch(struct lorie_wayland_compositor *compositor,
                                 uint32_t id, uint32_t x, uint32_t y, uint32_t state) {
    /* TODO: Forward to wl_seat touch interface */
    log(DEBUG, "Touch: id=%d x=%d y=%d state=%d", id, x, y, state);
}

void lorie_wayland_handle_mouse(struct lorie_wayland_compositor *compositor,
                                 uint32_t x, uint32_t y, uint32_t button, bool down) {
    /* TODO: Forward to wl_seat pointer interface */
    log(DEBUG, "Mouse: x=%d y=%d button=%d down=%d", x, y, button, down);
}

void lorie_wayland_handle_key(struct lorie_wayland_compositor *compositor,
                               uint32_t keycode, bool down) {
    /* TODO: Forward to wl_seat keyboard interface */
    log(DEBUG, "Key: keycode=%d down=%d", keycode, down);
}

void lorie_wayland_handle_stylus(struct lorie_wayland_compositor *compositor,
                                  float x, float y, uint16_t pressure,
                                  int8_t tilt_x, int8_t tilt_y, int16_t orientation,
                                  uint8_t buttons, bool eraser) {
    /* TODO: Forward to wl_seat pointer/tablet interface */
    log(DEBUG, "Stylus: x=%f y=%f pressure=%d", x, y, pressure);
}

/* Renderer thread stub */
void *lorie_wayland_renderer_thread(void *data) {
    struct lorie_wayland_compositor *compositor = (struct lorie_wayland_compositor*) data;
    
    log(INFO, "Wayland renderer thread started");
    
    while (compositor->running) {
        /* TODO: Implement actual rendering loop */
        /* For now, just sleep and check for work */
        if (compositor->shared_state) {
            lorie_wayland_mutex_lock(&compositor->shared_state->lock,
                                    &compositor->shared_state->lockingPid);
            if (!compositor->shared_state->drawRequested) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 16 * 1000000; /* ~60fps */
                pthread_cond_timedwait(&compositor->shared_state->cond,
                                      &compositor->shared_state->lock, &ts);
            }
            compositor->shared_state->drawRequested = 0;
            lorie_wayland_mutex_unlock(&compositor->shared_state->lock,
                                      &compositor->shared_state->lockingPid);
        }
    }
    
    log(INFO, "Wayland renderer thread stopped");
    return NULL;
}

void lorie_wayland_renderer_init(struct lorie_wayland_compositor *compositor) {
    /* TODO: Initialize EGL/GLES2 renderer */
    log(INFO, "Renderer init stub");
}

void lorie_wayland_renderer_fini(struct lorie_wayland_compositor *compositor) {
    /* TODO: Cleanup EGL/GLES2 renderer */
    log(INFO, "Renderer fini stub");
}

void lorie_wayland_renderer_request_frame(struct lorie_wayland_compositor *compositor) {
    if (compositor && compositor->shared_state) {
        lorie_wayland_mutex_lock(&compositor->shared_state->lock,
                                &compositor->shared_state->lockingPid);
        compositor->shared_state->drawRequested = 1;
        pthread_cond_signal(&compositor->shared_state->cond);
        lorie_wayland_mutex_unlock(&compositor->shared_state->lock,
                                  &compositor->shared_state->lockingPid);
    }
}

/* XWayland stubs */
int lorie_wayland_xwayland_start(struct lorie_wayland_compositor *compositor) {
    log(INFO, "XWayland start requested (stub)");
    /* TODO: Implement XWayland startup */
    return 0;
}

void lorie_wayland_xwayland_stop(struct lorie_wayland_compositor *compositor) {
    log(INFO, "XWayland stop requested (stub)");
    /* TODO: Implement XWayland cleanup */
}

/* Required by linker - override weak abort/exit */
void abort(void) {
    _exit(134);
}

void exit(int code) {
    _exit(code);
}
