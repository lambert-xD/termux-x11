/*
 * Lorie Wayland Compositor — Test Runner
 *
 * Entry point for all C unit tests. Each PR adds its own test file
 * and registers its suite here.
 */

#include "lorie_test.h"

/* Forward declarations — each test_*.c file provides one suite init. */
extern int lorie_test_framework_suite(struct lorie_test_suite* suite);
extern int lorie_test_build_suite(struct lorie_test_suite* suite);
extern int lorie_test_ndk_build_suite(struct lorie_test_suite* suite);
extern int lorie_test_compositor_suite(struct lorie_test_suite* suite);
extern int lorie_test_surface_suite(struct lorie_test_suite* suite);
extern int lorie_test_renderer_suite(struct lorie_test_suite* suite);
extern int lorie_test_input_suite(struct lorie_test_suite* suite);
extern int lorie_test_protocols_suite(struct lorie_test_suite* suite);
extern int lorie_test_xwayland_suite(struct lorie_test_suite* suite);
extern int lorie_test_jni_suite(struct lorie_test_suite* suite);
extern int lorie_test_integration_suite(struct lorie_test_suite* suite);
extern int lorie_test_shm_texture_suite(struct lorie_test_suite* suite);

int main(int argc, char** argv) {
    struct lorie_test_suite framework_suite;
    lorie_test_framework_suite(&framework_suite);

    struct lorie_test_suite build_suite;
    lorie_test_build_suite(&build_suite);

    struct lorie_test_suite ndk_build_suite;
    lorie_test_ndk_build_suite(&ndk_build_suite);

    struct lorie_test_suite compositor_suite;
    lorie_test_compositor_suite(&compositor_suite);

    struct lorie_test_suite surface_suite;
    lorie_test_surface_suite(&surface_suite);

    struct lorie_test_suite renderer_suite;
    lorie_test_renderer_suite(&renderer_suite);

    struct lorie_test_suite input_suite;
    lorie_test_input_suite(&input_suite);

    struct lorie_test_suite protocols_suite;
    lorie_test_protocols_suite(&protocols_suite);

    struct lorie_test_suite xwayland_suite;
    lorie_test_xwayland_suite(&xwayland_suite);

    struct lorie_test_suite jni_suite;
    lorie_test_jni_suite(&jni_suite);

    struct lorie_test_suite integration_suite;
    lorie_test_integration_suite(&integration_suite);

    struct lorie_test_suite shm_texture_suite;
    lorie_test_shm_texture_suite(&shm_texture_suite);

    struct lorie_test_suite* suites[] = {
        &framework_suite,
        &build_suite,
        &ndk_build_suite,
        &compositor_suite,
        &surface_suite,
        &renderer_suite,
        &input_suite,
        &protocols_suite,
        &xwayland_suite,
        &jni_suite,
        &integration_suite,
        &shm_texture_suite,
    };

    return lorie_test_main(argc, argv, suites, sizeof(suites) / sizeof(suites[0]));
}
