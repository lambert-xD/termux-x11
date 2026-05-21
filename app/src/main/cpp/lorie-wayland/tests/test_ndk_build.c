/*
 * Lorie Wayland Compositor — NDK Build Verification Tests (PR #1)
 *
 * TDD cycle:
 *   RED  : renderer.c contains EGL/GLES type stubs that conflict with
 *          real NDK headers; shm_create_pool is a no-op stub.
 *   GREEN: Stubs removed, real headers included; shm_create_pool
 *          implemented with mmap.
 */

#include "lorie_test.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include "../compositor.h"

static void test_ndk_headers_present(void) {
    /* Verify real NDK EGL/GLES headers are available and usable.
     * If renderer.c stubs were present in the same TU, this would
     * fail with type redefinition errors. */
    EGLDisplay dpy = EGL_NO_DISPLAY;
    ASSERT_EQ_PTR(NULL, (void*)dpy);

    EGLint attribs[] = {EGL_NONE};
    (void)attribs;

    GLenum err = GL_NO_ERROR;
    ASSERT_EQ_INT(0, (int)err);

    /* Verify key constants from real headers */
    ASSERT_EQ_INT(0x3038, EGL_NONE);
    ASSERT_EQ_INT(0x8B31, GL_VERTEX_SHADER);
    ASSERT_EQ_INT(0x8B30, GL_FRAGMENT_SHADER);
    ASSERT_EQ_INT(0x00004000, GL_COLOR_BUFFER_BIT);
}

static void test_shm_pool_create(void) {
    int fd = open("/dev/zero", O_RDONLY);
    ASSERT_TRUE(fd >= 0);

    struct lorie_shm_pool *pool = lorie_shm_pool_create(fd, 4096);
    close(fd);

    /* RED: stub returns NULL. GREEN: returns valid pool */
    ASSERT_NOT_NULL(pool);

    lorie_shm_pool_destroy(pool);
}

static void test_shm_pool_mmap(void) {
    int fd = open("/dev/zero", O_RDONLY);
    ASSERT_TRUE(fd >= 0);

    struct lorie_shm_pool *pool = lorie_shm_pool_create(fd, 4096);
    close(fd);

    ASSERT_NOT_NULL(pool);
    ASSERT_NOT_NULL(pool->data);
    ASSERT_EQ_INT(4096, pool->size);

    /* Verify memory is zeroed (from /dev/zero) */
    ASSERT_EQ_INT(0, ((uint8_t*)pool->data)[0]);
    ASSERT_EQ_INT(0, ((uint8_t*)pool->data)[4095]);

    lorie_shm_pool_destroy(pool);
}

int lorie_test_ndk_build_suite(struct lorie_test_suite* suite) {
    lorie_suite_init(suite, "ndk_build", NULL, NULL);
    SUITE_ADD(suite, test_ndk_headers_present);
    SUITE_ADD(suite, test_shm_pool_create);
    SUITE_ADD(suite, test_shm_pool_mmap);
    return 0;
}
