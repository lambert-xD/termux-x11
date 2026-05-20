/*
 * Lorie Wayland Compositor — JNI Bridge Tests
 *
 * Verifies security fixes and native method registration.
 */

#include "lorie_test.h"

/* Helpers exported from wayland-activity.c */
extern int lorie_clipboard_validate_size(uint32_t count);
extern int lorie_keycode_valid(int key_code);
extern const int lorie_wayland_native_method_count;

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

int lorie_test_jni_suite(struct lorie_test_suite* suite) {
    lorie_suite_init(suite, "jni", NULL, NULL);
    SUITE_ADD(suite, test_jni_native_methods_registered);
    SUITE_ADD(suite, test_clipboard_size_capped);
    SUITE_ADD(suite, test_keycode_bounds_checked);
    return 0;
}
