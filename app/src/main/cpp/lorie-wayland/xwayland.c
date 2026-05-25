#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <android/log.h>

#include "xwayland.h"
#include "compositor.h"

#define LOG_TAG "LorieXWayland"
#define loge(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define MAX_DISPLAY 99

static int clear_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC);
}

static char *get_tmpdir(void) {
    const char *t = getenv("TMPDIR");
    if (t && access(t, W_OK) == 0) return strdup(t);
    t = getenv("PREFIX");
    if (t) {
        char *p = NULL;
        if (asprintf(&p, "%s/tmp", t) > 0 && access(p, W_OK) == 0) return p;
        free(p);
    }
    return strdup("/data/local/tmp");
}

static int create_lockfile(int display, const char *tmpdir, char **out) {
    char *path = NULL;
    if (asprintf(&path, "%s/.X%d-lock", tmpdir, display) < 0) return -1;

    int fd = open(path, O_WRONLY | O_CLOEXEC | O_CREAT | O_EXCL, 0444);
    if (fd < 0) { free(path); return -1; }

    char pid_str[32];
    int n = snprintf(pid_str, sizeof(pid_str), "%10d\n", (int)getpid());
    if (n != 11 || write(fd, pid_str, 11) != 11) {
        close(fd); unlink(path); free(path); return -1;
    }
    close(fd);
    *out = path;
    return 0;
}

static int bind_abstract_socket(int display, const char *tmpdir) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1,
             "%s/.X11-unix/X%d", tmpdir, display);
    /* Abstract socket: sun_path[0] = '\0' */
    socklen_t len = (socklen_t)(1 + strlen(addr.sun_path + 1) + offsetof(struct sockaddr_un, sun_path));

    if (bind(fd, (struct sockaddr *)&addr, len) < 0) {
        close(fd); return -1;
    }
    if (listen(fd, 1) < 0) {
        close(fd); return -1;
    }
    return fd;
}

static int bind_unix_socket(int display, const char *tmpdir) {
    char path[256];
    snprintf(path, sizeof(path), "%s/.X11-unix", tmpdir);
    mkdir(path, 0755);

    snprintf(path, sizeof(path), "%s/.X11-unix/X%d", tmpdir, display);
    unlink(path);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd); return -1;
    }
    if (listen(fd, 1) < 0) {
        close(fd); return -1;
    }
    return fd;
}

struct lorie_xwayland *lorie_xwayland_init(struct lorie_compositor *c,
                                            const char *xserver_path) {
    struct lorie_xwayland *xw = calloc(1, sizeof(*xw));
    if (!xw) return NULL;

    xw->compositor = c;
    xw->xserver_path = xserver_path ? strdup(xserver_path) : NULL;
    xw->pid = -1;
    xw->display_number = -1;
    xw->abstract_fd = -1;
    xw->unix_fd = -1;
    xw->wm_fd[0] = -1;
    xw->wm_fd[1] = -1;
    xw->sigchld_source = NULL;

    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, xw->wm_fd) < 0) {
        free(xw->xserver_path);
        free(xw);
        return NULL;
    }

    char *tmpdir = get_tmpdir();
    if (!tmpdir) {
        lorie_xwayland_shutdown(xw);
        return NULL;
    }
    for (int d = 0; d <= MAX_DISPLAY; d++) {
        char *lockfile = NULL;
        if (create_lockfile(d, tmpdir, &lockfile) < 0) continue;

        int afd = bind_abstract_socket(d, tmpdir);
        if (afd < 0) { unlink(lockfile); free(lockfile); continue; }

        int ufd = bind_unix_socket(d, tmpdir);
        if (ufd < 0) { close(afd); unlink(lockfile); free(lockfile); continue; }

        xw->display_number = d;
        xw->lockfile = lockfile;
        xw->abstract_fd = afd;
        xw->unix_fd = ufd;
        break;
    }
    free(tmpdir);

    if (xw->display_number < 0) {
        lorie_xwayland_shutdown(xw);
        return NULL;
    }
    return xw;
}

static int sigchld_handler(int sig, void *data) {
    (void)sig;
    struct lorie_xwayland *xw = data;
    if (!xw || xw->pid <= 0) return 1;

    int status;
    pid_t pid = waitpid(xw->pid, &status, WNOHANG);
    if (pid == xw->pid) {
        xw->pid = -1;
        xw->running = 0;
        if (xw->sigchld_source) {
            wl_event_source_remove(xw->sigchld_source);
            xw->sigchld_source = NULL;
        }
    }
    return 1;
}

