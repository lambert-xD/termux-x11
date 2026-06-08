#ifndef LORIE_COMPOSITOR_H
#define LORIE_COMPOSITOR_H

#include <wayland-util.h>
#include <wayland-server.h>
#include <wayland-server-core.h>
#include <android/native_window.h>
#include <pthread.h>
#include <stdatomic.h>
#include <pixman.h>

struct lorie_input;
struct lorie_output;
struct lorie_surface;
struct lorie_region;
struct lorie_clipboard;
struct lorie_xwayland_surface;
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

struct lorie_xdg_popup {
    struct wl_resource *resource;
    struct lorie_xdg_surface *xdg_surface;
    struct wl_resource *parent;
    int configured;
    int32_t x, y, width, height;
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
    struct wl_global *xwayland_shell_global;
    struct lorie_clipboard *clipboard;
    struct wl_list outputs;
    struct wl_list surfaces;
    struct wl_list clients;
    struct wl_list data_devices;
    pthread_mutex_t lock;
    ANativeWindow *native_window;
    atomic_int running;
    pthread_t event_loop_thread;
    struct lorie_input *input;
    struct lorie_renderer *renderer;
    char socket_name[64];
    int socket_fd;
    int client_fd_pipe[2];
    struct wl_event_source *client_fd_source;
};

struct lorie_compositor *lorie_compositor_create(void);
void lorie_compositor_destroy(struct lorie_compositor *c);
int lorie_compositor_start(struct lorie_compositor *c);
void lorie_compositor_stop(struct lorie_compositor *c);
void lorie_compositor_set_window(struct lorie_compositor *c, ANativeWindow *window);
void lorie_compositor_set_socket_name(struct lorie_compositor *c, const char *name);
void lorie_compositor_set_socket_fd(struct lorie_compositor *c, int fd);
int lorie_compositor_add_client_fd(struct lorie_compositor *c, int fd);

/* Protocol globals */
struct wl_global *lorie_xdg_shell_create(struct wl_display *display);
struct wl_global *lorie_linux_dmabuf_create(struct wl_display *display, struct lorie_compositor *compositor);
struct wl_global *lorie_data_device_manager_create(struct wl_display *display, struct lorie_compositor *c);
struct wl_global *lorie_viewporter_create(struct wl_display *display);
struct wl_global *lorie_xwayland_shell_create(struct wl_display *display);

/* Notify Wayland clients of Android clipboard changes */
void lorie_clipboard_send_android_selection(struct lorie_compositor *c);

/* Conditional global creation (called after renderer init) */
void lorie_compositor_create_dmabuf_global(struct lorie_compositor *c);

/* Output API */
struct lorie_output_resource {
    struct wl_list link;
    struct wl_resource *resource;
};

struct lorie_output {
    struct wl_list link;
    struct lorie_compositor *compositor;
    struct wl_global *global;
    struct wl_list bound_resources;
    int32_t width;
    int32_t height;
    int32_t scale;
    int32_t refresh;
    char name[32];
};

struct lorie_output *lorie_output_create(struct lorie_compositor *c,
                                          int32_t width, int32_t height, int32_t scale);
void lorie_output_destroy(struct lorie_output *output);
void lorie_output_update_size(struct lorie_output *output,
                               int32_t w, int32_t h, int32_t scale);

/* xwayland surface role */
struct lorie_xwayland_surface {
    struct wl_resource *resource;
    struct lorie_surface *surface;
    uint64_t serial;
};

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
    struct lorie_xwayland_surface *xwayland_surface;
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

/* Internal API — exposed for tests
 *
 * Thread-affinity contract (compositor-teardown-safety): lorie_surface_destroy_internal
 * (and the wl_client_destroy of a compositor-owned client reached through it)
 * mutates wl_client/wl_resource/wl_display state that the event-loop thread
 * concurrently dispatches and flushes while the compositor is running. Calling
 * it from any thread other than `c->event_loop_thread` while `c->running` is
 * true is a data race that corrupts heap state non-deterministically. The only
 * safe sequences are: (a) it runs ON the event-loop thread (real client
 * wl_surface.destroy dispatch), (b) the compositor has not started yet
 * (`c->running == false`, e.g. lorie_surface_create_internal's error path), or
 * (c) lorie_compositor_stop has already joined the event-loop thread (also
 * `running == false` afterwards — see lorie_test_safe_destroy_client). Calling
 * it from any other thread while running is the unsafe pattern this contract
 * forbids; lorie_compositor_assert_event_loop_thread is the code-enforced
 * checkpoint that converts a violation into a loud, deterministic abort
 * instead of silent corruption (test/host builds only — see
 * LORIE_TEARDOWN_GUARD). */
struct lorie_surface *lorie_surface_create_internal(struct lorie_compositor *c,
                                                     struct wl_client *client,
                                                     uint32_t id);
void lorie_surface_destroy_internal(struct lorie_surface *s);

/* Cross-thread teardown guard (compositor-teardown-safety spec).
 *
 * Aborts with a diagnostic iff: c is non-NULL AND the compositor is currently
 * running AND the calling thread is NOT the registered event-loop thread —
 * i.e. exactly the unsafe pattern described above. No-ops (and never reads
 * c->event_loop_thread) when c is NULL, when the compositor has not started /
 * has already been stopped (running == false — event_loop_thread may be
 * uninitialized before the first lorie_compositor_start), or when called from
 * the event-loop thread itself.
 *
 * Compiled to an empty inline no-op unless LORIE_TEARDOWN_GUARD is defined
 * (test/host CMake targets only — see tests/CMakeLists.txt). The production
 * NDK build never defines it, so this call site contributes nothing to the
 * shipped libXlorie.so: no new abort path, no branch, no symbol reference. */
#if defined(LORIE_TEARDOWN_GUARD)
void lorie_compositor_assert_event_loop_thread(struct lorie_compositor *c);
#else
static inline void lorie_compositor_assert_event_loop_thread(struct lorie_compositor *c) { (void)c; }
#endif

void lorie_surface_compute_logical_size(struct lorie_surface *s);
void surface_commit(struct wl_client *client, struct wl_resource *resource);

/* xdg-shell internal — exposed for tests */
void lorie_xdg_surface_handle_commit(struct lorie_surface *s, struct wl_client *client);
void lorie_xdg_surface_send_configure_internal(struct lorie_xdg_surface *xdg_surf, uint32_t serial);
void lorie_xdg_surface_ack_configure_internal(struct lorie_xdg_surface *xdg_surf, uint32_t serial);
void lorie_xdg_surface_send_initial_configure(struct lorie_xdg_surface *xdg_surf);
void xdg_toplevel_handle_resource_destroy(struct wl_resource *resource);
void xdg_popup_handle_resource_destroy(struct wl_resource *resource);

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
