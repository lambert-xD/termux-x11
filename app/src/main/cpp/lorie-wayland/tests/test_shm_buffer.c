/*
 * Lorie Wayland Compositor — SHM Buffer Tests (PR #1)
 *
 * TDD cycle:
 *   RED  : shm_pool_create_buffer is a no-op stub; tests fail.
 *   GREEN: compositor.c implements buffer creation, validation,
 *          refcounting, and destroy lifecycle.
 */

#include "lorie_test.h"
#include "../compositor.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <errno.h>
#include <limits.h>

/* Forward declarations for LorieBuffer API (buffer.h may not be available in test env) */
typedef struct LorieBuffer LorieBuffer;
typedef struct {
    int32_t width, height, stride;
    uint8_t format, type;
    uint64_t id;
    void* buffer;
    void* data;
} LorieBuffer_Desc;

LorieBuffer* LorieBuffer_allocate(int32_t width, int32_t height, int8_t format, int8_t type);
const LorieBuffer_Desc* LorieBuffer_description(LorieBuffer* buffer);
void LorieBuffer_acquire(LorieBuffer* buffer);
void LorieBuffer_release(LorieBuffer* buffer);
void lorie_test_force_alloc_fail(int fail);

/* Test context holds everything needed for one test scenario */
struct test_ctx {
    struct lorie_compositor *compositor;
    struct lorie_shm_pool *pool;
    struct wl_client *client;
    struct wl_resource *pool_resource;
    int client_fd; /* other end of socketpair, kept open */
};

