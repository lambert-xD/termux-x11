#include "compositor.h"
#include <android/log.h>

#define LOG_TAG "LorieWayland"
#define log(prio, ...) __android_log_print(ANDROID_LOG_ ## prio, LOG_TAG, __VA_ARGS__)

/* ============================================================================
 * Output global
 * ============================================================================ */

void output_bind(struct wl_client* client, void* data,
                 uint32_t version, uint32_t id) {
    struct lorie_output* output = data;
    struct wl_resource* resource = wl_resource_create(client, &wl_output_interface,
                                                      version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_output_send_geometry(resource, output->x, output->y,
                            output->physical_width, output->physical_height,
                            output->subpixel, output->make, output->model,
                            output->transform);
    wl_output_send_mode(resource, output->flags, output->width, output->height,
                        output->refresh);
    wl_output_send_scale(resource, output->scale);
    wl_output_send_done(resource);
}

void lorie_output_damage_all(struct lorie_output* output) {
    if (!output || !output->compositor)
        return;

    struct lorie_surface* surface;
    lorie_compositor_lock(output->compositor);
    wl_list_for_each(surface, &output->compositor->surfaces, link) {
        if (surface->mapped) {
            pixman_region32_union_rect(
                &surface->current_damage, &surface->current_damage,
                0, 0, surface->width, surface->height);
        }
    }
    output->compositor->redraw_needed = 1;
    pthread_cond_broadcast(&output->compositor->cond);
    lorie_compositor_unlock(output->compositor);
}
