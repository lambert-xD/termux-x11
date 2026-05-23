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
struct lorie_clipboard;
struct lorie_xdg_surface {
    struct wl_resource *resource;
    struct lorie_surface *surface;
    struct wl_resource *role; /* toplevel or popup */
    uint32_t pending_configure_serial;
    int configured;
};

struct lorie_xdg_toplevel {
    struct wl_resource *resource;
    struct lorie_xdg_surface *xdg_surface;
    char *title;
    char *app_id;
};

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
    struct wl_global *viewporter_global;
    struct lorie_clipboard *clipboard;
    struct wl_list outputs;
    struct wl_list surfaces;
    struct wl_list clients;
    struct wl_list data_devices;
    pthread_mutex_t lock;
    ANativeWindow *native_window;
    int running;
    pthread_t event_loop_thread;
    struct lorie_input *input;
    struct lorie_renderer *renderer;
    char socket_name[64];
};

struct lorie_compositor *lorie_compositor_create(void);
void lorie_compositor_destroy(struct lorie_compositor *c);
int lorie_compositor_start(struct lorie_compositor *c);
void lorie_compositor_stop(struct lorie_compositor *c);
void lorie_compositor_set_window(struct lorie_compositor *c, ANativeWindow *window);
void lorie_compositor_set_socket_name(struct lorie_compositor *c, const char *name);

/* Protocol globals */
struct wl_global *lorie_xdg_shell_create(struct wl_display *display);
struct wl_global *lorie_linux_dmabuf_create(struct wl_display *display, struct lorie_compositor *compositor);
struct wl_global *lorie_data_device_manager_create(struct wl_display *display, struct lorie_compositor *c);
struct wl_global *lorie_viewporter_create(struct wl_display *display);

/* Notify Wayland clients of Android clipboard changes */
void lorie_clipboard_send_android_selection(struct lorie_compositor *c);

/* Conditional global creation (called after renderer init) */
void lorie_compositor_create_dmabuf_global(struct lorie_compositor *c);

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
    int32_t logical_width, logical_height;
    int32_t buffer_scale;
    int32_t buffer_transform;
    pixman_region32_t damage;
    struct wl_list frame_callbacks;
    struct lorie_surface *parent;
    struct wl_list subsurface_link;
    struct wl_list subsurfaces;
    void *buffer; /* LorieBuffer* — imported from wl_shm_buffer */
    struct wl_resource *viewport_resource;
    struct lorie_xdg_surface *xdg_surface;
    struct {
        double src_x, src_y, src_w, src_h;
        int has_src;
        int32_t dst_w, dst_h;
        int has_dst;
    } viewport, pending_viewport;
};

struct lorie_region {
    struct wl_resource *resource;
    pixman_region32_t region;
};

/* SHM pool (exposed for tests) */
struct lorie_shm_pool {
    void *data;
    int32_t size;
    int refcount;
    int pending_destroy;
};

struct lorie_shm_pool *lorie_shm_pool_create(int fd, int32_t size);
void lorie_shm_pool_destroy(struct lorie_shm_pool *pool);

/* SHM buffer (new for PR 1) */
struct lorie_shm_buffer {
    struct wl_resource *resource;
    struct lorie_shm_pool *pool;
    int32_t offset;
    int32_t width;
    int32_t height;
    int32_t stride;
    uint32_t format;
    void *data;
};

struct lorie_shm_buffer *lorie_shm_buffer_from_resource(struct wl_resource *resource);

/* Internal — exposed for tests */
struct wl_resource *lorie_shm_pool_create_buffer_internal(struct wl_client *client,
    struct wl_resource *pool_resource, uint32_t id, int32_t offset, int32_t width,
    int32_t height, int32_t stride, uint32_t format);
void shm_pool_handle_resource_destroy(struct wl_resource *resource);

enum lorie_clipboard_source {
    CLIPBOARD_SOURCE_NONE = 0,
    CLIPBOARD_SOURCE_ANDROID,
    CLIPBOARD_SOURCE_WAYLAND,
};

/* Clipboard API — exposed for tests */
struct lorie_clipboard *lorie_clipboard_create(struct lorie_compositor *c);
void lorie_clipboard_destroy(struct lorie_clipboard *cb);
int lorie_clipboard_read_pipe(int read_fd, char **out_text, size_t *out_len);
void lorie_clipboard_set_selection(struct lorie_clipboard *cb, struct wl_resource *source_resource);
void lorie_clipboard_clear_source(struct lorie_clipboard *cb, struct wl_resource *source_resource);
void lorie_clipboard_set_text_callback(struct lorie_clipboard *cb,
    void (*cb_fn)(const char *text, size_t len, void *user_data), void *user_data);
int lorie_clipboard_mime_type_supported(const char *mime_type);

/* Android → Wayland clipboard */
void lorie_clipboard_send_android_text(struct lorie_clipboard *cb, const char *text, size_t len);
const char *lorie_clipboard_get_android_text(struct lorie_clipboard *cb, size_t *out_len);
enum lorie_clipboard_source lorie_clipboard_get_last_source(struct lorie_clipboard *cb);
uint64_t lorie_clipboard_get_timestamp(struct lorie_clipboard *cb);
void lorie_clipboard_set_last_source(struct lorie_clipboard *cb, enum lorie_clipboard_source src);
void lorie_clipboard_set_timestamp(struct lorie_clipboard *cb, uint64_t ts);

/* Internal API — exposed for tests */
struct lorie_surface *lorie_surface_create_internal(struct lorie_compositor *c,
                                                     struct wl_client *client,
                                                     uint32_t id);
void lorie_surface_destroy_internal(struct lorie_surface *s);
void lorie_surface_compute_logical_size(struct lorie_surface *s);
void surface_commit(struct wl_client *client, struct wl_resource *resource);

/* xdg-shell internal — exposed for tests */
void lorie_xdg_surface_handle_commit(struct lorie_surface *s, struct wl_client *client);
void lorie_xdg_surface_send_configure_internal(struct lorie_xdg_surface *xdg_surf, uint32_t serial);
void lorie_xdg_surface_ack_configure_internal(struct lorie_xdg_surface *xdg_surf, uint32_t serial);
void xdg_toplevel_handle_resource_destroy(struct wl_resource *resource);

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
