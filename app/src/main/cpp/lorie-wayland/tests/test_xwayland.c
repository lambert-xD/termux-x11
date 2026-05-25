/*
 * Lorie Wayland Compositor — XWayland Tests
 */

#include "lorie_test.h"
#include "../xwayland.h"
#include <unistd.h>
#include <sys/stat.h>

static void test_xwayland_init_shutdown(void) {
    struct lorie_xwayland *xw = lorie_xwayland_init(NULL, "/nonexistent/Xwayland");
    ASSERT_NOT_NULL(xw);
    ASSERT_EQ_STR("/nonexistent/Xwayland", xw->xserver_path);
    ASSERT_EQ_INT(-1, xw->pid);
    lorie_xwayland_shutdown(xw);
}

static void test_xwayland_init_finds_display(void) {
    struct lorie_xwayland *xw = lorie_xwayland_init(NULL, "/nonexistent/Xwayland");
    ASSERT_NOT_NULL(xw);
    ASSERT_TRUE(xw->display_number >= 0);
    ASSERT_TRUE(xw->display_number <= 99);
    lorie_xwayland_shutdown(xw);
}

static void test_xwayland_lockfile_format(void) {
    struct lorie_xwayland *xw = lorie_xwayland_init(NULL, "/nonexistent/Xwayland");
    ASSERT_NOT_NULL(xw);
    if (xw->lockfile) {
        struct stat st;
        int r = stat(xw->lockfile, &st);
        ASSERT_EQ_INT(0, r);
        ASSERT_TRUE(st.st_size == 11); /* "%10d\n" */
    }
    lorie_xwayland_shutdown(xw);
}

static void test_xwayland_sockets_created(void) {
    struct lorie_xwayland *xw = lorie_xwayland_init(NULL, "/nonexistent/Xwayland");
    ASSERT_NOT_NULL(xw);
    ASSERT_TRUE(xw->abstract_fd >= 0 || xw->unix_fd >= 0);
    lorie_xwayland_shutdown(xw);
}

static void test_xwayland_wm_socketpair(void) {
    struct lorie_xwayland *xw = lorie_xwayland_init(NULL, "/nonexistent/Xwayland");
    ASSERT_NOT_NULL(xw);
    ASSERT_TRUE(xw->wm_fd[0] >= 0);
    ASSERT_TRUE(xw->wm_fd[1] >= 0);
    ASSERT_TRUE(xw->wm_fd[0] != xw->wm_fd[1]);
    lorie_xwayland_shutdown(xw);
}

static void test_xwayland_launch_rejects_null_compositor(void) {
    struct lorie_xwayland *xw = lorie_xwayland_init(NULL, "/nonexistent/Xwayland");
    ASSERT_NOT_NULL(xw);
    ASSERT_EQ_INT(-1, lorie_xwayland_launch(xw));
    ASSERT_EQ_INT(0, xw->running);
    ASSERT_EQ_INT(-1, xw->pid);
    ASSERT_NULL(xw->sigchld_source);
    lorie_xwayland_shutdown(xw);
}

int lorie_test_xwayland_suite(struct lorie_test_suite* suite) {
    lorie_suite_init(suite, "xwayland", NULL, NULL);
    SUITE_ADD(suite, test_xwayland_init_shutdown);
    SUITE_ADD(suite, test_xwayland_init_finds_display);
    SUITE_ADD(suite, test_xwayland_lockfile_format);
    SUITE_ADD(suite, test_xwayland_sockets_created);
    SUITE_ADD(suite, test_xwayland_wm_socketpair);
    SUITE_ADD(suite, test_xwayland_launch_rejects_null_compositor);
    return 0;
}
