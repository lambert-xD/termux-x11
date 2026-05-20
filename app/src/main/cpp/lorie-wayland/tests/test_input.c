/* TDD tests for input + seat */
#include "lorie_test.h"
#include "compositor.h"
#include "input.h"

static void test_input_init_creates_seat(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    ASSERT_NOT_NULL(c);
    struct lorie_input *input = lorie_input_init(c->display);
    ASSERT_NOT_NULL(input);
    lorie_input_destroy(input);
    lorie_compositor_destroy(c);
}

static void test_input_pointer_motion(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    struct lorie_input *input = lorie_input_init(c->display);
    lorie_input_pointer_motion(input, 100.0f, 200.0f);
    lorie_input_dispatch(input);
    lorie_input_destroy(input);
    lorie_compositor_destroy(c);
}

static void test_input_keyboard_key(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    struct lorie_input *input = lorie_input_init(c->display);
    lorie_input_keyboard_key(input, 29, 1);
    lorie_input_dispatch(input);
    lorie_input_destroy(input);
    lorie_compositor_destroy(c);
}

static void test_input_touch_down_up(void) {
    struct lorie_compositor *c = lorie_compositor_create();
    struct lorie_input *input = lorie_input_init(c->display);
    lorie_input_touch_down(input, 0, 50.0f, 100.0f);
    lorie_input_touch_up(input, 0);
    lorie_input_dispatch(input);
    lorie_input_destroy(input);
    lorie_compositor_destroy(c);
}

int lorie_test_input_suite(struct lorie_test_suite* suite) {
    lorie_suite_init(suite, "input", NULL, NULL);
    SUITE_ADD(suite, test_input_init_creates_seat);
    SUITE_ADD(suite, test_input_pointer_motion);
    SUITE_ADD(suite, test_input_keyboard_key);
    SUITE_ADD(suite, test_input_touch_down_up);
    return 0;
}
