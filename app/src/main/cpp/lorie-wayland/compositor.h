#ifndef LORIE_COMPOSITOR_H
#define LORIE_COMPOSITOR_H

#include <wayland-util.h>
#include <wayland-server.h>
#include <wayland-server-core.h>
#include <android/native_window.h>
#include <pthread.h>
#include <pixman.h>

struct lorie_input;
struct lorie_output;
struct lorie_surface;
struct lorie_region;

struct lorie_compositor {
    struct wl_display *display;
    struct wl_event_loop *event_loop;
    struct wl_global *compositor_global;
    struct wl_global *subcompositor_global;
    struct wl_global *shm_global;
    struct wl_global *output_global;
    struct wl_global *xdg_shell_global;
    struct wl_global *linux_dmabuf_global;
    struct wl_global *data_device_manager_global;
    struct wl_list outputs;
    struct wl_list surfaces;
    struct wl_list clients;
    pthread_mutex_t lock;
    ANativeWindow *native_window;
    int running;
    pthread_t event_loop_thread;
    struct lorie_input *input;
};

struct lorie_compositor *lorie_compositor_create(void);
void lorie_compositor_destroy(struct lorie_compositor *c);
int lorie_compositor_start(struct lorie_compositor *c);
void lorie_compositor_stop(struct lorie_compositor *c);
void lorie_compositor_set_window(struct lorie_compositor *c, ANativeWindow *window);

/* Protocol globals */
struct wl_global *lorie_xdg_shell_create(struct wl_display *display);
struct wl_global *lorie_linux_dmabuf_create(struct wl_display *display);
struct wl_global *lorie_data_device_manager_create(struct wl_display *display);

/* Output API */
struct lorie_output {
    struct wl_list link;
    struct lorie_compositor *compositor;
    struct wl_global *global;
    int32_t width;
    int32_t height;
    int32_t scale;
    int32_t refresh;
    char name[32];
};

struct lorie_output *lorie_output_create(struct lorie_compositor *c,
                                          int32_t width, int32_t height, int32_t scale);
void lorie_output_destroy(struct lorie_output *output);

/* Surface types (defined in surface.c) */
struct lorie_frame_callback {
    struct wl_list link;
    struct wl_resource *resource;
};

struct lorie_surface {
    struct wl_list link;
    struct wl_resource *resource;
    struct lorie_compositor *compositor;
    struct wl_resource *buffer_resource;
    struct wl_resource *pending_buffer;
    int pending_attached;
    int32_t pending_x, pending_y;
    int32_t x, y, width, height;
    int32_t buffer_scale;
    int32_t buffer_transform;
    pixman_region32_t damage;
    struct wl_list frame_callbacks;
    struct lorie_surface *parent;
    struct wl_list subsurface_link;
    struct wl_list subsurfaces;
    void *buffer; /* LorieBuffer* — imported from wl_shm_buffer */
};

struct lorie_region {
    struct wl_resource *resource;
    pixman_region32_t region;
};

/* Internal API — exposed for tests */
struct lorie_surface *lorie_surface_create_internal(struct lorie_compositor *c,
                                                     struct wl_client *client,
                                                     uint32_t id);
void lorie_surface_destroy_internal(struct lorie_surface *s);

/* Callbacks implemented in surface.c */
void compositor_create_surface(struct wl_client *client,
                               struct wl_resource *resource, uint32_t id);
void compositor_create_region(struct wl_client *client,
                              struct wl_resource *resource, uint32_t id);
void subcompositor_get_subsurface(struct wl_client *client,
                                  struct wl_resource *resource, uint32_t id,
                                  struct wl_resource *surface,
                                  struct wl_resource *parent);

#endif /* LORIE_COMPOSITOR_H */
