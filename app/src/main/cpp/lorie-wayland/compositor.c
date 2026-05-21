#include "compositor.h"
#include "input.h"

#include <stdlib.h>
#include <string.h>
#include <android/log.h>
#include <sys/mman.h>
#include <unistd.h>

#define LOG_TAG "LorieCompositor"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* Forward declarations for internal callbacks */
static void compositor_bind(struct wl_client *client, void *data,
                            uint32_t version, uint32_t id);
static void subcompositor_bind(struct wl_client *client, void *data,
                               uint32_t version, uint32_t id);
static void shm_bind(struct wl_client *client, void *data,
                     uint32_t version, uint32_t id);
static void output_bind(struct wl_client *client, void *data,
                        uint32_t version, uint32_t id) {
    struct lorie_output *output = data;
    struct wl_resource *resource =
        wl_resource_create(client, &wl_output_interface, (int)version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, NULL, output, NULL);
    wl_output_send_geometry(resource, 0, 0, output->width, output->height, 0,
                            "Unknown", "Unknown", 0);
    wl_output_send_mode(resource, 0, output->width, output->height, output->refresh);
    wl_output_send_scale(resource, output->scale);
    wl_output_send_done(resource);
}

/* --- SHM pool helpers (exposed for tests) --- */
struct lorie_shm_pool *lorie_shm_pool_create(int fd, int32_t size) {
    void *data = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        return NULL;
    }
    struct lorie_shm_pool *pool = calloc(1, sizeof(*pool));
    if (!pool) {
        munmap(data, size);
        return NULL;
    }
    pool->data = data;
    pool->size = size;
    return pool;
}

void lorie_shm_pool_destroy(struct lorie_shm_pool *pool) {
    if (!pool) return;
    if (pool->data && pool->data != MAP_FAILED)
        munmap(pool->data, pool->size);
    free(pool);
}

/* --- wl_shm_pool implementation --- */

static void shm_pool_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static void shm_pool_create_buffer(struct wl_client *client, struct wl_resource *resource,
                                   uint32_t id, int32_t offset, int32_t width,
                                   int32_t height, int32_t stride, uint32_t format) {
    /* Deferred to PR #3: buffer creation from shm pool */
    (void)client; (void)resource; (void)id; (void)offset;
    (void)width; (void)height; (void)stride; (void)format;
}

static void shm_pool_resize(struct wl_client *client, struct wl_resource *resource,
                            int32_t size) {
    /* Deferred to PR #3: pool resize */
    (void)client; (void)resource; (void)size;
}

static const struct wl_shm_pool_interface shm_pool_impl = {
    shm_pool_create_buffer,
    shm_pool_destroy,
    shm_pool_resize,
};

static void shm_pool_handle_resource_destroy(struct wl_resource *resource) {
    struct lorie_shm_pool *pool = wl_resource_get_user_data(resource);
    lorie_shm_pool_destroy(pool);
}

struct lorie_compositor *lorie_compositor_create(void) {
    struct lorie_compositor *c = calloc(1, sizeof(*c));
    if (!c) {
        LOGE("Failed to allocate compositor");
        return NULL;
    }

    wl_list_init(&c->outputs);
    wl_list_init(&c->surfaces);
    wl_list_init(&c->clients);

    if (pthread_mutex_init(&c->lock, NULL) != 0) {
        LOGE("Failed to init mutex");
        free(c);
        return NULL;
    }

    c->display = wl_display_create();
    if (!c->display) {
        LOGE("Failed to create wl_display");
        pthread_mutex_destroy(&c->lock);
        free(c);
        return NULL;
    }

    c->event_loop = wl_display_get_event_loop(c->display);
    if (!c->event_loop) {
        LOGE("Failed to get event loop");
        wl_display_destroy(c->display);
        pthread_mutex_destroy(&c->lock);
        free(c);
        return NULL;
    }

    c->compositor_global = wl_global_create(
        c->display, &wl_compositor_interface, 5, c, compositor_bind);
    if (!c->compositor_global) {
        LOGE("Failed to create wl_compositor global");
        goto fail_globals;
    }

