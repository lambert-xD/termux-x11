/*
 * Lorie Wayland Compositor — JNI Bridge Tests
 *
 * Verifies security fixes and native method registration.
 */

#include "lorie_test.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

/* Helpers exported from wayland-activity.c */
extern int lorie_clipboard_validate_size(uint32_t count);
extern int lorie_keycode_valid(int key_code);
extern int lorie_setup_wayland_runtime_dir(void);
extern int lorie_wayland_socket_ready(void);
extern const int lorie_wayland_native_method_count;

struct env_snapshot {
    char *xdg_runtime_dir;
    char *tmpdir;
    char *wayland_display;
};

static struct env_snapshot saved_env;

static char *dup_env(const char *name) {
    const char *value = getenv(name);
    return value ? strdup(value) : NULL;
}

static void restore_env_var(const char *name, char *value) {
    if (value)
        setenv(name, value, 1);
    else
        unsetenv(name);
}

static void test_jni_setup(void) {
    saved_env.xdg_runtime_dir = dup_env("XDG_RUNTIME_DIR");
    saved_env.tmpdir = dup_env("TMPDIR");
    saved_env.wayland_display = dup_env("WAYLAND_DISPLAY");
}

static void test_jni_teardown(void) {
    restore_env_var("XDG_RUNTIME_DIR", saved_env.xdg_runtime_dir);
    restore_env_var("TMPDIR", saved_env.tmpdir);
    restore_env_var("WAYLAND_DISPLAY", saved_env.wayland_display);
    free(saved_env.xdg_runtime_dir);
    free(saved_env.tmpdir);
    free(saved_env.wayland_display);
    memset(&saved_env, 0, sizeof(saved_env));
}

static void test_jni_native_methods_registered(void) {
    ASSERT_TRUE(lorie_wayland_native_method_count > 0);
}

static void test_clipboard_size_capped(void) {
    ASSERT_TRUE(lorie_clipboard_validate_size(0));
    ASSERT_TRUE(lorie_clipboard_validate_size(1024));
    ASSERT_TRUE(lorie_clipboard_validate_size(1024 * 1024));
    ASSERT_FALSE(lorie_clipboard_validate_size(1024 * 1024 + 1));
    ASSERT_FALSE(lorie_clipboard_validate_size(0xFFFFFFFFU));
}

static void test_keycode_bounds_checked(void) {
    ASSERT_TRUE(lorie_keycode_valid(0));
    ASSERT_TRUE(lorie_keycode_valid(303));
    ASSERT_FALSE(lorie_keycode_valid(-1));
    ASSERT_FALSE(lorie_keycode_valid(304));
    ASSERT_FALSE(lorie_keycode_valid(10000));
}

/* android_to_linux_keycode is defined in keymap.c */
extern int android_to_linux_keycode[304];

static void test_keycode_a_maps_to_linux_30(void) {
    ASSERT_EQ_INT(30, android_to_linux_keycode[29]);
}

static void test_keycode_menu_maps_to_linux_139(void) {
    ASSERT_EQ_INT(139, android_to_linux_keycode[82]);
}

static void test_keycode_unmapped_is_zero(void) {
    ASSERT_EQ_INT(0, android_to_linux_keycode[18]);
    ASSERT_EQ_INT(0, android_to_linux_keycode[200]);
}

static void test_wayland_runtime_dir_prefers_xdg_runtime_dir(void) {
    char dir[] = "/tmp/lorie_xdg_runtime_XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(dir));

    setenv("XDG_RUNTIME_DIR", dir, 1);
    unsetenv("WAYLAND_DISPLAY");

    ASSERT_EQ_INT(0, lorie_setup_wayland_runtime_dir());
    ASSERT_EQ_STR(dir, getenv("XDG_RUNTIME_DIR"));
    ASSERT_EQ_STR("wayland-0", getenv("WAYLAND_DISPLAY"));

    rmdir(dir);
}

static void test_wayland_runtime_dir_falls_back_to_tmpdir(void) {
    char dir[] = "/tmp/lorie_tmp_runtime_XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(dir));

    unsetenv("XDG_RUNTIME_DIR");
    unsetenv("WAYLAND_DISPLAY");
    setenv("TMPDIR", dir, 1);

    ASSERT_EQ_INT(0, lorie_setup_wayland_runtime_dir());
    ASSERT_EQ_STR(dir, getenv("XDG_RUNTIME_DIR"));
    ASSERT_EQ_STR("wayland-0", getenv("WAYLAND_DISPLAY"));

    unsetenv("TMPDIR");
    rmdir(dir);
}

static void test_wayland_runtime_dir_preserves_wayland_display(void) {
    char dir[] = "/tmp/lorie_display_runtime_XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(dir));

    setenv("XDG_RUNTIME_DIR", dir, 1);
    setenv("WAYLAND_DISPLAY", "wayland-7", 1);

    ASSERT_EQ_INT(0, lorie_setup_wayland_runtime_dir());
    ASSERT_EQ_STR(dir, getenv("XDG_RUNTIME_DIR"));
    ASSERT_EQ_STR("wayland-7", getenv("WAYLAND_DISPLAY"));

    unsetenv("WAYLAND_DISPLAY");
    rmdir(dir);
}

static void test_socket_ready_when_socket_exists(void) {
    char dir[] = "/tmp/lorie_socket_XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(dir));
    setenv("XDG_RUNTIME_DIR", dir, 1);
    setenv("WAYLAND_DISPLAY", "wayland-test", 1);

    char sock[1024];
    snprintf(sock, sizeof(sock), "%s/wayland-test", dir);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_TRUE(fd >= 0);
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock, sizeof(addr.sun_path) - 1);
    int bound = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    ASSERT_TRUE(bound == 0);

    ASSERT_TRUE(lorie_wayland_socket_ready());

    close(fd);
    unlink(sock);
    rmdir(dir);
}

static void test_socket_ready_when_missing(void) {
    char dir[] = "/tmp/lorie_nosock_XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(dir));
    setenv("XDG_RUNTIME_DIR", dir, 1);
    setenv("WAYLAND_DISPLAY", "wayland-missing", 1);

    ASSERT_FALSE(lorie_wayland_socket_ready());

    rmdir(dir);
}

int lorie_test_jni_suite(struct lorie_test_suite* suite) {
    lorie_suite_init(suite, "jni", test_jni_setup, test_jni_teardown);
    SUITE_ADD(suite, test_jni_native_methods_registered);
    SUITE_ADD(suite, test_clipboard_size_capped);
    SUITE_ADD(suite, test_keycode_bounds_checked);
    SUITE_ADD(suite, test_keycode_a_maps_to_linux_30);
    SUITE_ADD(suite, test_keycode_menu_maps_to_linux_139);
    SUITE_ADD(suite, test_keycode_unmapped_is_zero);
    SUITE_ADD(suite, test_wayland_runtime_dir_prefers_xdg_runtime_dir);
    SUITE_ADD(suite, test_wayland_runtime_dir_falls_back_to_tmpdir);
    SUITE_ADD(suite, test_wayland_runtime_dir_preserves_wayland_display);
    SUITE_ADD(suite, test_socket_ready_when_socket_exists);
    SUITE_ADD(suite, test_socket_ready_when_missing);
    return 0;
}
