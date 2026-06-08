# CMake build for Lorie Wayland unit tests
# Minimal test runner — zero external dependencies

cmake_minimum_required(VERSION 3.22)

set(LORIE_TEST_PRODUCTION_SOURCES
    ../compositor.c
    ../output.c
    ../surface.c
    ../renderer.c
    ../input.c
    ../seat.c
    ../keymap.c
    ../protocols/xdg-shell.c
    ../protocols/linux-dmabuf.c
    ../protocols/wl-data-device-manager.c
    ../protocols/viewporter.c
    ../protocols/xwayland-shell.c
    ../xwayland.c
    ../clipboard.c
    ../wayland-activity.c
    ../main.c
)

set(LORIE_TEST_SOURCES
    test_main.c
    test_framework.c
    test_build.c
    test_ndk_build.c
    test_compositor.c
    test_surface.c
    test_renderer.c
    # Reusable safe-teardown test primitives (compositor-teardown-safety,
    # input-focus-uaf-teardown-race Phase 2): join-then-destroy helpers used
    # by test_teardown_guard.c now and by the Phase 3 test migrations later.
    lorie_test_teardown.c
    # Cross-thread teardown guard suite (compositor-teardown-safety,
    # input-focus-uaf-teardown-race Phase 1+2): guard-trip death-test,
    # no-false-positive tests, helper-based-teardown test. Registered here for
    # full-binary coverage AND via its own isolated runner — see
    # test_teardown_guard_main.c's header comment for why both exist
    # (mirrors the lorie-wayland-protocols-tests precedent for the identical
    # crash symptom).
    test_teardown_guard.c
    test_input.c
    test_keymap.c
    test_protocols.c
    test_output.c
    ${LORIE_TEST_PRODUCTION_SOURCES}
    test_xwayland.c
    test_jni.c
    test_integration.c
    test_shm_texture.c
    test_buffer_stubs.c
    test_renderer_damage.c
    test_transform.c
    test_viewporter.c
    test_dmabuf.c
    test_clipboard.c
    test_shm_buffer.c
)

# Standalone `protocols`-suite-only runner — see test_main_protocols_only.c
# for why this exists (isolation from a pre-existing, out-of-scope `input`
# suite memory-safety crash that can land before `protocols` runs in the
# full binary's registration order; engram bugfixes #163/#164). It links the
# SAME production sources plus the framework and protocols test files so the
# xdg-shell-configure spec tests get a crash-free, deterministic signal.
set(LORIE_PROTOCOLS_TEST_SOURCES
    test_main_protocols_only.c
    test_framework.c
    test_protocols.c
    # Reusable safe-teardown test primitives — see LORIE_TEST_SOURCES comment;
    # linked here too (task 2.3) so Phase 3's protocols-suite migrations can
    # use lorie_test_safe_destroy_client/_surface in this isolated binary.
    lorie_test_teardown.c
    # Provides link-time stubs for LorieBuffer_* (allocate/description/
    # attachToGL/bindTexture/__LorieBuffer_free): surface.c and renderer.c
    # reference these, but the real implementation lives in JNI-side Kotlin/
    # Java code that isn't linkable into a host test binary.
    test_buffer_stubs.c
    ${LORIE_TEST_PRODUCTION_SOURCES}
)

# Standalone `teardown_guard`-suite-only runner — see
# test_teardown_guard_main.c for why this exists (isolation from the SAME
# pre-existing, out-of-scope `input`/`surface` cross-thread UAF crash that
# motivated lorie-wayland-protocols-tests above; input-focus-uaf-teardown-race
# Phase 1+2, engram bugfixes #163/#164). Once LORIE_TEARDOWN_GUARD is defined,
# the full lorie-wayland-tests binary deterministically SIGABRTs on the first
# unmigrated unsafe destroy (confirmed: exit 134 / WTERMSIG=SIGABRT) — abort()
# cannot be caught by the test framework's setjmp/longjmp, so this driver
# gives the new guard-trip / no-false-positive / helper tests a crash-free,
# deterministic GREEN/RED signal until Phase 3 migrates those callers.
set(LORIE_TEARDOWN_GUARD_TEST_SOURCES
    test_teardown_guard_main.c
    test_framework.c
    test_teardown_guard.c
    lorie_test_teardown.c
    test_buffer_stubs.c
    ${LORIE_TEST_PRODUCTION_SOURCES}
)