    c->subcompositor_global = wl_global_create(
        c->display, &wl_subcompositor_interface, 1, c, subcompositor_bind);
    if (!c->subcompositor_global) {
        LOGE("Failed to create wl_subcompositor global");
        goto fail_globals;
    }

    c->shm_global = wl_global_create(
        c->display, &wl_shm_interface, 1, c, shm_bind);
    if (!c->shm_global) {
        LOGE("Failed to create wl_shm global");
        goto fail_globals;
    }

    c->input = lorie_input_init(c->display);
    if (!c->input) {
        LOGE("Failed to create input");
        goto fail_globals;
    }

    c->xdg_shell_global = lorie_xdg_shell_create(c->display);
    if (!c->xdg_shell_global) {
        LOGE("Failed to create xdg_shell global");
        goto fail_globals;
    }

    /* linux_dmabuf_global is created conditionally after renderer init
     * via lorie_compositor_create_dmabuf_global() */
    c->linux_dmabuf_global = NULL;

    c->clipboard = lorie_clipboard_create(c);
    if (!c->clipboard) {
        LOGE("Failed to create clipboard");
        goto fail_globals;
    }

    c->data_device_manager_global = lorie_data_device_manager_create(c->display, c);
    if (!c->data_device_manager_global) {
        LOGE("Failed to create data_device_manager global");
        goto fail_globals;
    }

    c->viewporter_global = lorie_viewporter_create(c->display);
    if (!c->viewporter_global) {
        LOGE("Failed to create viewporter global");
        goto fail_globals;
    }

    /* wl_output global is created when an output is added */
    c->output_global = NULL;

    LOGI("Compositor created");
    return c;

fail_globals:
    if (c->compositor_global)
        wl_global_destroy(c->compositor_global);
    if (c->subcompositor_global)
        wl_global_destroy(c->subcompositor_global);
    if (c->shm_global)
        wl_global_destroy(c->shm_global);
    wl_display_destroy(c->display);
    pthread_mutex_destroy(&c->lock);
    free(c);
    return NULL;
}

void lorie_compositor_destroy(struct lorie_compositor *c) {
    if (!c)
        return;

    if (c->running)
        lorie_compositor_stop(c);

    /* Destroy outputs */
    struct lorie_output *output, *tmp;
    wl_list_for_each_safe(output, tmp, &c->outputs, link) {
        lorie_output_destroy(output);
    }

    if (c->output_global)
        wl_global_destroy(c->output_global);
    if (c->viewporter_global)
        wl_global_destroy(c->viewporter_global);
    if (c->data_device_manager_global)
        wl_global_destroy(c->data_device_manager_global);
    if (c->linux_dmabuf_global)
        wl_global_destroy(c->linux_dmabuf_global);
    if (c->xdg_shell_global)
        wl_global_destroy(c->xdg_shell_global);
    lorie_clipboard_destroy(c->clipboard);
    lorie_input_destroy(c->input);
    if (c->shm_global)
        wl_global_destroy(c->shm_global);
    if (c->subcompositor_global)
        wl_global_destroy(c->subcompositor_global);
    if (c->compositor_global)
        wl_global_destroy(c->compositor_global);

    if (c->display)
        wl_display_destroy(c->display);

    pthread_mutex_destroy(&c->lock);

    free(c);
    LOGI("Compositor destroyed");
}

static void *event_loop_thread_fn(void *data) {
    struct lorie_compositor *c = data;
    while (c->running) {
        wl_event_loop_dispatch(c->event_loop, -1);
    }
    return NULL;
}

int lorie_compositor_start(struct lorie_compositor *c) {
    if (!c || !c->display)
        return -1;
    if (c->running)
        return 0;

    const char *socket_name = wl_display_add_socket_auto(c->display);
    if (!socket_name) {
        LOGE("Failed to add socket");
        return -1;
    }
    LOGI("Wayland socket: %s", socket_name);

    /* Create output global if we have outputs */
    if (!wl_list_empty(&c->outputs) && !c->output_global) {
        struct lorie_output *output =
            wl_container_of(c->outputs.next, output, link);
        c->output_global = wl_global_create(
            c->display, &wl_output_interface, 3, output, output_bind);
        if (!c->output_global) {
            LOGE("Failed to create wl_output global");
            return -1;
        }
    }

    c->running = 1;
    if (pthread_create(&c->event_loop_thread, NULL,
                       event_loop_thread_fn, c) != 0) {
        LOGE("Failed to create event loop thread");
        c->running = 0;
        return -1;
    }

    LOGI("Compositor started");
    return 0;
}

