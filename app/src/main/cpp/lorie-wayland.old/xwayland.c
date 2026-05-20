/*
 * XWayland Integration for Termux:X11 Wayland Compositor
 * 
 * Based on Weston XWayland implementation but adapted for Android/Termux.
 * 
 * Key design points:
 * - Creates X11 sockets (abstract + unix domain) on demand
 * - Supports lazy start (only launches XWayland when X11 client connects)
 * - Manages X11 windows as Wayland surfaces via XCB WM
 * - Uses Termux prefix paths for /tmp (e.g., /data/data/com.termux/files/usr/tmp)
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/stat.h>
#include <android/log.h>

#include "xwayland.h"

#define LOG_TAG "LorieXWayland"
#define logd(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define logi(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define logw(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define loge(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define XWAYLAND_LISTEN_BACKLOG 1
#define MAX_DISPLAY_NUMBER 99

/* Forward declarations */
static int lorie_xwayland_handle_event(int fd, uint32_t mask, void* data);
static void lorie_xwayland_cleanup_sockets(struct lorie_xwayland* xwayland);
static int lorie_xwayland_bind_sockets(struct lorie_xwayland* xwayland);
static int lorie_xwayland_try_display(struct lorie_xwayland* xwayland, int display);

struct lorie_xwayland {
    struct wl_display* display;
    struct lorie_xwayland_config config;
    
    /* X11 socket infrastructure */
    int display_number;
    char display_name[16];  /* e.g., ":0" */
    int abstract_fd;
    int unix_fd;
    
    /* Wayland event sources */
    struct wl_event_source* abstract_source;
    struct wl_event_source* unix_source;
    struct wl_event_loop* loop;
    
    /* Process management */
    pid_t pid;
    struct wl_event_source* sigchld_source;
    
    /* State */
    bool running;
    bool lazy;
    char* lockfile;
    char* tmpdir;
    
    /* Window manager */
    struct lorie_xwayland_wm* wm;
    int wm_fd[2];  /* socket pair for WM communication */
    
    /* Cleanup */
    struct wl_listener display_destroy_listener;
};

struct lorie_xwayland_wm {
    struct lorie_xwayland* xwayland;
    int fd;
    struct wl_event_source* source;
    
    /* XCB connection for WM */
    // xcb_connection_t* xcb_conn;  /* TODO: add xcb dependency */
    // xcb_screen_t* screen;
    // xcb_window_t wm_window;
};

/*
 * Get Termux tmp directory.
 * On Android/Termux, /tmp may not exist or be writable.
 * Use $PREFIX/tmp or $TMPDIR or fallback to /data/local/tmp.
 */
static char* get_tmp_dir(void) {
    const char* tmp = getenv("TMPDIR");
    if (tmp && access(tmp, W_OK) == 0) {
        return strdup(tmp);
    }
    
    tmp = getenv("PREFIX");
    if (tmp) {
        char* prefix_tmp = NULL;
        asprintf(&prefix_tmp, "%s/tmp", tmp);
        if (prefix_tmp && access(prefix_tmp, W_OK) == 0) {
            return prefix_tmp;
        }
        free(prefix_tmp);
    }
    
    if (access("/tmp", W_OK) == 0) {
        return strdup("/tmp");
    }
    
    /* Last resort for Android */
    return strdup("/data/local/tmp");
}

/*
 * Create X11 lock file.
 * Returns: 0 on success, -1 on failure (sets errno).
 * On EEXIST, caller should try next display number.
 * On EAGAIN, caller should retry same display number.
 */
