#ifndef LORIE_VIEWPORTER_H
#define LORIE_VIEWPORTER_H

#include <wayland-server.h>
#include "compositor.h"

struct lorie_compositor;

struct wl_global *lorie_viewporter_create(struct wl_display *display);
void lorie_viewporter_destroy(struct wl_global *global);

/* Test helpers — manipulate pending viewport state directly */
void lorie_viewport_set_source(struct lorie_surface *s,
                               double x, double y, double w, double h);
void lorie_viewport_set_destination(struct lorie_surface *s,
                                    int32_t w, int32_t h);
void lorie_viewport_clear(struct lorie_surface *s);
int lorie_viewport_validate_source(double x, double y, double w, double h);

#endif