static pid_t spawn_xwayland(struct lorie_xwayland *xw) {
    char display_str[16];
    char listen1_str[32], fd1_str[16];
    char listen2_str[32], fd2_str[16];
    char wm_str[16], wmfd_str[16];
    char *argv[16];
    int argc = 0;

    snprintf(display_str, sizeof(display_str), ":%d", xw->display_number);
    snprintf(listen1_str, sizeof(listen1_str), "-listenfd");
    snprintf(fd1_str, sizeof(fd1_str), "%d", xw->abstract_fd);
    snprintf(listen2_str, sizeof(listen2_str), "-listenfd");
    snprintf(fd2_str, sizeof(fd2_str), "%d", xw->unix_fd);
    snprintf(wm_str, sizeof(wm_str), "-wm");
    snprintf(wmfd_str, sizeof(wmfd_str), "%d", xw->wm_fd[1]);

    /* Must use mutable char* for execvp */
    char *bin = xw->xserver_path ? strdup(xw->xserver_path) : strdup("Xwayland");
    if (!bin) return -1;

    argv[argc++] = bin;
    argv[argc++] = display_str;
    argv[argc++] = "-rootless";
    argv[argc++] = "-core";
    argv[argc++] = listen1_str;
    argv[argc++] = fd1_str;
    argv[argc++] = listen2_str;
    argv[argc++] = fd2_str;
    argv[argc++] = wm_str;
    argv[argc++] = wmfd_str;
    argv[argc++] = "-terminate";
    argv[argc++] = "-nolisten";
    argv[argc++] = "tcp";
    argv[argc] = NULL;

    /* Register SIGCHLD handler BEFORE fork to avoid race. */
    struct wl_event_loop *loop = wl_display_get_event_loop(xw->compositor->display);
    if (!loop) {
        free(bin);
        return -1;
    }
    xw->sigchld_source = wl_event_loop_add_signal(loop, SIGCHLD, sigchld_handler, xw);
    if (!xw->sigchld_source) {
        free(bin);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        wl_event_source_remove(xw->sigchld_source);
        xw->sigchld_source = NULL;
        free(bin);
        return -1;
    }
    if (pid == 0) {
        close(xw->wm_fd[0]);
        if (clear_cloexec(xw->abstract_fd) < 0 ||
            clear_cloexec(xw->unix_fd) < 0 ||
            clear_cloexec(xw->wm_fd[1]) < 0) {
            _exit(127);
        }
        setenv("DISPLAY", display_str, 1);
        signal(SIGCHLD, SIG_DFL);
        execvp(bin, argv);
        _exit(127);
    }
    free(bin);
    close(xw->wm_fd[1]);
    xw->wm_fd[1] = -1;
    return pid;
}

int lorie_xwayland_launch(struct lorie_xwayland *xw) {
    if (!xw || xw->running) return -1;
    if (xw->display_number < 0) return -1;
    if (!xw->compositor || !xw->compositor->display) return -1;
    if (xw->abstract_fd < 0 || xw->unix_fd < 0 || xw->wm_fd[1] < 0) return -1;

    xw->pid = spawn_xwayland(xw);
    if (xw->pid < 0) return -1;
    xw->running = 1;
    return 0;
}

void lorie_xwayland_shutdown(struct lorie_xwayland *xw) {
    if (!xw) return;

    if (xw->running && xw->pid > 0) {
        kill(xw->pid, SIGTERM);
        /* Wait up to 2 seconds */
        for (int i = 0; i < 200 && xw->running; i++) usleep(10000);
        if (xw->running) {
            kill(xw->pid, SIGKILL);
            waitpid(xw->pid, NULL, 0);
        }
        xw->running = 0;
        xw->pid = -1;
    }

    if (xw->sigchld_source) {
        wl_event_source_remove(xw->sigchld_source);
        xw->sigchld_source = NULL;
    }

    if (xw->abstract_fd >= 0) { close(xw->abstract_fd); xw->abstract_fd = -1; }
    if (xw->unix_fd >= 0) { close(xw->unix_fd); xw->unix_fd = -1; }
    if (xw->wm_fd[0] >= 0) { close(xw->wm_fd[0]); xw->wm_fd[0] = -1; }
    if (xw->wm_fd[1] >= 0) { close(xw->wm_fd[1]); xw->wm_fd[1] = -1; }

    if (xw->lockfile) {
        unlink(xw->lockfile);
        free(xw->lockfile);
        xw->lockfile = NULL;
    }

    free(xw->xserver_path);
    free(xw);
}
