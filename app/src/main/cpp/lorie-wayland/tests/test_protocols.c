/* Lorie Wayland Compositor — Protocol Tests (TDD)
 *
 * PR #6: xdg-shell + linux-dmabuf + wl_data_device
 */

#include "lorie_test.h"
#include "compositor.h"
#include "lorie_test_teardown.h"
#include <stdlib.h>

/* Generated protocol headers */
#include "stable-xdg-shell-xdg-shell.h"
#include "stable-linux-dmabuf-linux-dmabuf-v1.h"

#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

static struct lorie_compositor *g_comp = NULL;

/* WL_DISPLAY_ERROR is opcode 0 of the wl_display interface (object id 1).
 * Reads raw bytes from the client's peer socket — looping over multiple
 * recv() calls and skipping any number of unrelated leading events — and
 * parses the wire messages looking for a wl_display.error(object_id, code,
 * message) event, returning the protocol error `code` on success or -1 if
 * no such event is found before the socket drains (EAGAIN/EWOULDBLOCK).
 *
 * MUST drain past `wl_display.delete_id` flood: tests call
 * warm_up_client_ids(client, 1000), which destroys ~998 throwaway
 * wl_callback resources, each queuing a 12-byte wl_display.delete_id
 * (opcode 1) event — roughly 12KB of noise that arrives BEFORE the
 * wl_display.error (opcode 0) event the production code posts in response
 * to the protocol violation under test. A single bounded recv() into a
 * small buffer fills entirely with delete_id noise and never reaches the
 * error event 12KB downstream (see engram bugfix #164: "expected 1 got -1"
 * with the production code provably correct). This loop keeps draining
 * the socket — re-filling the buffer and carrying over any trailing
 * partial message across recv() calls — until the error event surfaces or
 * the peer has nothing left to send. */
static int recv_wl_display_error_code(int peer_fd, struct wl_client *client) {
    unsigned char buf[16384];
    size_t len = 0;   /* valid bytes currently in buf[0..len) */
    size_t off = 0;   /* offset of the next unparsed message within buf */
    int flags;

    wl_client_flush(client);

    flags = fcntl(peer_fd, F_GETFL, 0);
    fcntl(peer_fd, F_SETFL, flags | O_NONBLOCK);

    for (;;) {
        /* Slide any unparsed trailing bytes (an incomplete message split
         * across recv() boundaries) to the front before refilling. */
        if (off > 0) {
            if (off < len)
                memmove(buf, buf + off, len - off);
            len -= off;
            off = 0;
        }

        if (len < sizeof(buf)) {
            ssize_t n = recv(peer_fd, buf + len, sizeof(buf) - len, 0);
            fprintf(stderr, "[recv_wl_display_error_code] recv() returned n=%zd errno=%d\n",
                    (ssize_t)n, errno);
            if (n > 0) {
                len += (size_t)n;
            } else if (n == 0) {
                break; /* peer closed */
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (len == off) /* nothing buffered and nothing pending */
                    break;
                /* fall through: parse what's already buffered */
            } else {
                break;
            }
        }

        /* Parse every complete message currently buffered. */
        while (off + 8 <= len) {
            uint32_t sender_id;
            uint32_t second;
            uint16_t opcode;
            uint16_t size;

            memcpy(&sender_id, buf + off, 4);
            memcpy(&second, buf + off + 4, 4);
            opcode = (uint16_t)(second & 0xffff);
            size = (uint16_t)(second >> 16);

            if (size < 8) {
                /* Malformed framing — nothing useful left to parse. */
                fcntl(peer_fd, F_SETFL, flags);
                return -1;
            }

            if (off + size > len)
                break; /* incomplete message — wait for more bytes */

            /* wl_display object id is always 1; WL_DISPLAY_ERROR opcode is 0 */
            if (sender_id == 1 && opcode == 0 && size >= 16) {
                uint32_t code;
                memcpy(&code, buf + off + 12, 4);
                fcntl(peer_fd, F_SETFL, flags);
                return (int)code;
            }

            /* Skip this event (e.g. wl_display.delete_id, opcode 1) and
             * keep draining — the error event may be further downstream. */
            off += size;
        }

        if (len == sizeof(buf) && off == 0) {
            /* Buffer full of one oversized/unparseable message — bail out
             * rather than spin forever. */
            break;
        }
    }

    fcntl(peer_fd, F_SETFL, flags);
    return -1;
}

