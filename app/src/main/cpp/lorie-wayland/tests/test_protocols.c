/* Lorie Wayland Compositor — Protocol Tests (TDD)
 *
 * PR #6: xdg-shell + linux-dmabuf + wl_data_device
 */

#include "lorie_test.h"
#include "compositor.h"
#include <stdlib.h>

/* Generated protocol headers */
#include "stable-xdg-shell-xdg-shell.h"
#include "stable-linux-dmabuf-linux-dmabuf-v1.h"

#include <sys/socket.h>
#include <unistd.h>

static struct lorie_compositor *g_comp = NULL;

static void setup(void) {
    g_comp = lorie_compositor_create();
    ASSERT_NOT_NULL(g_comp);
}

static void teardown(void) {
    if (g_comp) {
        lorie_compositor_destroy(g_comp);
        g_comp = NULL;
    }
}

/* Test 1: xdg_wm_base global exists after compositor start */
static void test_xdg_shell_global_exists(void) {
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);
    ASSERT_NOT_NULL(g_comp->display);
    lorie_compositor_stop(g_comp);
}

/* Test 2: xdg_surface is NOT configured before first commit */
static void test_xdg_surface_not_configured_before_commit(void) {
    /* Create a surface without any xdg_shell involvement */
    struct lorie_surface *s = lorie_surface_create_internal(g_comp, NULL, 0);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ_PTR(NULL, s->xdg_surface);
    lorie_surface_destroy_internal(s);
}

/* Test 3: configure-on-commit sets configured flag and pending serial */
static void test_xdg_surface_configure_on_first_commit(void) {
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);

    int fds[2];
    ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    ASSERT_EQ_INT(0, ret);

    struct wl_client *client = wl_client_create(g_comp->display, fds[0]);
    ASSERT_NOT_NULL(client);

    struct lorie_surface *s = lorie_surface_create_internal(g_comp, client, 100);
    ASSERT_NOT_NULL(s);
    ASSERT_NOT_NULL(s->resource);

    struct lorie_xdg_surface *xdg = calloc(1, sizeof(*xdg));
    ASSERT_NOT_NULL(xdg);
    s->xdg_surface = xdg;
    xdg->surface = s;

    ASSERT_EQ_INT(0, xdg->configured);
    ASSERT_EQ_INT(0, xdg->pending_configure_serial);

    lorie_xdg_surface_handle_commit(s, client);

    ASSERT_EQ_INT(1, xdg->configured);
    ASSERT_TRUE(xdg->pending_configure_serial != 0);

    s->xdg_surface = NULL;
    free(xdg);
    lorie_surface_destroy_internal(s);
    wl_client_destroy(client);
    close(fds[1]);
}

/* Test 4: ack_configure internal helper clears pending on matching serial */
static void test_xdg_surface_ack_configure_valid(void) {
    struct lorie_xdg_surface *xdg = calloc(1, sizeof(*xdg));
    ASSERT_NOT_NULL(xdg);
    xdg->configured = 1;
    xdg->pending_configure_serial = 42;

    lorie_xdg_surface_ack_configure_internal(xdg, 42);
    ASSERT_EQ_INT(0, xdg->pending_configure_serial);

    free(xdg);
}

/* Test 5: ack_configure internal helper leaves pending on wrong serial */
static void test_xdg_surface_ack_configure_invalid(void) {
    struct lorie_xdg_surface *xdg = calloc(1, sizeof(*xdg));
    ASSERT_NOT_NULL(xdg);
    xdg->configured = 1;
    xdg->pending_configure_serial = 42;

    lorie_xdg_surface_ack_configure_internal(xdg, 99);
    ASSERT_EQ_INT(42, xdg->pending_configure_serial);

    free(xdg);
}

/* Test 6: ack_configure internal helper is no-op before configured */
static void test_xdg_surface_ack_configure_before_configured(void) {
    struct lorie_xdg_surface *xdg = calloc(1, sizeof(*xdg));
    ASSERT_NOT_NULL(xdg);
    xdg->configured = 0;

    lorie_xdg_surface_ack_configure_internal(xdg, 42);
    ASSERT_EQ_INT(0, xdg->pending_configure_serial);

    free(xdg);
}

/* Test 7: linux_dmabuf global exists */
static void test_dmabuf_global_exists(void) {
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);
    ASSERT_NOT_NULL(g_comp->display);
    lorie_compositor_stop(g_comp);
}

/* Test 8: wl_data_device_manager global exists */
static void test_data_device_manager_exists(void) {
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);
    ASSERT_NOT_NULL(g_comp->display);
    lorie_compositor_stop(g_comp);
}

int lorie_test_protocols_suite(struct lorie_test_suite *suite) {
    lorie_suite_init(suite, "protocols", setup, teardown);
    SUITE_ADD(suite, test_xdg_shell_global_exists);
    SUITE_ADD(suite, test_xdg_surface_not_configured_before_commit);
    SUITE_ADD(suite, test_xdg_surface_configure_on_first_commit);
    SUITE_ADD(suite, test_xdg_surface_ack_configure_valid);
    SUITE_ADD(suite, test_xdg_surface_ack_configure_invalid);
    SUITE_ADD(suite, test_xdg_surface_ack_configure_before_configured);
    SUITE_ADD(suite, test_dmabuf_global_exists);
    SUITE_ADD(suite, test_data_device_manager_exists);
    return 0;
}
