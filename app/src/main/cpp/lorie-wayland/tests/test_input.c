#include "lorie_test.h"
#include "compositor.h"
#include "input.h"
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

static struct lorie_compositor *g_comp = NULL;

static void setup(void) {
    g_comp = lorie_compositor_create();
    ASSERT_NOT_NULL(g_comp);
}

static void teardown(void) {
    if (g_comp) { lorie_compositor_destroy(g_comp); g_comp = NULL; }
}

static struct lorie_surface *make_surface(struct wl_client *client, int32_t x, int32_t y, int32_t w, int32_t h) {
    struct lorie_surface *s = lorie_surface_create_internal(g_comp, client, 0);
    if (s) { s->x = x; s->y = y; s->logical_width = w; s->logical_height = h; }
    return s;
}

static struct wl_client *make_client(void) {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0) return NULL;
    struct wl_client *c = wl_client_create(g_comp->display, fds[0]);
    close(fds[1]);
    return c;
}

static void test_input_init_creates_seat(void) {
    struct lorie_input *in = lorie_input_init(g_comp);
    ASSERT_NOT_NULL(in);
    ASSERT_EQ_PTR(g_comp, in->compositor);
    lorie_input_destroy(in);
}

static void test_pointer_focus_set_on_motion(void) {
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    struct lorie_input *in = lorie_input_init(g_comp);
    struct wl_client *client = make_client();
    ASSERT_NOT_NULL(client);
    struct lorie_surface *s = make_surface(client, 10, 20, 200, 100);
    ASSERT_NOT_NULL(s);
    ASSERT_NULL(in->pointer_focus);
    lorie_input_pointer_motion(in, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    ASSERT_EQ_PTR(s, in->pointer_focus);
    lorie_input_destroy(in);
    lorie_surface_destroy_internal(s);
    wl_client_destroy(client);
}

static void test_pointer_focus_cleared_on_leave(void) {
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    struct lorie_input *in = lorie_input_init(g_comp);
    struct wl_client *client = make_client();
    ASSERT_NOT_NULL(client);
    struct lorie_surface *s = make_surface(client, 10, 20, 200, 100);
    ASSERT_NOT_NULL(s);
    lorie_input_pointer_motion(in, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    ASSERT_EQ_PTR(s, in->pointer_focus);
    lorie_input_pointer_motion(in, 500.0f, 500.0f);
    lorie_input_dispatch(in);
    ASSERT_NULL(in->pointer_focus);
    lorie_input_destroy(in);
    lorie_surface_destroy_internal(s);
    wl_client_destroy(client);
}

static void test_keyboard_follows_pointer_focus(void) {
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    struct lorie_input *in = lorie_input_init(g_comp);
    struct wl_client *client = make_client();
    ASSERT_NOT_NULL(client);
    struct lorie_surface *s = make_surface(client, 0, 0, 100, 100);
    ASSERT_NOT_NULL(s);
    ASSERT_NULL(in->keyboard_focus);
    lorie_input_pointer_motion(in, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    ASSERT_EQ_PTR(s, in->keyboard_focus);
    lorie_input_pointer_motion(in, 200.0f, 200.0f);
    lorie_input_dispatch(in);
    ASSERT_NULL(in->keyboard_focus);
    lorie_input_destroy(in);
    lorie_surface_destroy_internal(s);
    wl_client_destroy(client);
}

static void test_focus_cleared_on_surface_destroy(void) {
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    struct lorie_input *in = g_comp->input;
    struct wl_client *client = make_client();
    ASSERT_NOT_NULL(client);
    struct lorie_surface *s = make_surface(client, 0, 0, 100, 100);
    ASSERT_NOT_NULL(s);
    lorie_input_pointer_motion(in, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    ASSERT_EQ_PTR(s, in->pointer_focus);
    ASSERT_EQ_PTR(s, in->keyboard_focus);
    lorie_surface_destroy_internal(s);
    ASSERT_NULL(in->pointer_focus); ASSERT_NULL(in->keyboard_focus);
    wl_client_destroy(client);
}

static void test_touch_focus_set_on_down(void) {
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    struct lorie_input *in = lorie_input_init(g_comp);
    struct wl_client *client = make_client();
    ASSERT_NOT_NULL(client);
    struct lorie_surface *s = make_surface(client, 0, 0, 100, 100);
    ASSERT_NOT_NULL(s);
    ASSERT_NULL(in->touch_focus);
    lorie_input_touch_down(in, 0, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    ASSERT_EQ_PTR(s, in->touch_focus);
    lorie_input_destroy(in);
    lorie_surface_destroy_internal(s);
    wl_client_destroy(client);
}

static void test_no_crash_when_no_focus(void) {
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    struct lorie_input *in = lorie_input_init(g_comp);
    lorie_input_pointer_motion(in, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    ASSERT_NULL(in->pointer_focus);
    lorie_input_pointer_button(in, 0x110, 1);
    lorie_input_dispatch(in);
    lorie_input_keyboard_key(in, 29, 1);
    lorie_input_dispatch(in);
    lorie_input_touch_down(in, 0, 50.0f, 50.0f);
    lorie_input_dispatch(in);
    lorie_input_destroy(in);
}

int lorie_test_input_suite(struct lorie_test_suite* suite) {
    lorie_suite_init(suite, "input", setup, teardown);
    SUITE_ADD(suite, test_input_init_creates_seat);
    SUITE_ADD(suite, test_pointer_focus_set_on_motion);
    SUITE_ADD(suite, test_pointer_focus_cleared_on_leave);
    SUITE_ADD(suite, test_keyboard_follows_pointer_focus);
    SUITE_ADD(suite, test_focus_cleared_on_surface_destroy);
    SUITE_ADD(suite, test_touch_focus_set_on_down);
    SUITE_ADD(suite, test_no_crash_when_no_focus);
    return 0;
}