if (NOT ANDROID)
    # libandroid (ANativeWindow_*) is not available on a plain Linux host;
    # provide link-time stubs so production sources that reference it can be
    # linked into the host test binary.
    list(APPEND LORIE_TEST_SOURCES test_android_stubs.c)
    list(APPEND LORIE_PROTOCOLS_TEST_SOURCES test_android_stubs.c)
    list(APPEND LORIE_TEARDOWN_GUARD_TEST_SOURCES test_android_stubs.c)
endif()

add_executable(lorie-wayland-tests ${LORIE_TEST_SOURCES})
add_executable(lorie-wayland-protocols-tests ${LORIE_PROTOCOLS_TEST_SOURCES})
add_executable(lorie-wayland-teardown-guard-tests ${LORIE_TEARDOWN_GUARD_TEST_SOURCES})

# Shared setup (include dirs, JNI headers, link libraries, C standard) for
# every Lorie Wayland test executable defined in this directory.
function(lorie_wayland_test_configure target)
    # Cross-thread teardown guard (input-focus-uaf-teardown-race): compiled
    # ONLY into test/host builds via this define — the production NDK
    # CMakeLists.txt (../../CMakeLists.txt) never defines it, so the guard's
    # `#if defined(LORIE_TEARDOWN_GUARD)` body (compositor.c) and call site
    # (surface.c:lorie_surface_destroy_internal) compile to nothing in the
    # shipped release libXlorie.so — zero runtime cost, zero new abort path
    # in production. See compositor-teardown-safety spec.
    target_compile_definitions(${target} PRIVATE LORIE_TEARDOWN_GUARD)

    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        # Headers generated by wayland.cmake (wayland-server-protocol.h etc.)
        ${CMAKE_CURRENT_BINARY_DIR}/../..
        # Headers generated by wayland-protocols.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/../../wayland-protocols
        # Pixman headers
        ${CMAKE_CURRENT_SOURCE_DIR}/../../pixman/pixman
    )

    if (NOT ANDROID)
        # Host builds need real JNI headers (wayland-activity.c includes <jni.h>)
        # plus host mocks (under tests/{android,linux,EGL,GLES2}/) that shadow the
        # Android-NDK-only headers (android/native_window.h, android/hardware_buffer.h,
        # linux/ashmem.h, EGL/egl.h, GLES2/gl2.h, ...) referenced by the production
        # sources. The mock dirs are picked up automatically because
        # ${CMAKE_CURRENT_SOURCE_DIR} (tests/) is already on the include path above
        # and contains matching subdirectories (tests/android/, tests/linux/, ...).
        if (DEFINED ENV{JAVA_HOME} AND EXISTS "$ENV{JAVA_HOME}/include/jni.h")
            target_include_directories(${target} PRIVATE
                $ENV{JAVA_HOME}/include
                $ENV{JAVA_HOME}/include/linux
            )
        else()
            message(WARNING
                "JAVA_HOME is not set (or $ENV{JAVA_HOME}/include/jni.h is missing); "
                "host build of ${target} will fail to find <jni.h>. "
                "Set JAVA_HOME to a JDK installation to fix this.")
        endif()
    endif()

    if (ANDROID)
        target_link_libraries(${target} PRIVATE
            android
            log
            EGL
            GLESv2
            wayland-server
            wayland-protocols-generated
            pixman
        )
    else()
        # Host distros (e.g. Ubuntu/Mesa) usually ship libEGL.so.1 / libGLESv2.so.2
        # without the unversioned -lEGL / -lGLESv2 dev symlinks. Resolve the real
        # library files via find_library so the host link step succeeds.
        find_library(LORIE_TEST_EGL_LIB NAMES EGL libEGL.so.1 libEGL.so)
        find_library(LORIE_TEST_GLESV2_LIB NAMES GLESv2 libGLESv2.so.2 libGLESv2.so)

        if (NOT LORIE_TEST_EGL_LIB OR NOT LORIE_TEST_GLESV2_LIB)
            message(WARNING
                "Could not locate a host EGL/GLESv2 runtime library "
                "(found EGL='${LORIE_TEST_EGL_LIB}' GLESv2='${LORIE_TEST_GLESV2_LIB}'). "
                "Install Mesa (libegl1, libgles2) to fix the ${target} host link step.")
        endif()

        target_link_libraries(${target} PRIVATE
            ${LORIE_TEST_EGL_LIB}
            ${LORIE_TEST_GLESV2_LIB}
            wayland-server
            wayland-protocols-generated
            pixman
            m
        )
    endif()

    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
    )

    # Ensure protocol headers are generated before test compilation
    add_dependencies(${target} wayland-protocols-headers wayland-protocols-generated)
