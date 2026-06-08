/* Standalone host runner for the `protocols` suite only.
 *
 * Isolation aid for xdg-shell-configure (see engram bugfixes #163/#164):
 * on this host (no real GPU/EGL driver, single long-running process spinning
 * up ~20 compositor instances back to back) the full lorie-wayland-tests
 * binary intermittently SIGSEGVs with heap corruption inside the unrelated,
 * pre-existing `input` suite (wl_client_destroy -> wl_map_release ->
 * wl_array_release -> free() on a garbage pointer; the exact triggering test
 * and crash site vary run to run — classic use-after-free/double-free
 * symptom, NOT an xdg-shell-configure regression). That suite runs BEFORE
 * `protocols` in test_main.c's registration order, so the crash can prevent
 * the protocols suite — which carries every xdg-shell-configure spec test —
 * from ever executing.
 *
 * This driver runs ONLY the `protocols` suite (which contains the new/updated
 * xdg-shell-configure tests) so we get a crash-free, deterministic GREEN/RED
 * signal for the spec-covering tests, decoupled from that pre-existing,
 * out-of-scope `input`-suite memory-safety issue. It does NOT remove or
 * replace the full-suite target — both are registered with CTest so no
 * coverage is lost; `lorie-wayland-protocols-tests` simply gives a reliable
 * way to exercise the xdg-shell-configure-relevant suite in isolation until
 * the `input`-suite corruption is independently triaged and fixed.
 *
 * Wired into CMakeLists.txt / CTest as `lorie-wayland-protocols-tests`
 * (target `lorie-wayland-protocols-tests`, CTest name
 * `lorie-wayland-protocols-unit`).
 */

#include "lorie_test.h"

int lorie_test_protocols_suite(struct lorie_test_suite *suite);

int main(int argc, char **argv) {
    struct lorie_test_suite protocols_suite;
    lorie_test_protocols_suite(&protocols_suite);

    struct lorie_test_suite *suites[] = { &protocols_suite };
    return lorie_test_main(argc, argv, suites, 1);
}
