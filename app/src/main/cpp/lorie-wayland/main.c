/*
 * Lorie Wayland Compositor — Main Integration
 *
 * Wires compositor, renderer, input, output, and protocols together.
 */

#include "compositor.h"
#include "input.h"
#include "renderer.h"
#include <android/log.h>

#define LOG_TAG "LorieWayland"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static struct lorie_compositor *g_comp = NULL;
static struct lorie_renderer *g_rend = NULL;

int lorie_wayland_main(void) {
    if (g_comp) {
        LOGI("Wayland already running");
        return 0;
    }

    g_comp = lorie_compositor_create();
    if (!g_comp) {
        LOGE("Failed to create compositor");
        return -1;
    }

    g_rend = lorie_renderer_create();
    if (!g_rend || lorie_renderer_init(g_rend) != 0) {
        LOGE("Failed to create renderer");
        goto fail;
    }

    struct lorie_output *out = lorie_output_create(g_comp, 1920, 1080, 1);
    if (!out) {
        LOGE("Failed to create output");
        goto fail;
    }

    if (lorie_compositor_start(g_comp) != 0) {
        LOGE("Failed to start compositor");
        goto fail;
    }

    LOGI("Wayland compositor started");
    return 0;

fail:
    if (g_rend) {
        lorie_renderer_fini(g_rend);
        lorie_renderer_destroy(g_rend);
        g_rend = NULL;
    }
    if (g_comp) {
        lorie_compositor_destroy(g_comp);
        g_comp = NULL;
    }
    return -1;
}

void lorie_wayland_stop(void) {
    if (g_comp) {
        lorie_compositor_stop(g_comp);
        lorie_compositor_destroy(g_comp);
        g_comp = NULL;
    }
    if (g_rend) {
        lorie_renderer_fini(g_rend);
        lorie_renderer_destroy(g_rend);
        g_rend = NULL;
    }
    LOGI("Wayland compositor stopped");
}

void lorie_wayland_set_window(ANativeWindow *window) {
    if (g_comp) lorie_compositor_set_window(g_comp, window);
    if (g_rend) lorie_renderer_set_window(g_rend, window);
}

struct lorie_compositor *lorie_wayland_get_compositor(void) { return g_comp; }
struct lorie_renderer *lorie_wayland_get_renderer(void) { return g_rend; }