void lorie_compositor_stop(struct lorie_compositor *c) {
    if (!c || !c->running)
        return;

    c->running = 0;

    /* Wake up event loop so thread can exit */
    wl_event_loop_dispatch(c->event_loop, 0);

    /* Destroy all clients first (critical from review) */
    wl_display_destroy_clients(c->display);

    pthread_join(c->event_loop_thread, NULL);

    LOGI("Compositor stopped");
}

void lorie_compositor_create_dmabuf_global(struct lorie_compositor *c) {
    if (!c || c->linux_dmabuf_global)
        return;
    c->linux_dmabuf_global = lorie_linux_dmabuf_create(c->display, c);
    if (!c->linux_dmabuf_global) {
        LOGE("Failed to create linux_dmabuf global");
    } else {
        LOGI("linux_dmabuf global created");
    }
}

void lorie_compositor_set_window(struct lorie_compositor *c,
                                  ANativeWindow *window) {
    if (!c)
        return;

    pthread_mutex_lock(&c->lock);
    if (c->native_window == window) {
        pthread_mutex_unlock(&c->lock);
        return;
    }
    if (c->native_window) {
        ANativeWindow_release(c->native_window);
    }
    c->native_window = window;
    if (window) {
        ANativeWindow_acquire(window);
    }
    pthread_mutex_unlock(&c->lock);

    LOGI("Window set to %p", (void *)window);
}

/* Forward declarations — implemented in surface.c */
void compositor_create_surface(struct wl_client *client,
                               struct wl_resource *resource, uint32_t id);
void compositor_create_region(struct wl_client *client,
                              struct wl_resource *resource, uint32_t id);
void subcompositor_get_subsurface(struct wl_client *client,
                                  struct wl_resource *resource, uint32_t id,
                                  struct wl_resource *surface,
                                  struct wl_resource *parent);

static const struct wl_compositor_interface compositor_impl = {
    compositor_create_surface,
    compositor_create_region,
};

static void compositor_bind(struct wl_client *client, void *data,
                            uint32_t version, uint32_t id) {
    struct lorie_compositor *c = data;
    struct wl_resource *resource =
        wl_resource_create(client, &wl_compositor_interface,
                           (int)version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &compositor_impl, c, NULL);
}

static void subcompositor_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
    (void)client;
}

static const struct wl_subcompositor_interface subcompositor_impl = {
    subcompositor_destroy,
    subcompositor_get_subsurface,
};

static void subcompositor_bind(struct wl_client *client, void *data,
                               uint32_t version, uint32_t id) {
    struct lorie_compositor *c = data;
    struct wl_resource *resource =
        wl_resource_create(client, &wl_subcompositor_interface,
                           (int)version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &subcompositor_impl, c, NULL);
}

/* --- wl_shm implementation stubs --- */

static void shm_create_pool(struct wl_client *client,
                            struct wl_resource *resource,
                            uint32_t id, int32_t fd, int32_t size) {
    struct lorie_shm_pool *pool = lorie_shm_pool_create(fd, size);
    if (!pool) {
        wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "mmap failed");
        close(fd);
        return;
    }
    close(fd);

    struct wl_resource *pool_resource = wl_resource_create(client, &wl_shm_pool_interface, 1, id);
    if (!pool_resource) {
        lorie_shm_pool_destroy(pool);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(pool_resource, &shm_pool_impl, pool, shm_pool_handle_resource_destroy);
}

static const struct wl_shm_interface shm_impl = {
    shm_create_pool,
};

static void shm_bind(struct wl_client *client, void *data,
                     uint32_t version, uint32_t id) {
    struct lorie_compositor *c = data;
    struct wl_resource *resource =
        wl_resource_create(client, &wl_shm_interface,
                           (int)version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &shm_impl, c, NULL);
}
