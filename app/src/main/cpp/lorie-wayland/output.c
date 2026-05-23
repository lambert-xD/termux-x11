#include "compositor.h"

#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "LorieOutput"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static void output_bind(struct wl_client *client, void *data,
                        uint32_t version, uint32_t id) {
    struct lorie_output *output = data;
    struct wl_resource *resource =
        wl_resource_create(client, &wl_output_interface,
                           (int)version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_output_send_geometry(resource, 0, 0,
                            output->width * output->scale,
                            output->height * output->scale,
                            0, "Lorie", "Internal",
                            WL_OUTPUT_TRANSFORM_NORMAL);

    wl_output_send_mode(resource,
                        WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
                        output->width, output->height,
                        output->refresh * 1000);

    if (version >= WL_OUTPUT_SCALE_SINCE_VERSION) {
        wl_output_send_scale(resource, output->scale);
    }

    wl_output_send_done(resource);
}

struct lorie_output *lorie_output_create(struct lorie_compositor *c,
                                          int32_t width, int32_t height,
                                          int32_t scale) {
    if (width <= 0 || height <= 0 || scale <= 0) {
        LOGE("Invalid output dimensions: %dx%d@%d", width, height, scale);
        return NULL;
    }

    struct lorie_output *output = calloc(1, sizeof(*output));
    if (!output) {
        LOGE("Failed to allocate output");
        return NULL;
    }

    output->compositor = c;
    output->width = width;
    output->height = height;
    output->scale = scale;
    output->refresh = 60;
    strncpy(output->name, "Lorie-0", sizeof(output->name) - 1);
    wl_list_init(&output->bound_resources);

    wl_list_insert(&c->outputs, &output->link);

    LOGI("Output created: %dx%d@%d", width, height, scale);
    return output;
}

void lorie_output_destroy(struct lorie_output *output) {
    if (!output)
        return;

    wl_list_remove(&output->link);

    if (output->global) {
        wl_global_destroy(output->global);
    }

    struct lorie_output_resource *or, *or_tmp;
    wl_list_for_each_safe(or, or_tmp, &output->bound_resources, link) {
        if (or->resource)
            wl_resource_set_user_data(or->resource, NULL);
        wl_list_remove(&or->link);
        free(or);
    }

    free(output);
    LOGI("Output destroyed");
}

void lorie_output_update_size(struct lorie_output *output,
                               int32_t w, int32_t h, int32_t scale) {
    if (!output || w <= 0 || h <= 0 || scale <= 0) {
        LOGE("Invalid output update dimensions: %dx%d@%d", w, h, scale);
        return;
    }
    output->width = w;
    output->height = h;
    output->scale = scale;
    struct lorie_output_resource *or;
    wl_list_for_each(or, &output->bound_resources, link) {
        if (or->resource) {
            wl_output_send_geometry(or->resource, 0, 0,
                                    output->width, output->height, 0,
                                    "Unknown", "Unknown", 0);
            wl_output_send_mode(or->resource, 0,
                                output->width, output->height,
                                output->refresh);
            if (wl_resource_get_version(or->resource) >= WL_OUTPUT_SCALE_SINCE_VERSION)
                wl_output_send_scale(or->resource, output->scale);
            wl_output_send_done(or->resource);
        }
    }
    LOGI("Output updated: %dx%d@%d", w, h, scale);
}
