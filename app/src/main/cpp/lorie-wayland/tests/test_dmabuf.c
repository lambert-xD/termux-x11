/*
 * Lorie Wayland Compositor — DMA-BUF Import Tests (PR #4)
 *
 * TDD cycle:
 *   RED  : linux-dmabuf.c is a stub that closes FDs without validation or import.
 *   GREEN: params_create validates dimensions, format, planes; EGLImage import
 *          creates textures; renderer binds dmabuf textures; SHM path intact.
 */

#include "lorie_test.h"
#include "../compositor.h"
#include "../protocols/linux-dmabuf.h"
#include "../renderer.h"
#include "stable-linux-dmabuf-linux-dmabuf-v1.h"

/* DRM_FORMAT_NV12 (not supported by our compositor) */
#define DRM_FORMAT_NV12 0x3231564E

static void test_dmabuf_format_supported(void) {
    ASSERT_TRUE(lorie_dmabuf_format_supported(DRM_FORMAT_ABGR8888));
    ASSERT_TRUE(lorie_dmabuf_format_supported(DRM_FORMAT_XBGR8888));
    ASSERT_TRUE(lorie_dmabuf_format_supported(DRM_FORMAT_ARGB8888));
    ASSERT_FALSE(lorie_dmabuf_format_supported(DRM_FORMAT_NV12));
}

static void test_dmabuf_params_validate_abgr8888(void) {
    struct lorie_dmabuf_plane planes[1] = {{.fd = 1, .offset = 0, .stride = 256}};
    int err = lorie_dmabuf_params_validate(100, 100, DRM_FORMAT_ABGR8888, 1, planes);
    ASSERT_EQ_INT(0, err);
}

static void test_dmabuf_params_reject_nv12(void) {
    struct lorie_dmabuf_plane planes[1] = {{.fd = 1, .offset = 0, .stride = 256}};
    int err = lorie_dmabuf_params_validate(100, 100, DRM_FORMAT_NV12, 1, planes);
    ASSERT_EQ_INT(ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT, err);
}

static void test_dmabuf_params_reject_zero_size(void) {
    struct lorie_dmabuf_plane planes[1] = {{.fd = 1, .offset = 0, .stride = 256}};
    int err = lorie_dmabuf_params_validate(0, 100, DRM_FORMAT_ABGR8888, 1, planes);
    ASSERT_EQ_INT(ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS, err);

    err = lorie_dmabuf_params_validate(100, 0, DRM_FORMAT_ABGR8888, 1, planes);
    ASSERT_EQ_INT(ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS, err);
}

static void test_dmabuf_params_reject_no_planes(void) {
    int err = lorie_dmabuf_params_validate(100, 100, DRM_FORMAT_ABGR8888, 0, NULL);
    ASSERT_EQ_INT(ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, err);
}

static void test_dmabuf_params_reject_invalid_fd(void) {
    struct lorie_dmabuf_plane planes[1] = {{.fd = -1, .offset = 0, .stride = 256}};
    int err = lorie_dmabuf_params_validate(100, 100, DRM_FORMAT_ABGR8888, 1, planes);
    ASSERT_EQ_INT(ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, err);
}

static void test_dmabuf_import_no_egl(void) {
    /* Without EGL function pointers, import should fail gracefully */
    struct lorie_dmabuf_plane planes[1] = {{.fd = -1, .offset = 0, .stride = 256}};
    struct lorie_dmabuf_buffer *buf = lorie_dmabuf_buffer_import(
        NULL, 100, 100, DRM_FORMAT_ABGR8888, 1, planes,
        NULL, NULL, NULL, NULL);
    ASSERT_NULL(buf);
}

static void test_dmabuf_renderer_has_dmabuf_flag(void) {
    struct lorie_renderer *r = lorie_renderer_create();
    ASSERT_NOT_NULL(r);
    /* Before init, flag should be 0 */
    ASSERT_FALSE(lorie_renderer_has_dmabuf_import(r));
    lorie_renderer_destroy(r);
}

static void test_dmabuf_shm_fallback(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_surface *s = lorie_surface_create_internal(c, NULL, 0);
    ASSERT_NOT_NULL(s);

    /* SHM buffer path: buffer_resource set but no dmabuf data */
    s->buffer_resource = NULL;
    s->buffer = NULL;

    /* Verify surface exists and can be destroyed without dmabuf logic interfering */
    lorie_surface_destroy_internal(s);
    lorie_compositor_destroy(c);
}

int lorie_test_dmabuf_suite(struct lorie_test_suite *suite) {
    lorie_suite_init(suite, "dmabuf", NULL, NULL);
    SUITE_ADD(suite, test_dmabuf_format_supported);
    SUITE_ADD(suite, test_dmabuf_params_validate_abgr8888);
    SUITE_ADD(suite, test_dmabuf_params_reject_nv12);
    SUITE_ADD(suite, test_dmabuf_params_reject_zero_size);
    SUITE_ADD(suite, test_dmabuf_params_reject_no_planes);
    SUITE_ADD(suite, test_dmabuf_params_reject_invalid_fd);
    SUITE_ADD(suite, test_dmabuf_import_no_egl);
    SUITE_ADD(suite, test_dmabuf_renderer_has_dmabuf_flag);
    SUITE_ADD(suite, test_dmabuf_shm_fallback);
    return 0;
}