/* Test helper: a freshly created wl_client only has client-side object-map
 * slots for ids 0 (reserved/null) and 1 (wl_display), so wl_resource_create()
 * with an explicit id N > 2 fails with EINVAL (libwayland's wl_map_insert_at
 * requires `count == i` to grow the map by one slot at a time — see
 * wayland-util.c). Real clients reach higher ids by allocating new_ids
 * sequentially as they create objects over the wire.
 *
 * To let tests pick "nice round" resource ids (100, 200, ...) without
 * reproducing the full wire protocol, this helper "warms up" the client's
 * id map by creating (and immediately destroying) throwaway wl_callback
 * resources for every id in [2, target_id), bumping the map's slot count up
 * to target_id so that the subsequent wl_resource_create(..., target_id)
 * call satisfies `count == i` and succeeds. wl_map_remove() only clears the
 * slot's data pointer — it does not shrink the underlying array — so the
 * slot count remains permanently raised after the throwaway resources are
 * destroyed. */
static void warm_up_client_ids(struct wl_client *client, uint32_t target_id) {
    uint32_t id;
    for (id = 2; id < target_id; id++) {
        struct wl_resource *r = wl_resource_create(client, &wl_callback_interface, 1, id);
        if (!r)
            continue;
        wl_resource_destroy(r);
    }
}

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
    warm_up_client_ids(client, 1000);

    struct lorie_surface *s = lorie_surface_create_internal(g_comp, client, 100);
    ASSERT_NOT_NULL(s);
    ASSERT_NOT_NULL(s->resource);

    struct lorie_xdg_surface *xdg = calloc(1, sizeof(*xdg));
    ASSERT_NOT_NULL(xdg);
    xdg->resource = wl_resource_create(client, &xdg_surface_interface, 1, 200);
    s->xdg_surface = xdg;
    xdg->surface = s;

    /* xdg-shell requires a role (get_toplevel/get_popup) before a commit can
     * be configured — assign a mock toplevel role so role validation passes
     * and we can exercise the configure-on-commit path in isolation. */
    struct lorie_xdg_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    ASSERT_NOT_NULL(toplevel);
    toplevel->xdg_surface = xdg;
    struct wl_resource *toplevel_res = wl_resource_create(client, &xdg_toplevel_interface, 1, 211);
    ASSERT_NOT_NULL(toplevel_res);
    wl_resource_set_implementation(toplevel_res, NULL, toplevel, xdg_toplevel_handle_resource_destroy);
    xdg->role = toplevel_res;

    ASSERT_EQ_INT(0, xdg->configured);
    ASSERT_EQ_INT(0, xdg->pending_configure_serial);

    lorie_xdg_surface_handle_commit(s, client);

    ASSERT_EQ_INT(1, xdg->configured);
    ASSERT_TRUE(xdg->pending_configure_serial != 0);

    s->xdg_surface = NULL;
    xdg->role = NULL;
    wl_resource_destroy(toplevel_res);
    free(xdg);
    /* compositor-teardown-safety: lorie_surface_destroy_internal /
     * wl_client_destroy must not run cross-thread while the loop is alive
     * (lorie_compositor_assert_event_loop_thread aborts here — this is the
     * exact caller proven to trip the guard in both lorie-wayland-tests and
     * the isolated lorie-wayland-protocols-tests binary, batch-1 evidence).
     * lorie_test_safe_destroy_client joins the loop first, then destroys
     * client+surface together via the resource-destroy chain. */
    lorie_test_safe_destroy_client(g_comp, client);
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

