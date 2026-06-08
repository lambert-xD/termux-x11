/* Standalone host runner for the `teardown_guard` suite only.
 *
 * Isolation rationale (input-focus-uaf-teardown-race, mirrors the EXACT
 * precedent test_main_protocols_only.c established for the EXACT SAME crash
 * symptom — see engram bugfixes #163/#164 and discovery "LORIE_TEARDOWN_GUARD
 * immediately aborts full lorie-wayland-tests binary"):
 *
 * Once LORIE_TEARDOWN_GUARD is compiled in, the full lorie-wayland-tests
 * binary deterministically SIGABRTs (confirmed via real run: exit 134,
 * WTERMSIG == SIGABRT) on the first pre-existing unmigrated unsafe destroy —
 * input::test_pointer_focus_set_on_motion, which calls
 * lorie_surface_destroy_internal on the test thread while the compositor is
 * running. That crash is the documented, designed CORRECT behavior: the guard
 * converts the intermittent `wl_client_destroy -> wl_map_release ->
 * wl_array_release -> free() on garbage pointer` UAF (the very crash
 * test_main_protocols_only.c was written to dodge) into a deterministic, loud,
 * attributable abort. But abort() cannot be caught by the test framework's
 * setjmp/longjmp — it kills the whole process — so the full binary cannot run
 * to completion (and thus cannot give a clean RED->GREEN signal for the NEW
 * guard tests below) until Phase 3 of this change migrates every
 * running-compositor direct-destroy onto lorie_test_safe_destroy_client.
 *
 * This driver runs ONLY the `teardown_guard` suite (the new guard-trip,
 * no-false-positive, and helper tests for this change) so we get a
 * crash-free, deterministic GREEN/RED signal decoupled from the (now
 * mechanically-surfaced, soon-to-be-migrated) pre-existing unsafe callers. It
 * does NOT replace the full-suite target — `teardown_guard` is ALSO appended
 * to lorie-wayland-tests' registration list (test_main.c) so no coverage is
 * lost; once Phase 3 lands, it runs cleanly there too, exactly mirroring
 * test_main_protocols_only.c's stated "both are registered with CTest" promise.
 *
 * Wired into CMakeLists.txt / CTest as `lorie-wayland-teardown-guard-tests`
 * (CTest name `lorie-wayland-teardown-guard-unit`).
 */

#include "lorie_test.h"

int lorie_test_teardown_guard_suite(struct lorie_test_suite *suite);

int main(int argc, char **argv) {
    struct lorie_test_suite teardown_guard_suite;
    lorie_test_teardown_guard_suite(&teardown_guard_suite);

    struct lorie_test_suite *suites[] = { &teardown_guard_suite };
    return lorie_test_main(argc, argv, suites, 1);
}
