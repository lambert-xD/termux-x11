/* Lorie Wayland Compositor — Output Tests
 *
 * Dynamic output creation, sizing, and global lifecycle.
 */

#include "lorie_test.h"
#include "../compositor.h"

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

static void test_output_create_sets_dimensions(void) {
    struct lorie_output *output = lorie_output_create(g_comp, 1920, 1080, 1);
    ASSERT_NOT_NULL(output);
    ASSERT_EQ_INT(1920, output->width);
    ASSERT_EQ_INT(1080, output->height);
    ASSERT_EQ_INT(1, output->scale);
    lorie_output_destroy(output);
}

static void test_output_update_size_changes_fields(void) {
    struct lorie_output *output = lorie_output_create(g_comp, 1920, 1080, 1);
    ASSERT_NOT_NULL(output);
    lorie_output_update_size(output, 800, 600, 2);
    ASSERT_EQ_INT(800, output->width);
    ASSERT_EQ_INT(600, output->height);
    ASSERT_EQ_INT(2, output->scale);
    lorie_output_destroy(output);
}

static void test_output_update_zero_rejected(void) {
    struct lorie_output *output = lorie_output_create(g_comp, 1920, 1080, 1);
    ASSERT_NOT_NULL(output);
    lorie_output_update_size(output, 0, 0, 0);
    ASSERT_EQ_INT(1920, output->width);
    ASSERT_EQ_INT(1080, output->height);
    ASSERT_EQ_INT(1, output->scale);
    lorie_output_destroy(output);
}

static void test_output_global_created_on_start(void) {
    struct lorie_output *output = lorie_output_create(g_comp, 1080, 2400, 2);
    ASSERT_NOT_NULL(output);
    ASSERT_EQ_INT(0, lorie_compositor_start(g_comp));
    ASSERT_NOT_NULL(g_comp->output_global);
    lorie_compositor_stop(g_comp);
    lorie_output_destroy(output);
}

int lorie_test_output_suite(struct lorie_test_suite *suite) {
    lorie_suite_init(suite, "output", setup, teardown);
    SUITE_ADD(suite, test_output_create_sets_dimensions);
    SUITE_ADD(suite, test_output_update_size_changes_fields);
    SUITE_ADD(suite, test_output_update_zero_rejected);
    SUITE_ADD(suite, test_output_global_created_on_start);
    return 0;
}