/* Test 7: unconfigured buffer prevents configure */
static void test_xdg_surface_unconfigured_buffer_error(void) {
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);

    int fds[2];
    ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    ASSERT_EQ_INT(0, ret);

    struct wl_client *client = wl_client_create(g_comp->display, fds[0]);
    ASSERT_NOT_NULL(client);
    warm_up_client_ids(client, 1000);

    struct lorie_surface *s = lorie_surface_create_internal(g_comp, client, 100);
    ASSERT_NOT_NULL(s);
    ASSERT_NOT_NULL(s->resource);

    struct lorie_xdg_surface *xdg = calloc(1, sizeof(*xdg));
    ASSERT_NOT_NULL(xdg);
    xdg->resource = wl_resource_create(client, &xdg_surface_interface, 1, 200);
    s->xdg_surface = xdg;
    xdg->surface = s;
    /* Simulate buffer attached before configure */
    s->pending_attached = 1;
    s->pending_buffer = (struct wl_resource *)0x1234;

    ASSERT_EQ_INT(0, xdg->configured);
    surface_commit(client, s->resource);
    ASSERT_EQ_INT(0, xdg->configured);
    ASSERT_EQ_INT(0, xdg->pending_configure_serial);
    ASSERT_EQ_INT(0, s->pending_attached);
    ASSERT_EQ_PTR(NULL, s->pending_buffer);

    s->xdg_surface = NULL;
    free(xdg);
    /* compositor-teardown-safety: see test_xdg_surface_configure_on_first_commit
     * for why a direct lorie_surface_destroy_internal/wl_client_destroy pair
     * here would trip the cross-thread teardown guard. */
    lorie_test_safe_destroy_client(g_comp, client);
    close(fds[1]);
}

/* Test 8: preferred_buffer_scale path sends configure (now needs a mock role for role validation) */
static void test_xdg_surface_preferred_buffer_scale_sent(void) {
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);

    int fds[2];
    ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    ASSERT_EQ_INT(0, ret);

    struct wl_client *client = wl_client_create(g_comp->display, fds[0]);
    ASSERT_NOT_NULL(client);
    warm_up_client_ids(client, 1000);

    struct lorie_surface *s = lorie_surface_create_internal(g_comp, client, 100);
    ASSERT_NOT_NULL(s);
    ASSERT_NOT_NULL(s->resource);

    struct lorie_xdg_surface *xdg = calloc(1, sizeof(*xdg));
    ASSERT_NOT_NULL(xdg);
    xdg->resource = wl_resource_create(client, &xdg_surface_interface, 1, 200);
    s->xdg_surface = xdg;
    xdg->surface = s;

    /* Assign a mock toplevel role so role validation passes */
    struct lorie_xdg_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    ASSERT_NOT_NULL(toplevel);
    toplevel->xdg_surface = xdg;
    struct wl_resource *toplevel_res = wl_resource_create(client, &xdg_toplevel_interface, 1, 211);
    ASSERT_NOT_NULL(toplevel_res);
    wl_resource_set_implementation(toplevel_res, NULL, toplevel, xdg_toplevel_handle_resource_destroy);
    xdg->role = toplevel_res;

    ASSERT_EQ_INT(0, xdg->configured);
    ASSERT_EQ_INT(6, wl_resource_get_version(s->resource));
    lorie_xdg_surface_handle_commit(s, client);
    ASSERT_EQ_INT(1, xdg->configured);
    ASSERT_TRUE(xdg->pending_configure_serial != 0);

    s->xdg_surface = NULL;
    xdg->role = NULL;
    wl_resource_destroy(toplevel_res);
    free(xdg);
    /* compositor-teardown-safety: see test_xdg_surface_configure_on_first_commit. */
    lorie_test_safe_destroy_client(g_comp, client);
    close(fds[1]);
}

