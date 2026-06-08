#ifndef LORIE_XWAYLAND_SHELL_H
#define LORIE_XWAYLAND_SHELL_H

#include <wayland-server.h>

struct wl_global *lorie_xwayland_shell_create(struct wl_display *display);

#endif /* LORIE_XWAYLAND_SHELL_H */
