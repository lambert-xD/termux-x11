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
extern int lorie_test_keymap_suite(struct lorie_test_suite* suite);
extern int lorie_test_protocols_suite(struct lorie_test_suite* suite);
extern int lorie_test_xwayland_suite(struct lorie_test_suite* suite);
extern int lorie_test_jni_suite(struct lorie_test_suite* suite);
extern int lorie_test_integration_suite(struct lorie_test_suite* suite);
extern int lorie_test_shm_texture_suite(struct lorie_test_suite* suite);
extern int lorie_test_renderer_damage_suite(struct lorie_test_suite* suite);
extern int lorie_test_transform_suite(struct lorie_test_suite* suite);
extern int lorie_test_viewporter_suite(struct lorie_test_suite* suite);
extern int lorie_test_dmabuf_suite(struct lorie_test_suite* suite);
extern int lorie_test_clipboard_suite(struct lorie_test_suite* suite);
extern int lorie_test_shm_buffer_suite(struct lorie_test_suite* suite);
extern int lorie_test_output_suite(struct lorie_test_suite* suite);
extern int lorie_test_teardown_guard_suite(struct lorie_test_suite* suite);

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

    /* Registered here (right after surface, before input/protocols) so that —
     * even pre-Phase-3-migration, while LORIE_TEARDOWN_GUARD makes the binary
     * SIGABRT on the first unmigrated unsafe destroy further down the
     * registration order — this suite's guard-trip / no-false-positive /
     * helper tests still get a chance to execute and report in the full
     * binary's run, maximizing coverage that survives until migration lands.
     * It is ALSO run via the dedicated isolated runner
     * (lorie-wayland-teardown-guard-tests / test_teardown_guard_main.c) for a
     * guaranteed crash-free signal in the meantime — see that file's header
     * comment for the full isolation rationale (mirrors
     * test_main_protocols_only.c's precedent for the identical crash symptom). */
    struct lorie_test_suite teardown_guard_suite;
    lorie_test_teardown_guard_suite(&teardown_guard_suite);

    struct lorie_test_suite input_suite;
    lorie_test_input_suite(&input_suite);

    struct lorie_test_suite keymap_suite;
    lorie_test_keymap_suite(&keymap_suite);

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

    struct lorie_test_suite renderer_damage_suite;
    lorie_test_renderer_damage_suite(&renderer_damage_suite);

    struct lorie_test_suite transform_suite;
    lorie_test_transform_suite(&transform_suite);

    struct lorie_test_suite viewporter_suite;
    lorie_test_viewporter_suite(&viewporter_suite);

    struct lorie_test_suite dmabuf_suite;
    lorie_test_dmabuf_suite(&dmabuf_suite);

    struct lorie_test_suite clipboard_suite;
    lorie_test_clipboard_suite(&clipboard_suite);

    struct lorie_test_suite shm_buffer_suite;
    lorie_test_shm_buffer_suite(&shm_buffer_suite);

    struct lorie_test_suite output_suite;
    lorie_test_output_suite(&output_suite);

    struct lorie_test_suite* suites[] = {
        &framework_suite,
        &build_suite,
        &ndk_build_suite,
        &compositor_suite,
        &surface_suite,
        &renderer_suite,
        &teardown_guard_suite,
        &input_suite,
        &keymap_suite,
        &protocols_suite,
        &xwayland_suite,
        &jni_suite,
        &integration_suite,
        &shm_texture_suite,
        &renderer_damage_suite,
        &transform_suite,
        &viewporter_suite,
        &dmabuf_suite,
        &clipboard_suite,
        &shm_buffer_suite,
        &output_suite,
    };

    return lorie_test_main(argc, argv, suites, sizeof(suites) / sizeof(suites[0]));
}