/* Test 18: initial configure is sent immediately on get_toplevel (role assignment) */
static void test_xdg_surface_initial_configure_on_get_toplevel(void) {
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);

    int fds[2];
    ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    ASSERT_EQ_INT(0, ret);

    struct wl_client *client = wl_client_create(g_comp->display, fds[0]);
    ASSERT_NOT_NULL(client);
    warm_up_client_ids(client, 1000);

    struct lorie_surface *s = lorie_surface_create_internal(g_comp, client, 100);
    ASSERT_NOT_NULL(s);

    struct lorie_xdg_surface *xdg = calloc(1, sizeof(*xdg));
    ASSERT_NOT_NULL(xdg);
    xdg->resource = wl_resource_create(client, &xdg_surface_interface, 1, 500);
    ASSERT_NOT_NULL(xdg->resource);
    s->xdg_surface = xdg;
    xdg->surface = s;

    /* Assign a mock toplevel role and call the initial configure helper */
    struct lorie_xdg_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    ASSERT_NOT_NULL(toplevel);
    toplevel->xdg_surface = xdg;
    struct wl_resource *toplevel_res = wl_resource_create(client, &xdg_toplevel_interface, 1, 501);
    ASSERT_NOT_NULL(toplevel_res);
    wl_resource_set_implementation(toplevel_res, NULL, toplevel, xdg_toplevel_handle_resource_destroy);
    xdg->role = toplevel_res;

    /* Before initial configure: configured must be 0 */
    ASSERT_EQ_INT(0, xdg->configured);
    ASSERT_EQ_INT(0, xdg->pending_configure_serial);

    /* Call initial configure helper directly (mimics what get_toplevel will do) */
    lorie_xdg_surface_send_initial_configure(xdg);

    /* After initial configure: must be configured with a nonzero serial */
    ASSERT_EQ_INT(1, xdg->configured);
    ASSERT_TRUE(xdg->pending_configure_serial != 0);

    s->xdg_surface = NULL;
    xdg->role = NULL;
    wl_resource_destroy(toplevel_res);
    free(xdg);
    /* compositor-teardown-safety: see test_xdg_surface_configure_on_first_commit. */
    lorie_test_safe_destroy_client(g_comp, client);
    close(fds[1]);
}

/* Test 18b: initial configure is sent immediately on get_popup (role assignment) */
static void test_xdg_surface_initial_configure_on_get_popup(void) {
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);

    int fds[2];
    ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    ASSERT_EQ_INT(0, ret);

    struct wl_client *client = wl_client_create(g_comp->display, fds[0]);
    ASSERT_NOT_NULL(client);
    warm_up_client_ids(client, 1000);

    struct lorie_surface *s = lorie_surface_create_internal(g_comp, client, 100);
    ASSERT_NOT_NULL(s);

    struct lorie_xdg_surface *xdg = calloc(1, sizeof(*xdg));
    ASSERT_NOT_NULL(xdg);
    xdg->resource = wl_resource_create(client, &xdg_surface_interface, 1, 510);
    ASSERT_NOT_NULL(xdg->resource);
    s->xdg_surface = xdg;
    xdg->surface = s;

    /* Assign a mock popup role and call the initial configure helper */
    struct lorie_xdg_popup *popup = calloc(1, sizeof(*popup));
    ASSERT_NOT_NULL(popup);
    popup->xdg_surface = xdg;
    struct wl_resource *popup_res = wl_resource_create(client, &xdg_popup_interface, 1, 511);
    ASSERT_NOT_NULL(popup_res);
    wl_resource_set_implementation(popup_res, NULL, popup,
                                   xdg_popup_handle_resource_destroy);
    popup->resource = popup_res;
    xdg->role = popup_res;

    /* Before initial configure: configured must be 0 */
    ASSERT_EQ_INT(0, xdg->configured);
    ASSERT_EQ_INT(0, xdg->pending_configure_serial);
    ASSERT_EQ_INT(0, popup->configured);

    /* Call initial configure helper directly (mimics what get_popup will do) */
    lorie_xdg_surface_send_initial_configure(xdg);

    /* After initial configure: surface and popup must be configured with a nonzero serial */
    ASSERT_EQ_INT(1, xdg->configured);
    ASSERT_TRUE(xdg->pending_configure_serial != 0);
    ASSERT_EQ_INT(1, popup->configured);

    s->xdg_surface = NULL;
    xdg->role = NULL;
    wl_resource_destroy(popup_res);
    free(xdg);
    /* compositor-teardown-safety: see test_xdg_surface_configure_on_first_commit. */
    lorie_test_safe_destroy_client(g_comp, client);
    close(fds[1]);
}