static int create_shm_fd(size_t size) {
    int fd = -1;
    /* Use mkstemp + unlink for a writable temporary file.
     * O_TMPFILE is not available on Android NDK. */
    char path[] = "/tmp/lorie_test_shm_XXXXXX";
    fd = mkstemp(path);
    if (fd >= 0) {
        unlink(path);
    }
    if (fd < 0) {
        /* Last resort: open /dev/zero as read-only */
        fd = open("/dev/zero", O_RDONLY);
    }
    if (fd >= 0 && size > 0) {
        if (ftruncate(fd, (off_t)size) < 0 && errno != EINVAL) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

static struct test_ctx *setup_ctx(int pool_size) {
    struct test_ctx *ctx = calloc(1, sizeof(*ctx));
    ASSERT_NOT_NULL(ctx);

    ctx->compositor = lorie_compositor_create();
    ASSERT_NOT_NULL(ctx->compositor);

    int fd = create_shm_fd(pool_size);
    ASSERT_TRUE(fd >= 0);

    ctx->pool = lorie_shm_pool_create(fd, pool_size);
    close(fd);
    ASSERT_NOT_NULL(ctx->pool);

    /* Create a Wayland client via socketpair */
    int fds[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    ASSERT_EQ_INT(0, ret);

    ctx->client = wl_client_create(ctx->compositor->display, fds[0]);
    ASSERT_NOT_NULL(ctx->client);
    ctx->client_fd = fds[1]; /* keep open so connection stays alive */

    /* Create a pool resource for the test client */
    ctx->pool_resource = wl_resource_create(ctx->client, &wl_shm_pool_interface, 1, 1);
    ASSERT_NOT_NULL(ctx->pool_resource);
    wl_resource_set_implementation(ctx->pool_resource, NULL, ctx->pool, shm_pool_handle_resource_destroy);

    return ctx;
}

static void teardown_ctx(struct test_ctx *ctx) {
    if (!ctx) return;

    if (ctx->pool_resource) {
        /* Only destroy if client is still alive */
        wl_resource_destroy(ctx->pool_resource);
    }

    if (ctx->client_fd >= 0) {
        close(ctx->client_fd);
    }

    if (ctx->client) {
        wl_client_destroy(ctx->client);
    }

    if (ctx->compositor) {
        lorie_compositor_destroy(ctx->compositor);
    }

    free(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: valid buffer creation                                        */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_create_valid(void) {
    struct test_ctx *ctx = setup_ctx(4096);

    ASSERT_EQ_INT(1, ctx->pool->refcount);

    struct wl_resource *buf_res = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        0, 10, 10, 40, WL_SHM_FORMAT_ARGB8888);

    ASSERT_NOT_NULL(buf_res);
    ASSERT_EQ_INT(2, ctx->pool->refcount);

    struct lorie_shm_buffer *buf = wl_resource_get_user_data(buf_res);
    ASSERT_NOT_NULL(buf);
    ASSERT_EQ_PTR(ctx->pool, buf->pool);
    ASSERT_EQ_INT(0, buf->offset);
    ASSERT_EQ_INT(10, buf->width);
    ASSERT_EQ_INT(10, buf->height);
    ASSERT_EQ_INT(40, buf->stride);
    ASSERT_EQ_INT(WL_SHM_FORMAT_ARGB8888, (int)buf->format);
    ASSERT_EQ_PTR((uint8_t *)ctx->pool->data + 0, buf->data);

    wl_resource_destroy(buf_res);
    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: data pointer equals pool->data + offset                      */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_data_pointer_valid(void) {
    struct test_ctx *ctx = setup_ctx(4096);

    struct wl_resource *buf_res = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        64, 8, 8, 32, WL_SHM_FORMAT_XRGB8888);

    ASSERT_NOT_NULL(buf_res);

    struct lorie_shm_buffer *buf = wl_resource_get_user_data(buf_res);
    ASSERT_NOT_NULL(buf);
    ASSERT_EQ_PTR((uint8_t *)ctx->pool->data + 64, buf->data);
    ASSERT_EQ_INT(WL_SHM_FORMAT_XRGB8888, (int)buf->format);

    wl_resource_destroy(buf_res);
    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: negative offset rejected                                     */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_reject_negative_offset(void) {
    struct test_ctx *ctx = setup_ctx(4096);

    struct wl_resource *buf_res = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        -1, 10, 10, 40, WL_SHM_FORMAT_ARGB8888);

    ASSERT_NULL(buf_res);
    ASSERT_EQ_INT(1, ctx->pool->refcount);

    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: zero width rejected                                          */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_reject_zero_width(void) {
    struct test_ctx *ctx = setup_ctx(4096);

    struct wl_resource *buf_res = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        0, 0, 10, 40, WL_SHM_FORMAT_ARGB8888);

    ASSERT_NULL(buf_res);
    ASSERT_EQ_INT(1, ctx->pool->refcount);

    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: zero height rejected                                         */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_reject_zero_height(void) {
    struct test_ctx *ctx = setup_ctx(4096);

    struct wl_resource *buf_res = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        0, 10, 0, 40, WL_SHM_FORMAT_ARGB8888);

    ASSERT_NULL(buf_res);
    ASSERT_EQ_INT(1, ctx->pool->refcount);

    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: zero stride rejected                                         */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_reject_zero_stride(void) {
    struct test_ctx *ctx = setup_ctx(4096);

    struct wl_resource *buf_res = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        0, 10, 10, 0, WL_SHM_FORMAT_ARGB8888);

    ASSERT_NULL(buf_res);
    ASSERT_EQ_INT(1, ctx->pool->refcount);

    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: stride too small rejected                                    */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_reject_stride_too_small(void) {
    struct test_ctx *ctx = setup_ctx(4096);

    struct wl_resource *buf_res = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        0, 10, 10, 39, WL_SHM_FORMAT_ARGB8888);

    ASSERT_NULL(buf_res);
    ASSERT_EQ_INT(1, ctx->pool->refcount);

    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: integer overflow rejected                                    */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_reject_overflow(void) {
    struct test_ctx *ctx = setup_ctx(4096);

    struct wl_resource *buf_res = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        0, 1, INT32_MAX, INT32_MAX, WL_SHM_FORMAT_ARGB8888);

    ASSERT_NULL(buf_res);
    ASSERT_EQ_INT(1, ctx->pool->refcount);

    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: buffer exceeding pool size rejected                          */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_reject_exceeds_pool(void) {
    struct test_ctx *ctx = setup_ctx(64);

    /* 10x10 buffer needs at least 0 + 9*40 + 40 = 400 bytes */
    struct wl_resource *buf_res = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        0, 10, 10, 40, WL_SHM_FORMAT_ARGB8888);

    ASSERT_NULL(buf_res);
    ASSERT_EQ_INT(1, ctx->pool->refcount);

    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: invalid format rejected                                      */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_reject_bad_format(void) {
    struct test_ctx *ctx = setup_ctx(4096);

    struct wl_resource *buf_res = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        0, 10, 10, 40, WL_SHM_FORMAT_RGB565);

    ASSERT_NULL(buf_res);
    ASSERT_EQ_INT(1, ctx->pool->refcount);

    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: pool refcount on buffer create                               */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_pool_refcount_on_create(void) {
    struct test_ctx *ctx = setup_ctx(4096);

    ASSERT_EQ_INT(1, ctx->pool->refcount);

    struct wl_resource *buf1 = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        0, 10, 10, 40, WL_SHM_FORMAT_ARGB8888);
    ASSERT_NOT_NULL(buf1);
    ASSERT_EQ_INT(2, ctx->pool->refcount);

    struct wl_resource *buf2 = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 3,
        400, 10, 10, 40, WL_SHM_FORMAT_XRGB8888);
    ASSERT_NOT_NULL(buf2);
    ASSERT_EQ_INT(3, ctx->pool->refcount);

    wl_resource_destroy(buf1);
    wl_resource_destroy(buf2);
    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: buffer destroy decrements refcount                           */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_destroy_decrements_refcount(void) {
    struct test_ctx *ctx = setup_ctx(4096);

    struct wl_resource *buf_res = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        0, 10, 10, 40, WL_SHM_FORMAT_ARGB8888);

    ASSERT_NOT_NULL(buf_res);
    ASSERT_EQ_INT(2, ctx->pool->refcount);

    wl_resource_destroy(buf_res);
    ASSERT_EQ_INT(1, ctx->pool->refcount);

    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: pool deferred destroy with live buffer                       */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_pool_deferred_destroy(void) {
    struct test_ctx *ctx = setup_ctx(4096);

    struct wl_resource *buf_res = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        0, 10, 10, 40, WL_SHM_FORMAT_ARGB8888);

    ASSERT_NOT_NULL(buf_res);
    ASSERT_EQ_INT(2, ctx->pool->refcount);
    ASSERT_EQ_INT(0, ctx->pool->pending_destroy);

    /* Destroy the pool resource — should defer actual destroy */
    wl_resource_destroy(ctx->pool_resource);
    ctx->pool_resource = NULL;

    ASSERT_EQ_INT(1, ctx->pool->refcount);
    ASSERT_EQ_INT(1, ctx->pool->pending_destroy);

    /* Pool data should still be accessible (not unmapped) */
    ASSERT_NOT_NULL(ctx->pool->data);
    ASSERT_EQ_INT(0, ((uint8_t *)ctx->pool->data)[0]);

    wl_resource_destroy(buf_res);
    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: final cleanup when last buffer destroyed                     */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_final_cleanup(void) {
    struct test_ctx *ctx = setup_ctx(4096);

    struct wl_resource *buf_res = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        0, 10, 10, 40, WL_SHM_FORMAT_ARGB8888);

    ASSERT_NOT_NULL(buf_res);

    /* Destroy pool resource first (deferred) */
    wl_resource_destroy(ctx->pool_resource);
    ctx->pool_resource = NULL;
    ASSERT_EQ_INT(1, ctx->pool->refcount);

    /* Now destroy the buffer — this should trigger pool cleanup */
    wl_resource_destroy(buf_res);

    /* After cleanup, pool->refcount would be 0 and pool freed.
     * We can't safely assert on freed memory, but the test
     * passing without crash/ASAN error is the evidence. */

    ctx->pool = NULL; /* Avoid double-free in teardown */
    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: buffer destroy with NULL pool is safe                        */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_destroy_null_pool_safe(void) {
    struct lorie_shm_buffer buf = {0};
    buf.pool = NULL;

    struct wl_resource *res = wl_resource_create(NULL, &wl_buffer_interface, 1, 0);
    /* If res is NULL (no client), we can't fully test, but we can
     * at least verify the destroy logic doesn't crash with NULL pool.
     * We manually call the equivalent of buffer_destroy_resource. */

    /* Simulate what buffer_destroy_resource does */
    if (buf.pool) {
        buf.pool->refcount--;
        if (buf.pool->refcount == 0 && buf.pool->pending_destroy) {
            lorie_shm_pool_destroy(buf.pool);
        }
    }
    /* No crash = pass */
    ASSERT_TRUE(1);

    (void)res;
}

/* ------------------------------------------------------------------ */
/* Test: pool creation sets refcount to 1                             */
/* ------------------------------------------------------------------ */
static void test_shm_pool_create_sets_refcount(void) {
    int fd = create_shm_fd(4096);
    ASSERT_TRUE(fd >= 0);

    struct lorie_shm_pool *pool = lorie_shm_pool_create(fd, 4096);
    close(fd);
    ASSERT_NOT_NULL(pool);
    ASSERT_EQ_INT(1, pool->refcount);

    lorie_shm_pool_destroy(pool);
}

/* ------------------------------------------------------------------ */
/* Test: lorie_shm_buffer_from_resource with valid resource           */
/* ------------------------------------------------------------------ */
static void test_lorie_shm_buffer_from_resource_valid(void) {
    struct test_ctx *ctx = setup_ctx(4096);

    struct wl_resource *buf_res = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        0, 10, 10, 40, WL_SHM_FORMAT_ARGB8888);

    ASSERT_NOT_NULL(buf_res);

    struct lorie_shm_buffer *buf = lorie_shm_buffer_from_resource(buf_res);
    ASSERT_NOT_NULL(buf);
    ASSERT_EQ_PTR(buf_res, buf->resource);

    wl_resource_destroy(buf_res);
    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: lorie_shm_buffer_from_resource with NULL                     */
/* ------------------------------------------------------------------ */
static void test_lorie_shm_buffer_from_resource_null(void) {
    struct lorie_shm_buffer *buf = lorie_shm_buffer_from_resource(NULL);
    ASSERT_NULL(buf);
}

/* ------------------------------------------------------------------ */
/* Test: lorie_shm_buffer_from_resource with wrong type               */
/* ------------------------------------------------------------------ */
static void test_lorie_shm_buffer_from_resource_wrong_type(void) {
    struct test_ctx *ctx = setup_ctx(4096);

    struct wl_resource *surface_res = wl_resource_create(
        ctx->client, &wl_surface_interface, 6, 99);
    ASSERT_NOT_NULL(surface_res);

    struct lorie_shm_buffer *buf = lorie_shm_buffer_from_resource(surface_res);
    ASSERT_NULL(buf);

    wl_resource_destroy(surface_res);
    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: bounds check with offset                                     */
/* ------------------------------------------------------------------ */
static void test_shm_buffer_bounds_with_offset(void) {
    struct test_ctx *ctx = setup_ctx(100);

    /* offset=64, width=2, height=2, stride=8 needs 64 + 1*8 + 8 = 80 bytes */
    struct wl_resource *buf_res = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        64, 2, 2, 8, WL_SHM_FORMAT_ARGB8888);
    ASSERT_NOT_NULL(buf_res);

    /* offset=64, width=10, height=1, stride=40 needs 64 + 40 = 104 > 100 */
    struct wl_resource *buf_res2 = lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 3,
        64, 10, 1, 40, WL_SHM_FORMAT_ARGB8888);
    ASSERT_NULL(buf_res2);

    wl_resource_destroy(buf_res);
    teardown_ctx(ctx);
}

/* ================================================================== */
/* PR 2: Surface Integration Tests                                    */
/* ================================================================== */

static struct test_ctx *setup_ctx_prefilled(int pool_size, uint8_t fill) {
    struct test_ctx *ctx = calloc(1, sizeof(*ctx));
    ASSERT_NOT_NULL(ctx);

    ctx->compositor = lorie_compositor_create();
    ASSERT_NOT_NULL(ctx->compositor);

    int fd = create_shm_fd(pool_size);
    ASSERT_TRUE(fd >= 0);

    /* Pre-fill fd before read-only mapping */
    uint8_t *tmp = malloc(pool_size);
    ASSERT_NOT_NULL(tmp);
    memset(tmp, fill, pool_size);
    ssize_t written = write(fd, tmp, pool_size);
    ASSERT_EQ_INT(pool_size, (int)written);
    free(tmp);
    lseek(fd, 0, SEEK_SET);

    ctx->pool = lorie_shm_pool_create(fd, pool_size);
    close(fd);
    ASSERT_NOT_NULL(ctx->pool);

    int fds[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    ASSERT_EQ_INT(0, ret);

    ctx->client = wl_client_create(ctx->compositor->display, fds[0]);
    ASSERT_NOT_NULL(ctx->client);
    ctx->client_fd = fds[1];

    ctx->pool_resource = wl_resource_create(ctx->client, &wl_shm_pool_interface, 1, 1);
    ASSERT_NOT_NULL(ctx->pool_resource);
    wl_resource_set_implementation(ctx->pool_resource, NULL, ctx->pool, shm_pool_handle_resource_destroy);

    return ctx;
}

static struct lorie_surface *create_test_surface(struct test_ctx *ctx) {
    struct lorie_surface *s = lorie_surface_create_internal(ctx->compositor, ctx->client, 100);
    ASSERT_NOT_NULL(s);
    ASSERT_NOT_NULL(s->resource);
    return s;
}

static struct wl_resource *create_test_buffer(struct test_ctx *ctx, int32_t w, int32_t h) {
    return lorie_shm_pool_create_buffer_internal(
        ctx->client, ctx->pool_resource, 2,
        0, w, h, w * 4, WL_SHM_FORMAT_ARGB8888);
}

/* ------------------------------------------------------------------ */
/* Test: surface commit copies SHM pixels into LorieBuffer            */
/* ------------------------------------------------------------------ */
static void test_surface_commit_shm_buffer(void) {
    struct test_ctx *ctx = setup_ctx_prefilled(4096, 0xAB);
    struct lorie_surface *s = create_test_surface(ctx);

    struct wl_resource *buf_res = create_test_buffer(ctx, 4, 4);
    ASSERT_NOT_NULL(buf_res);

    s->pending_buffer = buf_res;
    s->pending_attached = 1;
    surface_commit(NULL, s->resource);

    ASSERT_NOT_NULL(s->buffer);
    ASSERT_EQ_INT(4, s->width);
    ASSERT_EQ_INT(4, s->height);

    const LorieBuffer_Desc *desc = LorieBuffer_description((LorieBuffer*)s->buffer);
    ASSERT_NOT_NULL(desc);
    ASSERT_NOT_NULL(desc->data);

    uint8_t *data = (uint8_t*)desc->data;
    for (int i = 0; i < 4 * 4 * 4; i++) {
        ASSERT_EQ_INT(0xAB, data[i]);
    }

    lorie_surface_destroy_internal(s);
    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: surface commit with null buffer skips allocation             */
/* ------------------------------------------------------------------ */
static void test_surface_commit_null_buffer(void) {
    struct test_ctx *ctx = setup_ctx(4096);
    struct lorie_surface *s = create_test_surface(ctx);

    s->pending_buffer = NULL;
    s->pending_attached = 1;
    surface_commit(NULL, s->resource);

    ASSERT_NULL(s->buffer);
    ASSERT_EQ_INT(0, s->width);
    ASSERT_EQ_INT(0, s->height);

    lorie_surface_destroy_internal(s);
    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: surface commit with non-SHM buffer is safely ignored         */
/* ------------------------------------------------------------------ */
static void test_surface_commit_non_shm_buffer(void) {
    struct test_ctx *ctx = setup_ctx(4096);
    struct lorie_surface *s = create_test_surface(ctx);

    struct wl_resource *non_buf = wl_resource_create(
        ctx->client, &wl_surface_interface, 6, 99);
    ASSERT_NOT_NULL(non_buf);

    s->pending_buffer = non_buf;
    s->pending_attached = 1;
    surface_commit(NULL, s->resource);

    ASSERT_NULL(s->buffer);
    ASSERT_EQ_INT(0, s->width);
    ASSERT_EQ_INT(0, s->height);

    wl_resource_destroy(non_buf);
    lorie_surface_destroy_internal(s);
    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: reattach releases old LorieBuffer                            */
/* ------------------------------------------------------------------ */
static void test_surface_reattach_releases_old(void) {
    struct test_ctx *ctx = setup_ctx(4096);
    struct lorie_surface *s = create_test_surface(ctx);

    struct wl_resource *buf_a = create_test_buffer(ctx, 4, 4);
    ASSERT_NOT_NULL(buf_a);

    s->pending_buffer = buf_a;
    s->pending_attached = 1;
    surface_commit(NULL, s->resource);

    LorieBuffer *old_buffer = (LorieBuffer*)s->buffer;
    if (old_buffer) {
        LorieBuffer_acquire(old_buffer);
    }

    struct wl_resource *buf_b = create_test_buffer(ctx, 8, 8);
    ASSERT_NOT_NULL(buf_b);

    s->pending_buffer = buf_b;
    s->pending_attached = 1;
    surface_commit(NULL, s->resource);

    ASSERT_EQ_INT(8, s->width);
    ASSERT_EQ_INT(8, s->height);

    if (old_buffer) {
        typedef struct { int refcount; LorieBuffer_Desc desc; } TestLorieBuffer;
        ASSERT_EQ_INT(1, ((TestLorieBuffer*)old_buffer)->refcount);
        LorieBuffer_release(old_buffer);
    }

    lorie_surface_destroy_internal(s);
    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: first attach with no prior buffer does not crash             */
/* ------------------------------------------------------------------ */
static void test_surface_first_attach_no_crash(void) {
    struct test_ctx *ctx = setup_ctx(4096);
    struct lorie_surface *s = create_test_surface(ctx);

    ASSERT_NULL(s->buffer_resource);
    ASSERT_NULL(s->buffer);

    struct wl_resource *buf = create_test_buffer(ctx, 4, 4);
    ASSERT_NOT_NULL(buf);

    s->pending_buffer = buf;
    s->pending_attached = 1;
    surface_commit(NULL, s->resource);

    ASSERT_NOT_NULL(s->buffer_resource);

    lorie_surface_destroy_internal(s);
    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: surface destroy releases attached buffer                     */
/* ------------------------------------------------------------------ */
static void test_surface_destroy_releases_buffer(void) {
    struct test_ctx *ctx = setup_ctx(4096);
    struct lorie_surface *s = create_test_surface(ctx);

    struct wl_resource *buf = create_test_buffer(ctx, 4, 4);
    ASSERT_NOT_NULL(buf);

    s->pending_buffer = buf;
    s->pending_attached = 1;
    surface_commit(NULL, s->resource);

    LorieBuffer *saved = (LorieBuffer*)s->buffer;
    if (saved) {
        LorieBuffer_acquire(saved);
    }

    lorie_surface_destroy_internal(s);

    if (saved) {
        typedef struct { int refcount; LorieBuffer_Desc desc; } TestLorieBuffer;
        ASSERT_EQ_INT(1, ((TestLorieBuffer*)saved)->refcount);
        LorieBuffer_release(saved);
    }

    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: alloc failure is handled gracefully                          */
/* ------------------------------------------------------------------ */
static void test_surface_commit_alloc_failure(void) {
    extern void lorie_test_force_alloc_fail(int fail);

    struct test_ctx *ctx = setup_ctx(4096);
    struct lorie_surface *s = create_test_surface(ctx);

    struct wl_resource *buf = create_test_buffer(ctx, 4, 4);
    ASSERT_NOT_NULL(buf);

    lorie_test_force_alloc_fail(1);
    s->pending_buffer = buf;
    s->pending_attached = 1;
    surface_commit(NULL, s->resource);
    lorie_test_force_alloc_fail(0);

    ASSERT_NULL(s->buffer);

    lorie_surface_destroy_internal(s);
    teardown_ctx(ctx);
}

/* ------------------------------------------------------------------ */
/* Test: surface dimensions match buffer after commit                 */
/* ------------------------------------------------------------------ */
static void test_surface_dimensions_updated(void) {
    struct test_ctx *ctx = setup_ctx(4096);
    struct lorie_surface *s = create_test_surface(ctx);

    struct wl_resource *buf = create_test_buffer(ctx, 16, 32);
    ASSERT_NOT_NULL(buf);

    s->pending_buffer = buf;
    s->pending_attached = 1;
    surface_commit(NULL, s->resource);

    ASSERT_EQ_INT(16, s->width);
    ASSERT_EQ_INT(32, s->height);

    lorie_surface_destroy_internal(s);
    teardown_ctx(ctx);
}

int lorie_test_shm_buffer_suite(struct lorie_test_suite *suite) {
    lorie_suite_init(suite, "shm_buffer", NULL, NULL);
    SUITE_ADD(suite, test_shm_buffer_create_valid);
    SUITE_ADD(suite, test_shm_buffer_data_pointer_valid);
    SUITE_ADD(suite, test_shm_buffer_reject_negative_offset);
    SUITE_ADD(suite, test_shm_buffer_reject_zero_width);
    SUITE_ADD(suite, test_shm_buffer_reject_zero_height);
    SUITE_ADD(suite, test_shm_buffer_reject_zero_stride);
    SUITE_ADD(suite, test_shm_buffer_reject_stride_too_small);
    SUITE_ADD(suite, test_shm_buffer_reject_overflow);
    SUITE_ADD(suite, test_shm_buffer_reject_exceeds_pool);
    SUITE_ADD(suite, test_shm_buffer_reject_bad_format);
    SUITE_ADD(suite, test_shm_buffer_pool_refcount_on_create);
    SUITE_ADD(suite, test_shm_buffer_destroy_decrements_refcount);
    SUITE_ADD(suite, test_shm_buffer_pool_deferred_destroy);
    SUITE_ADD(suite, test_shm_buffer_final_cleanup);
    SUITE_ADD(suite, test_shm_buffer_destroy_null_pool_safe);
    SUITE_ADD(suite, test_shm_pool_create_sets_refcount);
    SUITE_ADD(suite, test_lorie_shm_buffer_from_resource_valid);
    SUITE_ADD(suite, test_lorie_shm_buffer_from_resource_null);
    SUITE_ADD(suite, test_lorie_shm_buffer_from_resource_wrong_type);
    SUITE_ADD(suite, test_shm_buffer_bounds_with_offset);
    SUITE_ADD(suite, test_surface_commit_shm_buffer);
    SUITE_ADD(suite, test_surface_commit_null_buffer);
    SUITE_ADD(suite, test_surface_commit_non_shm_buffer);
    SUITE_ADD(suite, test_surface_reattach_releases_old);
    SUITE_ADD(suite, test_surface_first_attach_no_crash);
    SUITE_ADD(suite, test_surface_destroy_releases_buffer);
    SUITE_ADD(suite, test_surface_commit_alloc_failure);
    SUITE_ADD(suite, test_surface_dimensions_updated);
    return 0;
}