endfunction()

lorie_wayland_test_configure(lorie-wayland-tests)
lorie_wayland_test_configure(lorie-wayland-protocols-tests)
lorie_wayland_test_configure(lorie-wayland-teardown-guard-tests)

# Cross-thread teardown guard requires real pthread.h (pthread_create/_join/
# _equal) plus fork()/waitpid()/signal.h for the death-test repro — link the
# pthread runtime explicitly (glibc >= 2.34 folds it into libc, but linking it
# unconditionally is harmless and matches how the production sources already
# pull pthread_create/_join in via compositor.c without an explicit -lpthread,
# i.e. it is needed for the death-test's helper thread on older toolchains).
find_library(LORIE_TEST_PTHREAD_LIB NAMES pthread)
if (LORIE_TEST_PTHREAD_LIB)
    target_link_libraries(lorie-wayland-teardown-guard-tests PRIVATE ${LORIE_TEST_PTHREAD_LIB})
endif()

# --- TSan race-repro target (compositor-teardown-safety / Phase 4, design D4) ---
#
# Builds tests/test_teardown_tsan.c (a tiny standalone create/focus/destroy-
# while-running loop, NOT a CTest-framework test — see that file's header)
# against the real production sources with -fsanitize=thread, in TWO modes:
#   "unsafe" — the original racy pattern (RED: TSan must report a data race)
#   "safe"   — lorie_test_safe_destroy_client's join-then-destroy ordering
#              (GREEN: TSan must report a clean run — the contrast case)
#
# Host-only (NOT ANDROID — TSan needs a host libtsan + linker; the NDK does
# not ship one for the on-device target) and probed for genuine usability
# (compile AND link AND run a trivial threaded program clean) rather than
# merely "the flag was accepted" — see the two real, evidence-backed host
# quirks documented inline below, both confirmed via direct experimentation:
#
#   1. The Android NDK's clang/lld toolchain shadows /usr/bin/ld on PATH with
#      an `lld` *generic driver* (not a real linker) — `gcc -fsanitize=thread`
#      fails at link time with "lld is a generic driver. Invoke ld.lld...".
#      Fix: -fuse-ld=bfd (GNU bfd ld is present at /usr/bin/ld.bfd and links
#      cleanly).
#   2. Even once linked, TSan aborts at *runtime* on this host/kernel with
#      "FATAL: ThreadSanitizer: unexpected memory mapping ..." — a documented
#      TSan-vs-ASLR shadow-memory-layout incompatibility on modern
#      Ubuntu/glibc. Fix: run under `setarch $(uname -m) -R` (disable ASLR
#      for the child process only — does not affect the rest of the build or
#      any other test binary).
#
# If EITHER probe fails, the target is skipped with a clear status message
# and a CTest entry that reports "TSan unavailable on this host — see the
# always-on lorie-wayland-teardown-guard-unit death-test for the portable,
# CI-safe proof that the unsafe pattern aborts deterministically" — exactly
# the design's documented fallback (D4: "guard-trip test is portable proof
# if CI lacks TSan"), so `ctest` never reports a hard FAIL for an
# environment limitation outside this change's control.
if (NOT ANDROID)
    include(CheckCCompilerFlag)

    set(CMAKE_REQUIRED_FLAGS "-fsanitize=thread -fuse-ld=bfd")
    check_c_compiler_flag("-fsanitize=thread" LORIE_TSAN_FLAG_ACCEPTED)
    unset(CMAKE_REQUIRED_FLAGS)

    set(LORIE_TSAN_USABLE FALSE)
    if (LORIE_TSAN_FLAG_ACCEPTED)
        find_program(LORIE_TSAN_SETARCH NAMES setarch)
        if (LORIE_TSAN_SETARCH)
            # Probe 1+2 together: compile+link+RUN (under setarch -R) a
            # trivial real pthread program. This is the ONLY way to catch
            # quirk #2 (link succeeds; only the *run* fails) — a flag-only
            # check_c_compiler_flag would report a false "TSan is usable".
            execute_process(
                COMMAND ${CMAKE_C_COMPILER} -fsanitize=thread -fuse-ld=bfd -no-pie
                        -x c -o ${CMAKE_CURRENT_BINARY_DIR}/lorie_tsan_probe -
                INPUT_FILE /dev/null
                RESULT_VARIABLE LORIE_TSAN_PROBE_COMPILE_RC
                OUTPUT_QUIET ERROR_QUIET
            )
            # The probe source is fed via a heredoc-equivalent: write it out
            # first so -x c - (stdin) gets real pthread content, not /dev/null.
            file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/lorie_tsan_probe.c
