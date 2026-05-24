#include "lorie_test.h"
#include "keymap.h"
#include <unistd.h>
#include <string.h>

static void test_keymap_create_fd(void) {
    size_t size = 0;
    int fd = lorie_keymap_create_fd(&size);
    ASSERT_TRUE(fd >= 0);
    ASSERT_TRUE(size > 0);

    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    ASSERT_TRUE(n > 0);
    buf[n] = '\0';
    ASSERT_TRUE(strstr(buf, "xkb_keymap") != NULL);
    ASSERT_TRUE(strstr(buf, "<ESC>=9") != NULL);
    ASSERT_TRUE(strstr(buf, "<AE01>=10") != NULL);
    ASSERT_TRUE(strstr(buf, "<AC01>=38") != NULL);

    ASSERT_EQ_INT(0, close(fd));
}

static void test_keymap_create_fd_rejects_null(void) {
    ASSERT_EQ_INT(-1, lorie_keymap_create_fd(NULL));
}

int lorie_test_keymap_suite(struct lorie_test_suite* suite) {
    lorie_suite_init(suite, "keymap", NULL, NULL);
    SUITE_ADD(suite, test_keymap_create_fd);
    SUITE_ADD(suite, test_keymap_create_fd_rejects_null);
    return 0;
}
