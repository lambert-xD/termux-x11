#ifndef LORIE_XWAYLAND_H
#define LORIE_XWAYLAND_H

#include <sys/types.h>

struct lorie_compositor;
struct lorie_xwayland;
struct wl_event_source;

struct lorie_xwayland *lorie_xwayland_init(struct lorie_compositor *c,
                                            const char *xserver_path);
int lorie_xwayland_launch(struct lorie_xwayland *xw);
void lorie_xwayland_shutdown(struct lorie_xwayland *xw);

/* Exposed for testing */
struct lorie_xwayland {
    struct lorie_compositor *compositor;
    char *xserver_path;
    int display_number;
    int abstract_fd;
    int unix_fd;
    int wm_fd[2];
    pid_t pid;
    struct wl_event_source *sigchld_source;
    char *lockfile;
    int running;
};

#endif /* LORIE_XWAYLAND_H */
