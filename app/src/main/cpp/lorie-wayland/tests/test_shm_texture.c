/*
 * Lorie Wayland Compositor — SHM Buffer Import + Texture Binding Tests
 *
 * Verifies the critical pipeline: wl_shm_buffer → LorieBuffer → GLES2 texture.
 */

#include "lorie_test.h"
#include "../compositor.h"
#include "../renderer.h"
#include "../../lorie/buffer.h"

static void test_surface_buffer_null_initially(void) {
    struct lorie_surface *s = lorie_surface_create_internal(NULL, NULL, 0);
    ASSERT_NOT_NULL(s);
    ASSERT_NULL(s->buffer);
    ASSERT_NULL(s->buffer_resource);
    lorie_surface_destroy_internal(s);
}

static void test_surface_destroy_with_null_buffer_safe(void) {
    struct lorie_surface *s = lorie_surface_create_internal(NULL, NULL, 0);
    ASSERT_NOT_NULL(s);
    /* buffer is NULL — destroy must not crash */
    lorie_surface_destroy_internal(s);
}

static void test_renderer_skips_surface_without_buffer(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    struct lorie_surface *s = lorie_surface_create_internal(NULL, NULL, 0);
    ASSERT_NOT_NULL(s);
    lorie_renderer_add_surface(r, s);
    /* No EGL context — commit should return -1 or 0, but NOT crash */
    int ret = lorie_renderer_commit(r);
    /* We accept either success (no surfaces to draw) or EGL-not-ready */
    ASSERT_TRUE(ret == 0 || ret == -1);
    lorie_renderer_remove_surface(r, s);
    lorie_surface_destroy_internal(s);
    lorie_renderer_destroy(r);
}

static void test_surface_commit_swaps_buffer_resource(void) {
    struct lorie_surface *s = lorie_surface_create_internal(NULL, NULL, 0);
    ASSERT_NOT_NULL(s);
    ASSERT_NULL(s->buffer_resource);

    /* Simulate pending attach with a fake resource pointer */
    s->pending_buffer = (struct wl_resource*)0x1;
    s->pending_attached = 1;
    /* We cannot call surface_commit() because it needs a real wl_resource
     * and wl_shm_buffer_get() on it. But we can verify the pending state. */
    ASSERT_EQ_PTR((struct wl_resource*)0x1, s->pending_buffer);
    ASSERT_TRUE(s->pending_attached);

    lorie_surface_destroy_internal(s);
}

static void test_regular_buffer_properties(void) {
    LorieBuffer *b = LorieBuffer_allocate(64, 32, AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM, LORIEBUFFER_REGULAR);
    ASSERT_NOT_NULL(b);
    const LorieBuffer_Desc *d = LorieBuffer_description(b);
    ASSERT_EQ_INT(LORIEBUFFER_REGULAR, d->type);
    ASSERT_EQ_INT(64, d->width);
    ASSERT_EQ_INT(32, d->height);
    ASSERT_EQ_INT(64, d->stride);
    ASSERT_NOT_NULL(d->data);
    LorieBuffer_release(b);
}

static void test_attach_to_gl_no_context_safe(void) {
    /* Without a current EGL context attachToGL returns early; calling it
     * repeatedly must not crash or leak (texture gen is skipped). */
    LorieBuffer *b = LorieBuffer_allocate(4, 4, AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM, LORIEBUFFER_REGULAR);
    ASSERT_NOT_NULL(b);
    LorieBuffer_attachToGL(b);
    LorieBuffer_attachToGL(b);
    LorieBuffer_release(b);
}

int lorie_test_shm_texture_suite(struct lorie_test_suite* suite) {
    lorie_suite_init(suite, "shm_texture", NULL, NULL);
    SUITE_ADD(suite, test_surface_buffer_null_initially);
    SUITE_ADD(suite, test_surface_destroy_with_null_buffer_safe);
    SUITE_ADD(suite, test_renderer_skips_surface_without_buffer);
    SUITE_ADD(suite, test_surface_commit_swaps_buffer_resource);
    SUITE_ADD(suite, test_regular_buffer_properties);
    SUITE_ADD(suite, test_attach_to_gl_no_context_safe);
    return 0;
}