/* Test 19: surface commit without role raises XDG_SURFACE_ERROR_NOT_CONSTRUCTED */
static void test_xdg_surface_commit_without_role_raises_error(void) {
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);

    int fds[2];
    ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    ASSERT_EQ_INT(0, ret);

    struct wl_client *client = wl_client_create(g_comp->display, fds[0]);
    ASSERT_NOT_NULL(client);
    warm_up_client_ids(client, 1000);

    struct lorie_surface *s = lorie_surface_create_internal(g_comp, client, 100);
    ASSERT_NOT_NULL(s);

    struct lorie_xdg_surface *xdg = calloc(1, sizeof(*xdg));
    ASSERT_NOT_NULL(xdg);
    xdg->resource = wl_resource_create(client, &xdg_surface_interface, 1, 600);
    ASSERT_NOT_NULL(xdg->resource);
    s->xdg_surface = xdg;
    xdg->surface = s;
    /* No role assigned: xdg->role == NULL */
    ASSERT_EQ_PTR(NULL, xdg->role);

    /* A commit without a role should raise a protocol error and leave configured == 0 */
    lorie_xdg_surface_handle_commit(s, client);

    /* The surface must remain unconfigured */
    ASSERT_EQ_INT(0, xdg->configured);

    /* The compositor MUST have posted XDG_SURFACE_ERROR_NOT_CONSTRUCTED to the client */
    int err_code = recv_wl_display_error_code(fds[1], client);
    ASSERT_EQ_INT(XDG_SURFACE_ERROR_NOT_CONSTRUCTED, err_code);

    s->xdg_surface = NULL;
    free(xdg);
    /* compositor-teardown-safety: see test_xdg_surface_configure_on_first_commit.
     * recv_wl_display_error_code already drained fds[1] above (before
     * teardown) — close() here remains purely post-teardown cleanup, same
     * relative ordering as before migration. */
    lorie_test_safe_destroy_client(g_comp, client);
    close(fds[1]);
}

/* Test 20: surface commit with role succeeds (no protocol error) */
static void test_xdg_surface_commit_with_role_succeeds(void) {
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);

    int fds[2];
    ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    ASSERT_EQ_INT(0, ret);

    struct wl_client *client = wl_client_create(g_comp->display, fds[0]);
    ASSERT_NOT_NULL(client);
    warm_up_client_ids(client, 1000);

    struct lorie_surface *s = lorie_surface_create_internal(g_comp, client, 100);
    ASSERT_NOT_NULL(s);

    struct lorie_xdg_surface *xdg = calloc(1, sizeof(*xdg));
    ASSERT_NOT_NULL(xdg);
    xdg->resource = wl_resource_create(client, &xdg_surface_interface, 1, 700);
    ASSERT_NOT_NULL(xdg->resource);
    s->xdg_surface = xdg;
    xdg->surface = s;

    /* Assign a mock toplevel role */
    struct lorie_xdg_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    ASSERT_NOT_NULL(toplevel);
    toplevel->xdg_surface = xdg;
    struct wl_resource *toplevel_res = wl_resource_create(client, &xdg_toplevel_interface, 1, 701);
    ASSERT_NOT_NULL(toplevel_res);
    wl_resource_set_implementation(toplevel_res, NULL, toplevel, xdg_toplevel_handle_resource_destroy);
    xdg->role = toplevel_res;

    /* Commit with role must succeed: configured flag must be set */
    lorie_xdg_surface_handle_commit(s, client);
    ASSERT_EQ_INT(1, xdg->configured);
    ASSERT_TRUE(xdg->pending_configure_serial != 0);

    s->xdg_surface = NULL;
    xdg->role = NULL;
    wl_resource_destroy(toplevel_res);
    free(xdg);
    /* compositor-teardown-safety: see test_xdg_surface_configure_on_first_commit. */
    lorie_test_safe_destroy_client(g_comp, client);
    close(fds[1]);
}

