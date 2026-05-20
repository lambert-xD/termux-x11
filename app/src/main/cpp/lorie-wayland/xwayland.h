#ifndef LORIE_XWAYLAND_H
#define LORIE_XWAYLAND_H

#include <sys/types.h>

struct lorie_compositor;
struct lorie_xwayland;

struct lorie_xwayland *lorie_xwayland_init(struct lorie_compositor *c,
                                            const char *xserver_path);
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
    char *lockfile;
    int running;
};

#endif /* LORIE_XWAYLAND_H */