static int create_lockfile(int display, const char* tmpdir, char** lockfile_out) {
    char* lockfile = NULL;
    int fd, size;
    pid_t other;
    char pid_str[16];
    
    asprintf(&lockfile, "%s/.X%d-lock", tmpdir, display);
    if (!lockfile) {
        errno = ENOMEM;
        return -1;
    }
    
    fd = open(lockfile, O_WRONLY | O_CLOEXEC | O_CREAT | O_EXCL, 0444);
    if (fd < 0 && errno == EEXIST) {
        /* Check if the lock is stale */
        fd = open(lockfile, O_CLOEXEC | O_RDONLY);
        if (fd < 0) {
            loge("Can't read lock file %s: %s", lockfile, strerror(errno));
            free(lockfile);
            errno = EEXIST;
            return -1;
        }
        
        ssize_t n = read(fd, pid_str, sizeof(pid_str) - 1);
        close(fd);
        
        if (n <= 0) {
            loge("Can't read lock file %s", lockfile);
            free(lockfile);
            errno = EEXIST;
            return -1;
        }
        pid_str[n] = '\0';
        
        /* Parse PID */
        char* endptr;
        long parsed = strtol(pid_str, &endptr, 10);
        if (endptr == pid_str || *endptr != '\0') {
            loge("Can't parse lock file %s", lockfile);
            free(lockfile);
            errno = EEXIST;
            return -1;
        }
        other = (pid_t)parsed;
        
        /* Check if process still exists */
        if (kill(other, 0) < 0 && errno == ESRCH) {
            logw("Unlinking stale lock file %s", lockfile);
            if (unlink(lockfile) == 0) {
                free(lockfile);
                errno = EAGAIN;
                return -1;
            }
            free(lockfile);
            errno = EEXIST;
            return -1;
        }
        
        free(lockfile);
        errno = EEXIST;
        return -1;
    } else if (fd < 0) {
        loge("Failed to create lock file %s: %s", lockfile, strerror(errno));
        free(lockfile);
        return -1;
    }
    
    /* Write our PID */
    size = dprintf(fd, "%10d\n", getpid());
    close(fd);
    
    if (size != 11) {
        unlink(lockfile);
        free(lockfile);
        return -1;
    }
    
    *lockfile_out = lockfile;
    return 0;
}

/*
 * Bind to abstract Unix socket for X11.
 * Format: @/tmp/.X11-unix/X<display>
 */
