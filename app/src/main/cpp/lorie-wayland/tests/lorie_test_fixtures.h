#ifndef LORIE_TEST_FIXTURES_H
#define LORIE_TEST_FIXTURES_H

#include "../compositor.h"
#include <string.h>
#include <wayland-util.h>

/*
 * Shared test fixture: a blank but VALID surface for renderer/transform
 * tests that drive lorie_renderer_commit (which walks s->frame_callbacks).
 *
 * Replaces the old `static char dummy_surface[256]` cast pattern, which was
 * broken in two ways once the honest test harness started running these
 * tests to completion:
 *   1. sizeof(struct lorie_surface) > 256, so memset(s, 0, sizeof(*s)) and
 *      field writes overflowed the buffer (fortify abort / OOB writes).
 *   2. A zero-initialized wl_list head has next == NULL, which is NOT a valid
 *      empty list, so lorie_renderer_commit's wl_list_for_each_safe over
 *      frame_callbacks dereferenced a bad pointer (SIGSEGV).
 *
 * Use a real, properly-sized `struct lorie_surface` and pass it here. The
 * surface is zero-initialized and frame_callbacks is made a valid empty list.
 * static inline is safe in a header (pure function, no shared mutable state).
 */
static inline void lorie_test_init_surface(struct lorie_surface *s) {
    memset(s, 0, sizeof(*s));
    wl_list_init(&s->frame_callbacks);
}

#endif /* LORIE_TEST_FIXTURES_H */