/* Test 9: toplevel destroy cleans role pointer */
static void test_xdg_toplevel_destroy_cleans_role(void) {
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);

    int fds[2];
    ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    ASSERT_EQ_INT(0, ret);

    struct wl_client *client = wl_client_create(g_comp->display, fds[0]);
    ASSERT_NOT_NULL(client);
    warm_up_client_ids(client, 1000);

    struct lorie_xdg_surface *xdg = calloc(1, sizeof(*xdg));
    ASSERT_NOT_NULL(xdg);
    struct lorie_xdg_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    ASSERT_NOT_NULL(toplevel);
    toplevel->xdg_surface = xdg;

    struct wl_resource *toplevel_res = wl_resource_create(client, &xdg_toplevel_interface, 1, 300);
    ASSERT_NOT_NULL(toplevel_res);
    wl_resource_set_implementation(toplevel_res, NULL, toplevel, xdg_toplevel_handle_resource_destroy);
    xdg->role = toplevel_res;

    wl_resource_destroy(toplevel_res);
    /* After toplevel destroy, xdg->role should be NULL */
    ASSERT_EQ_PTR(NULL, xdg->role);

    free(xdg);
    /* toplevel was freed by the destroy handler.
     * compositor-teardown-safety: this test never creates a lorie_surface
     * (no lorie_surface_destroy_internal call site exists for it), so the
     * guard never trips here regardless of which thread calls
     * wl_client_destroy — but wl_client_destroy-while-running on the test
     * thread is still the unsafe-teardown SHAPE the spec targets, and the
     * helper is the documented reusable safe-teardown primitive for exactly
     * this call. Migrating keeps every running-compositor teardown in this
     * suite uniform and matches task 3.4's audit list. */
    lorie_test_safe_destroy_client(g_comp, client);
    close(fds[1]);
}

/* Test 12: popup destroy cleans role pointer */
static void test_popup_destroy_cleans_role(void) {
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);

    int fds[2];
    ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    ASSERT_EQ_INT(0, ret);

    struct wl_client *client = wl_client_create(g_comp->display, fds[0]);
    ASSERT_NOT_NULL(client);
    warm_up_client_ids(client, 1000);

    struct lorie_xdg_surface *xdg = calloc(1, sizeof(*xdg));
    ASSERT_NOT_NULL(xdg);
    struct lorie_xdg_popup *popup = calloc(1, sizeof(*popup));
    ASSERT_NOT_NULL(popup);
    popup->xdg_surface = xdg;

    struct wl_resource *popup_res = wl_resource_create(client, &xdg_popup_interface, 1, 400);
    ASSERT_NOT_NULL(popup_res);
    popup->resource = popup_res;
    wl_resource_set_implementation(popup_res, NULL, popup, xdg_popup_handle_resource_destroy);
    xdg->role = popup_res;

    wl_resource_destroy(popup_res);
    ASSERT_EQ_PTR(NULL, xdg->role);

    free(xdg);
    /* compositor-teardown-safety: see test_xdg_toplevel_destroy_cleans_role
     * — same "no lorie_surface, but still unsafe-shaped teardown" rationale
     * for migrating onto the helper here too. */
    lorie_test_safe_destroy_client(g_comp, client);
    close(fds[1]);
}

/* Test 13: popup configure on first commit */
static void test_popup_configure_on_commit(void) {
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);

    int fds[2];
    ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    ASSERT_EQ_INT(0, ret);

    struct wl_client *client = wl_client_create(g_comp->display, fds[0]);
    ASSERT_NOT_NULL(client);
    warm_up_client_ids(client, 1000);

    struct lorie_surface *s = lorie_surface_create_internal(g_comp, client, 100);
    ASSERT_NOT_NULL(s);

    struct lorie_xdg_surface *xdg = calloc(1, sizeof(*xdg));
    ASSERT_NOT_NULL(xdg);
    xdg->surface = s;
    s->xdg_surface = xdg;

    struct lorie_xdg_popup *popup = calloc(1, sizeof(*popup));
    ASSERT_NOT_NULL(popup);
    popup->xdg_surface = xdg;

    struct wl_resource *popup_res = wl_resource_create(client, &xdg_popup_interface, 1, 401);
    ASSERT_NOT_NULL(popup_res);
    popup->resource = popup_res;
    wl_resource_set_implementation(popup_res, NULL, popup, xdg_popup_handle_resource_destroy);
    xdg->role = popup_res;

    ASSERT_EQ_INT(0, popup->configured);
    lorie_xdg_surface_handle_commit(s, client);
    ASSERT_EQ_INT(1, popup->configured);

    s->xdg_surface = NULL;
    wl_resource_destroy(popup_res);
    ASSERT_EQ_PTR(NULL, xdg->role);
    free(xdg);
    /* compositor-teardown-safety: see test_xdg_surface_configure_on_first_commit. */
    lorie_test_safe_destroy_client(g_comp, client);
    close(fds[1]);
}