static int bind_to_abstract_socket(int display, const char* tmpdir) {
    struct sockaddr_un addr;
    socklen_t size;
    size_t name_size;
    int fd;
    
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        loge("socket() failed: %s", strerror(errno));
        return -1;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    
    /* Abstract socket: leading null byte */
    name_size = snprintf(addr.sun_path, sizeof(addr.sun_path),
                         "%c%s/.X11-unix/X%d", 0, tmpdir, display);
    size = offsetof(struct sockaddr_un, sun_path) + name_size;
    
    if (bind(fd, (struct sockaddr*)&addr, size) < 0) {
        if (errno != EADDRINUSE) {
            loge("Failed to bind abstract socket: %s", strerror(errno));
        }
        close(fd);
        return -1;
    }
    
    if (listen(fd, XWAYLAND_LISTEN_BACKLOG) < 0) {
        loge("listen() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }
    
    return fd;
}

/*
 * Bind to Unix domain socket for X11.
 * Format: /tmp/.X11-unix/X<display>
 */
static int bind_to_unix_socket(int display, const char* tmpdir) {
    struct sockaddr_un addr;
    socklen_t size;
    size_t name_size;
    int fd;
    
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        loge("socket() failed: %s", strerror(errno));
        return -1;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    
    name_size = snprintf(addr.sun_path, sizeof(addr.sun_path),
                         "%s/.X11-unix/X%d", tmpdir, display) + 1;
    size = offsetof(struct sockaddr_un, sun_path) + name_size;
    
    /* Ensure X11-unix directory exists */
    char* dir = NULL;
    asprintf(&dir, "%s/.X11-unix", tmpdir);
    if (dir) {
        mkdir(dir, 0777);
        free(dir);
    }
    
    unlink(addr.sun_path);
    
    if (bind(fd, (struct sockaddr*)&addr, size) < 0) {
        loge("Failed to bind unix socket %s: %s", addr.sun_path, strerror(errno));
        close(fd);
        return -1;
    }
    
    if (listen(fd, XWAYLAND_LISTEN_BACKLOG) < 0) {
        loge("listen() failed: %s", strerror(errno));
        unlink(addr.sun_path);
        close(fd);
        return -1;
    }
    
    return fd;
}

/*
 * Try to set up a specific display number.
 * Returns: 0 on success, -1 on failure.
 */
static int lorie_xwayland_try_display(struct lorie_xwayland* xwayland, int display) {
    int ret;
    
    ret = create_lockfile(display, xwayland->tmpdir, &xwayland->lockfile);
    if (ret < 0) {
        return -1;
    }
    
    xwayland->abstract_fd = bind_to_abstract_socket(display, xwayland->tmpdir);
    if (xwayland->abstract_fd < 0) {
        if (errno != EADDRINUSE) {
            /* Real error, not just in use */
            unlink(xwayland->lockfile);
            free(xwayland->lockfile);
            xwayland->lockfile = NULL;
            return -1;
        }
        /* Abstract socket in use, try next display */
        unlink(xwayland->lockfile);
        free(xwayland->lockfile);
        xwayland->lockfile = NULL;
        errno = EEXIST;
        return -1;
    }
    
    xwayland->unix_fd = bind_to_unix_socket(display, xwayland->tmpdir);
    if (xwayland->unix_fd < 0) {
        close(xwayland->abstract_fd);
        xwayland->abstract_fd = -1;
        unlink(xwayland->lockfile);
        free(xwayland->lockfile);
        xwayland->lockfile = NULL;
        return -1;
    }
    
    xwayland->display_number = display;
    snprintf(xwayland->display_name, sizeof(xwayland->display_name), ":%d", display);
    
    return 0;
}

/*
 * Bind to available X11 sockets.
 * Auto-increment display number if needed.
 */
static int lorie_xwayland_bind_sockets(struct lorie_xwayland* xwayland) {
    int display = xwayland->config.display;
    
    if (display < 0) {
        display = 0;
    }
    
    for (int i = 0; i < MAX_DISPLAY_NUMBER; i++) {
        int try_display = display + i;
        if (try_display > MAX_DISPLAY_NUMBER) {
            try_display = i;  /* Wrap around */
        }
        
        if (lorie_xwayland_try_display(xwayland, try_display) == 0) {
            logi("X11 listening on display %s", xwayland->display_name);
            return 0;
        }
        
        if (errno == EEXIST) {
            /* Display in use, try next */
            continue;
        }
        
        /* Real error */
        return -1;
    }
    
    loge("Could not find available X11 display number");
    return -1;
}

/*
 * Clean up X11 sockets and lock file.
 */
static void lorie_xwayland_cleanup_sockets(struct lorie_xwayland* xwayland) {
    char path[256];
    
    if (xwayland->lockfile) {
        unlink(xwayland->lockfile);
        free(xwayland->lockfile);
        xwayland->lockfile = NULL;
    }
    
    if (xwayland->display_number >= 0) {
        snprintf(path, sizeof(path), "%s/.X11-unix/X%d",
                 xwayland->tmpdir, xwayland->display_number);
        unlink(path);
    }
    
    if (xwayland->abstract_fd >= 0) {
        close(xwayland->abstract_fd);
        xwayland->abstract_fd = -1;
    }
    
    if (xwayland->unix_fd >= 0) {
        close(xwayland->unix_fd);
        xwayland->unix_fd = -1;
    }
    
    xwayland->display_number = -1;
    xwayland->display_name[0] = '\0';
}

/*
 * Handle incoming X11 client connection.
 * This triggers XWayland launch in lazy mode.
 */
static int lorie_xwayland_handle_event(int fd, uint32_t mask, void* data) {
    struct lorie_xwayland* xwayland = data;
    (void)fd;
    (void)mask;
    
    logi("X11 client connection received on display %s", xwayland->display_name);
    
    if (!xwayland->running) {
        logi("Launching XWayland (lazy mode)");
        if (lorie_xwayland_launch(xwayland) < 0) {
            loge("Failed to launch XWayland");
            return 1;
        }
    }
    
    /* Remove the listen sources - XWayland now owns the sockets */
    if (xwayland->abstract_source) {
        wl_event_source_remove(xwayland->abstract_source);
        xwayland->abstract_source = NULL;
    }
    if (xwayland->unix_source) {
        wl_event_source_remove(xwayland->unix_source);
        xwayland->unix_source = NULL;
    }
    
    return 1;
}

/*
 * Handle SIGCHLD from XWayland process.
 */
static int lorie_xwayland_sigchld_handler(int signal_number, void* data) {
    struct lorie_xwayland* xwayland = data;
    int status;
    pid_t pid;
    
    (void)signal_number;
    
    pid = waitpid(xwayland->pid, &status, WNOHANG);
    if (pid != xwayland->pid) {
        return 1;
    }
    
    if (WIFEXITED(status)) {
        logi("XWayland exited with status %d", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        logw("XWayland killed by signal %d", WTERMSIG(status));
    }
    
    xwayland->pid = -1;
    xwayland->running = false;
    
    /* Clean up WM */
    if (xwayland->wm) {
        lorie_xwayland_wm_destroy(xwayland->wm);
        xwayland->wm = NULL;
    }
    
    /* If XWayland crashed before WM was established, don't restart */
    if (!xwayland->wm && xwayland->lazy) {
        logw("XWayland crashed too fast, not restarting");
        lorie_xwayland_cleanup_sockets(xwayland);
    } else if (xwayland->lazy) {
        logi("XWayland exited, will restart on demand");
        /* Re-add socket listeners for lazy restart */
        xwayland->abstract_source =
            wl_event_loop_add_fd(xwayland->loop, xwayland->abstract_fd,
                                 WL_EVENT_READABLE,
                                 lorie_xwayland_handle_event, xwayland);
        xwayland->unix_source =
            wl_event_loop_add_fd(xwayland->loop, xwayland->unix_fd,
                                 WL_EVENT_READABLE,
                                 lorie_xwayland_handle_event, xwayland);
    }
    
    return 1;
}

/*
 * Create WM communication socket pair.
 * XWayland connects back to us via this socket.
 */
static int lorie_xwayland_create_wm_socket(struct lorie_xwayland* xwayland) {
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, xwayland->wm_fd) < 0) {
        loge("socketpair() failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

/*
 * Spawn the XWayland process.
 */
static pid_t lorie_xwayland_spawn(struct lorie_xwayland* xwayland) {
    pid_t pid;
    const char* xwayland_binary = xwayland->config.xwayland_path;
    const char* xorg_binary = xwayland->config.xorg_path;
    
    /* Find XWayland binary */
    if (!xwayland_binary) {
        xwayland_binary = "Xwayland";
    }
    
    if (access(xwayland_binary, X_OK) != 0) {
        /* Try with PREFIX */
        const char* prefix = getenv("PREFIX");
        if (prefix) {
            char* path = NULL;
            asprintf(&path, "%s/bin/Xwayland", prefix);
            if (path && access(path, X_OK) == 0) {
                xwayland_binary = path;
            } else {
                free(path);
            }
        }
    }
    
    /* Fallback to Xorg if XWayland not found */
    if (access(xwayland_binary, X_OK) != 0) {
        logw("XWayland binary not found, trying Xorg");
        if (xorg_binary && access(xorg_binary, X_OK) == 0) {
            xwayland_binary = xorg_binary;
        } else {
            xorg_binary = "Xorg";
            if (access(xorg_binary, X_OK) == 0) {
                xwayland_binary = xorg_binary;
            } else {
                loge("No XWayland or Xorg binary found");
                return -1;
            }
        }
    }
    
    /* Create WM socket */
    if (lorie_xwayland_create_wm_socket(xwayland) < 0) {
        return -1;
    }
    
    /* Prepare arguments */
    char* argv[32];
    int argc = 0;
    char display_str[16];
    char listen_str[32];
    char wm_fd_str[16];
    
    snprintf(display_str, sizeof(display_str), ":%d", xwayland->display_number);
    
    argv[argc++] = (char*)xwayland_binary;
    argv[argc++] = display_str;
    argv[argc++] = "-rootless";
    argv[argc++] = "-core";
    
    /* Pass socket fds */
    snprintf(listen_str, sizeof(listen_str), "-listenfd");
    argv[argc++] = listen_str;
    snprintf(listen_str, sizeof(listen_str), "%d", xwayland->abstract_fd);
    argv[argc++] = listen_str;
    
    snprintf(listen_str, sizeof(listen_str), "-listenfd");
    argv[argc++] = listen_str;
    snprintf(listen_str, sizeof(listen_str), "%d", xwayland->unix_fd);
    argv[argc++] = listen_str;
    
    /* Pass WM fd */
    snprintf(wm_fd_str, sizeof(wm_fd_str), "-wm");
    argv[argc++] = wm_fd_str;
    snprintf(wm_fd_str, sizeof(wm_fd_str), "%d", xwayland->wm_fd[1]);
    argv[argc++] = wm_fd_str;
    
    /* Termination on display disconnect */
    argv[argc++] = "-terminate";
    
    /* Android-specific: no DRM/KMS */
    argv[argc++] = "-nolisten";
    argv[argc++] = "tcp";
    
    argv[argc] = NULL;
    
    pid = fork();
    if (pid < 0) {
        loge("fork() failed: %s", strerror(errno));
        close(xwayland->wm_fd[0]);
        close(xwayland->wm_fd[1]);
        return -1;
    }
    
    if (pid == 0) {
        /* Child process: XWayland */
        
        /* Close parent's end of WM socket */
        close(xwayland->wm_fd[0]);
        
        /* Set DISPLAY */
        setenv("DISPLAY", display_str, 1);
        
        /* Reset signal handlers */
        signal(SIGCHLD, SIG_DFL);
        
        /* Exec XWayland */
        execvp(xwayland_binary, argv);
        
        /* If we get here, exec failed */
        loge("execvp(%s) failed: %s", xwayland_binary, strerror(errno));
        _exit(127);
    }
    
    /* Parent */
    close(xwayland->wm_fd[1]);
    logi("XWayland started as PID %d", pid);
    
    return pid;
}

/*
 * Initialize XWayland support.
 */
struct lorie_xwayland* lorie_xwayland_init(struct wl_display* display,
                                            const struct lorie_xwayland_config* config) {
    struct lorie_xwayland* xwayland;
    
    if (!display || !config) {
        return NULL;
    }
    
    xwayland = calloc(1, sizeof(*xwayland));
    if (!xwayland) {
        return NULL;
    }
    
    xwayland->display = display;
    xwayland->config = *config;
    xwayland->pid = -1;
    xwayland->display_number = -1;
    xwayland->abstract_fd = -1;
    xwayland->unix_fd = -1;
    xwayland->wm_fd[0] = -1;
    xwayland->wm_fd[1] = -1;
    xwayland->lazy = config->lazy;
    
    /* Set up tmp directory */
    if (config->termux_prefix) {
        xwayland->tmpdir = strdup(config->termux_prefix);
    } else {
        xwayland->tmpdir = get_tmp_dir();
    }
    
    /* Bind X11 sockets */
    if (lorie_xwayland_bind_sockets(xwayland) < 0) {
        loge("Failed to bind X11 sockets");
        free(xwayland->tmpdir);
        free(xwayland);
        return NULL;
    }
    
    /* Set up Wayland event loop */
    xwayland->loop = wl_display_get_event_loop(display);
    
    /* Add socket listeners for lazy mode */
    if (xwayland->lazy) {
        xwayland->abstract_source =
            wl_event_loop_add_fd(xwayland->loop, xwayland->abstract_fd,
                                 WL_EVENT_READABLE,
                                 lorie_xwayland_handle_event, xwayland);
        xwayland->unix_source =
            wl_event_loop_add_fd(xwayland->loop, xwayland->unix_fd,
                                 WL_EVENT_READABLE,
                                 lorie_xwayland_handle_event, xwayland);
    }
    
    /* Set DISPLAY in environment */
    setenv("DISPLAY", xwayland->display_name, 1);
    
    logi("XWayland initialized on display %s (lazy=%s)",
         xwayland->display_name, xwayland->lazy ? "true" : "false");
    
    /* If not lazy, launch immediately */
    if (!xwayland->lazy) {
        if (lorie_xwayland_launch(xwayland) < 0) {
            loge("Failed to launch XWayland immediately");
            lorie_xwayland_cleanup_sockets(xwayland);
            free(xwayland->tmpdir);
            free(xwayland);
            return NULL;
        }
    }
    
    return xwayland;
}

/*
 * Launch the XWayland process.
 */
int lorie_xwayland_launch(struct lorie_xwayland* xwayland) {
    if (!xwayland || xwayland->running) {
        return 0;
    }
    
    if (xwayland->display_number < 0) {
        loge("No display number set");
        return -1;
    }
    
    xwayland->pid = lorie_xwayland_spawn(xwayland);
    if (xwayland->pid < 0) {
        return -1;
    }
    
    xwayland->running = true;
    
    /* Set up SIGCHLD handler */
    xwayland->sigchld_source =
        wl_event_loop_add_signal(xwayland->loop, SIGCHLD,
                                 lorie_xwayland_sigchld_handler, xwayland);
    
    /* Create WM */
    // xwayland->wm = lorie_xwayland_wm_create(xwayland, xwayland->wm_fd[0]);
    
    return 0;
}

/*
 * Shutdown XWayland.
 */
void lorie_xwayland_shutdown(struct lorie_xwayland* xwayland) {
    if (!xwayland) {
        return;
    }
    
    if (xwayland->running && xwayland->pid > 0) {
        logi("Shutting down XWayland (PID %d)", xwayland->pid);
        kill(xwayland->pid, SIGTERM);
        
        /* Wait up to 2 seconds for graceful shutdown */
        int waited = 0;
        while (xwayland->running && waited < 200) {
            usleep(10000);  /* 10ms */
            waited++;
        }
        
        if (xwayland->running) {
            logw("XWayland did not terminate gracefully, sending SIGKILL");
            kill(xwayland->pid, SIGKILL);
            waitpid(xwayland->pid, NULL, 0);
            xwayland->running = false;
            xwayland->pid = -1;
        }
    }
    
    /* Clean up WM */
    if (xwayland->wm) {
        lorie_xwayland_wm_destroy(xwayland->wm);
        xwayland->wm = NULL;
    }
    
    /* Remove event sources */
    if (xwayland->sigchld_source) {
        wl_event_source_remove(xwayland->sigchld_source);
        xwayland->sigchld_source = NULL;
    }
    
    lorie_xwayland_cleanup_sockets(xwayland);
}

/*
 * Destroy XWayland resources.
 */
void lorie_xwayland_destroy(struct lorie_xwayland* xwayland) {
    if (!xwayland) {
        return;
    }
    
    lorie_xwayland_shutdown(xwayland);
    
    free(xwayland->tmpdir);
    free(xwayland);
}

/*
 * Check if XWayland is running.
 */
bool lorie_xwayland_is_running(struct lorie_xwayland* xwayland) {
    return xwayland && xwayland->running;
}

/*
 * Get the DISPLAY string.
 */
const char* lorie_xwayland_get_display(struct lorie_xwayland* xwayland) {
    if (!xwayland || xwayland->display_number < 0) {
        return NULL;
    }
    return xwayland->display_name;
}

/*
 * Set the environment DISPLAY variable.
 */
int lorie_xwayland_set_env_display(struct lorie_xwayland* xwayland) {
    const char* display = lorie_xwayland_get_display(xwayland);
    if (!display) {
        return -1;
    }
    return setenv("DISPLAY", display, 1);
}

/*
 * Get the XWayland process PID.
 */
pid_t lorie_xwayland_get_pid(struct lorie_xwayland* xwayland) {
    if (!xwayland) {
        return -1;
    }
    return xwayland->pid;
}

/*
 * Window Manager stub implementation.
 * TODO: Full XCB-based WM implementation.
 */
struct lorie_xwayland_wm* lorie_xwayland_wm_create(struct lorie_xwayland* xwayland, int wm_fd) {
    struct lorie_xwayland_wm* wm = calloc(1, sizeof(*wm));
    if (!wm) {
        return NULL;
    }
    
    wm->xwayland = xwayland;
    wm->fd = wm_fd;
    
    logi("XWayland WM created (fd=%d)", wm_fd);
    
    /* TODO: Initialize XCB connection and set up WM */
    
    return wm;
}

void lorie_xwayland_wm_destroy(struct lorie_xwayland_wm* wm) {
    if (!wm) {
        return;
    }
    
    if (wm->fd >= 0) {
        close(wm->fd);
    }
    
    /* TODO: Clean up XCB resources */
    
    free(wm);
}
