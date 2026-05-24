/*
 * Lorie Wayland Compositor — Integration Tests
 */

#include "lorie_test.h"
#include "compositor.h"
#include "renderer.h"
#include "input.h"

/* Declared in main.c */
extern int lorie_wayland_main(void);
extern void lorie_wayland_stop(void);
extern struct lorie_compositor *lorie_wayland_get_compositor(void);
extern struct lorie_renderer *lorie_wayland_get_renderer(void);

static void test_full_lifecycle(void) {
    ASSERT_EQ_INT(0, lorie_wayland_main());
    struct lorie_compositor *c = lorie_wayland_get_compositor();
    ASSERT_NOT_NULL(c);
    ASSERT_NOT_NULL(c->display);
    ASSERT_NOT_NULL(c->input);
    ASSERT_TRUE(atomic_load(&c->running));
    /* Globals must exist after start */
    ASSERT_NOT_NULL(c->compositor_global);
    ASSERT_NOT_NULL(c->subcompositor_global);
    ASSERT_NOT_NULL(c->shm_global);
    ASSERT_NOT_NULL(c->xdg_shell_global);
    ASSERT_NOT_NULL(c->linux_dmabuf_global);
    ASSERT_NOT_NULL(c->data_device_manager_global);
    ASSERT_NOT_NULL(lorie_wayland_get_renderer());
    lorie_wayland_stop();
    ASSERT_NULL(lorie_wayland_get_compositor());
    ASSERT_NULL(lorie_wayland_get_renderer());
}

static void test_surface_attach_and_render(void) {
    ASSERT_EQ_INT(0, lorie_wayland_main());
    struct lorie_compositor *c = lorie_wayland_get_compositor();
    struct lorie_renderer *r = lorie_wayland_get_renderer();
    ASSERT_NOT_NULL(c);
    ASSERT_NOT_NULL(r);
    /* Create surface via internal API */
    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 1);
    ASSERT_NOT_NULL(s);
    /* Add to renderer */
    lorie_renderer_add_surface(r, s);
    /* Commit should not crash even without buffer */
    ASSERT_EQ_INT(0, lorie_renderer_commit(r));
    /* Clean up */
    lorie_renderer_remove_surface(r, s);
    lorie_surface_destroy_internal(s);
    lorie_wayland_stop();
}

static void test_input_to_surface(void) {
    ASSERT_EQ_INT(0, lorie_wayland_main());
    struct lorie_compositor *c = lorie_wayland_get_compositor();
    ASSERT_NOT_NULL(c);
    ASSERT_NOT_NULL(c->input);
    /* Queue events — dispatch runs on event loop, but queue must accept */
    lorie_input_pointer_motion(c->input, 100.0f, 200.0f);
    lorie_input_pointer_button(c->input, 0x110, 1);
    lorie_input_keyboard_key(c->input, 30, 1);
    lorie_input_touch_down(c->input, 0, 50.0f, 50.0f);
    /* Queue should have entries (internal state check via dispatch) */
    ASSERT_EQ_INT(0, lorie_input_dispatch(c->input));
    lorie_wayland_stop();
}

int lorie_test_integration_suite(struct lorie_test_suite *suite) {
    lorie_suite_init(suite, "integration", NULL, NULL);
    SUITE_ADD(suite, test_full_lifecycle);
    SUITE_ADD(suite, test_surface_attach_and_render);
    SUITE_ADD(suite, test_input_to_surface);
    return 0;
}