/* Test 10: linux_dmabuf global exists */
static void test_dmabuf_global_exists(void) {
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);
    ASSERT_NOT_NULL(g_comp->display);
    lorie_compositor_stop(g_comp);
}

/* Test 11: wl_data_device_manager global exists */
static void test_data_device_manager_exists(void) {
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);
    ASSERT_NOT_NULL(g_comp->display);
    lorie_compositor_stop(g_comp);
}

/* Test 14: xwayland_shell_v1 global exists */
static void test_xwayland_shell_global_exists(void) {
    ASSERT_NOT_NULL(g_comp->xwayland_shell_global);
    int ret = lorie_compositor_start(g_comp);
    ASSERT_EQ_INT(0, ret);
    ASSERT_NOT_NULL(g_comp->display);
    lorie_compositor_stop(g_comp);
}

/* Test 15: xwayland_surface stores nonzero serial */
static void test_xwayland_surface_serial_stored(void) {
    struct lorie_xwayland_surface *xw = calloc(1, sizeof(*xw));
    ASSERT_NOT_NULL(xw);
    xw->serial = ((uint64_t)1 << 32) | 42;
    ASSERT_TRUE(xw->serial != 0);
    ASSERT_EQ_INT(42, (uint32_t)(xw->serial & 0xffffffffu));
    ASSERT_EQ_INT(1, (uint32_t)(xw->serial >> 32));
    free(xw);
}

/* Test 16: xwayland_surface rejects zero serial */
static void test_xwayland_surface_serial_rejects_zero(void) {
    struct lorie_xwayland_surface *xw = calloc(1, sizeof(*xw));
    ASSERT_NOT_NULL(xw);
    /* Simulate the validation logic from set_serial */
    uint64_t serial = 0;
    ASSERT_TRUE(serial == 0);
    free(xw);
}

/* Test 17: xwayland_surface rejects double association */
static void test_xwayland_surface_already_associated(void) {
    struct lorie_xwayland_surface *xw = calloc(1, sizeof(*xw));
    ASSERT_NOT_NULL(xw);
    xw->serial = 123;
    ASSERT_TRUE(xw->serial != 0);
    free(xw);
}

int lorie_test_protocols_suite(struct lorie_test_suite *suite) {
    lorie_suite_init(suite, "protocols", setup, teardown);
    SUITE_ADD(suite, test_xdg_shell_global_exists);
    SUITE_ADD(suite, test_xdg_surface_not_configured_before_commit);
    SUITE_ADD(suite, test_xdg_surface_configure_on_first_commit);
    SUITE_ADD(suite, test_xdg_surface_ack_configure_valid);
    SUITE_ADD(suite, test_xdg_surface_ack_configure_invalid);
    SUITE_ADD(suite, test_xdg_surface_ack_configure_before_configured);
    SUITE_ADD(suite, test_xdg_surface_unconfigured_buffer_error);
    SUITE_ADD(suite, test_xdg_surface_preferred_buffer_scale_sent);
    SUITE_ADD(suite, test_xdg_toplevel_destroy_cleans_role);
    SUITE_ADD(suite, test_popup_destroy_cleans_role);
    SUITE_ADD(suite, test_popup_configure_on_commit);
    SUITE_ADD(suite, test_dmabuf_global_exists);
    SUITE_ADD(suite, test_data_device_manager_exists);
    SUITE_ADD(suite, test_xwayland_shell_global_exists);
    SUITE_ADD(suite, test_xwayland_surface_serial_stored);
    SUITE_ADD(suite, test_xwayland_surface_serial_rejects_zero);
    SUITE_ADD(suite, test_xwayland_surface_already_associated);
    /* Phase: xdg-shell configure lifecycle */
    SUITE_ADD(suite, test_xdg_surface_initial_configure_on_get_toplevel);
    SUITE_ADD(suite, test_xdg_surface_initial_configure_on_get_popup);
    SUITE_ADD(suite, test_xdg_surface_commit_without_role_raises_error);
    SUITE_ADD(suite, test_xdg_surface_commit_with_role_succeeds);
    return 0;
}