"#include <pthread.h>\n#include <stdio.h>\nstatic void *fn(void *a){(void)a; return NULL;}\nint main(void){pthread_t t; pthread_create(&t,NULL,fn,NULL); pthread_join(t,NULL); printf(\"ok\\n\"); return 0;}\n")
            execute_process(
                COMMAND ${CMAKE_C_COMPILER} -fsanitize=thread -fuse-ld=bfd -no-pie
                        ${CMAKE_CURRENT_BINARY_DIR}/lorie_tsan_probe.c
                        -o ${CMAKE_CURRENT_BINARY_DIR}/lorie_tsan_probe
                RESULT_VARIABLE LORIE_TSAN_PROBE_COMPILE_RC
                OUTPUT_QUIET ERROR_QUIET
            )
            if (LORIE_TSAN_PROBE_COMPILE_RC EQUAL 0)
                execute_process(
                    COMMAND ${LORIE_TSAN_SETARCH} ${CMAKE_SYSTEM_PROCESSOR} -R
                            ${CMAKE_CURRENT_BINARY_DIR}/lorie_tsan_probe
                    RESULT_VARIABLE LORIE_TSAN_PROBE_RUN_RC
                    OUTPUT_QUIET ERROR_QUIET
                )
                if (LORIE_TSAN_PROBE_RUN_RC EQUAL 0)
                    set(LORIE_TSAN_USABLE TRUE)
                endif()
            endif()
        endif()
    endif()

    if (LORIE_TSAN_USABLE)
        message(STATUS "TSan probe passed (compile -fuse-ld=bfd + run under setarch -R) — building lorie-wayland-teardown-tsan")

        add_executable(lorie-wayland-teardown-tsan
            test_teardown_tsan.c
            lorie_test_teardown.c
            test_buffer_stubs.c
            test_android_stubs.c
            ${LORIE_TEST_PRODUCTION_SOURCES}
        )

        target_compile_options(lorie-wayland-teardown-tsan PRIVATE
            -fsanitize=thread -fsanitize=undefined -g -O1
        )
        target_link_options(lorie-wayland-teardown-tsan PRIVATE
            -fsanitize=thread -fsanitize=undefined -fuse-ld=bfd -no-pie
        )
        # Deliberately NOT calling lorie_wayland_test_configure here: that
        # helper unconditionally adds -DLORIE_TEARDOWN_GUARD, which would
        # make the "unsafe" mode abort() before TSan can observe the race
        # (see test_teardown_tsan.c's header for why the guard must stay
        # OFF for this specific target). Configure include dirs / libs
        # directly instead, mirroring the helper's host (NOT ANDROID) branch.
        target_include_directories(lorie-wayland-teardown-tsan PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/..
            ${CMAKE_CURRENT_BINARY_DIR}/../..
            ${CMAKE_CURRENT_BINARY_DIR}/../../wayland-protocols
            ${CMAKE_CURRENT_SOURCE_DIR}/../../pixman/pixman
        )
        if (DEFINED ENV{JAVA_HOME} AND EXISTS "$ENV{JAVA_HOME}/include/jni.h")
            target_include_directories(lorie-wayland-teardown-tsan PRIVATE
                $ENV{JAVA_HOME}/include
                $ENV{JAVA_HOME}/include/linux
            )
        endif()
        find_library(LORIE_TSAN_EGL_LIB NAMES EGL libEGL.so.1 libEGL.so)
        find_library(LORIE_TSAN_GLESV2_LIB NAMES GLESv2 libGLESv2.so.2 libGLESv2.so)
        target_link_libraries(lorie-wayland-teardown-tsan PRIVATE
            ${LORIE_TSAN_EGL_LIB}
            ${LORIE_TSAN_GLESV2_LIB}
            wayland-server
            wayland-protocols-generated
            pixman
            m
        )
        if (LORIE_TEST_PTHREAD_LIB)
            target_link_libraries(lorie-wayland-teardown-tsan PRIVATE ${LORIE_TEST_PTHREAD_LIB})
        endif()
        set_target_properties(lorie-wayland-teardown-tsan PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED ON
        )
        add_dependencies(lorie-wayland-teardown-tsan wayland-protocols-headers wayland-protocols-generated)

        # RED contrast case: the ORIGINAL unsafe pattern. TSAN_OPTIONS sets
        # halt_on_error=1 (stop at first race, matching "first occurrence"
        # in the design) and exitcode=66 (TSan's documented default — made
        # explicit so the PASS_REGULAR_EXPRESSION below has a stable, named
        # signal to anchor on regardless of host TSAN_OPTIONS env overrides).
        # PASS criterion: the run MUST print "ThreadSanitizer: data race" —
        # i.e. this CTest entry passes precisely WHEN the sanitizer detects
        # the race (a "RED" repro that successfully reproduces is the GREEN
        # for this CTest entry's own purpose: proving the unsafe pattern
        # genuinely races under a real race detector, not just "looks risky").
        add_test(NAME lorie-wayland-teardown-tsan-unsafe-races
                 COMMAND ${LORIE_TSAN_SETARCH} ${CMAKE_SYSTEM_PROCESSOR} -R
                         $<TARGET_FILE:lorie-wayland-teardown-tsan> unsafe)
        set_tests_properties(lorie-wayland-teardown-tsan-unsafe-races PROPERTIES
            ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1:exitcode=66"
            PASS_REGULAR_EXPRESSION "ThreadSanitizer: data race"
        )

        # GREEN contrast case: the SAFE (helper-based) pattern. PASS
        # criterion: the run completes ALL cycles AND TSan reports NOTHING —
        # i.e. this entry passes precisely WHEN the sanitizer stays silent
        # (FAIL_REGULAR_EXPRESSION catches a race report turning a "should be
        # clean" run into a hard failure, the inverse assertion of the entry
        # above — together they are the RED/GREEN pair design D4 specifies).
        add_test(NAME lorie-wayland-teardown-tsan-safe-clean
                 COMMAND ${LORIE_TSAN_SETARCH} ${CMAKE_SYSTEM_PROCESSOR} -R
                         $<TARGET_FILE:lorie-wayland-teardown-tsan> safe)
        set_tests_properties(lorie-wayland-teardown-tsan-safe-clean PROPERTIES
            ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1:exitcode=66"
            PASS_REGULAR_EXPRESSION "completed 8 cycles without crashing"
            FAIL_REGULAR_EXPRESSION "ThreadSanitizer"
        )
    else()
        message(STATUS "TSan probe failed on this host (toolchain/runtime limitation, not a code issue) — "
                       "skipping lorie-wayland-teardown-tsan. The always-on "
                       "lorie-wayland-teardown-guard-unit death-test remains the "
                       "portable, CI-safe proof that the unsafe pattern aborts "
                       "deterministically (design D4's documented fallback).")
        add_test(NAME lorie-wayland-teardown-tsan-unavailable
                 COMMAND ${CMAKE_COMMAND} -E echo
                 "TSan unavailable on this host (toolchain/runtime probe failed) -- see lorie-wayland-teardown-guard-unit for the portable always-on proof that the unsafe cross-thread teardown pattern aborts deterministically.")
        set_tests_properties(lorie-wayland-teardown-tsan-unavailable PROPERTIES
            PASS_REGULAR_EXPRESSION "TSan unavailable on this host"
        )
    endif()
endif()

# Register with CTest
enable_testing()
add_test(NAME lorie-wayland-unit COMMAND lorie-wayland-tests)
add_test(NAME lorie-wayland-protocols-unit COMMAND lorie-wayland-protocols-tests)
add_test(NAME lorie-wayland-teardown-guard-unit COMMAND lorie-wayland-teardown-guard-tests)
