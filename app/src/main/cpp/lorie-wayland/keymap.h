#ifndef LORIE_KEYMAP_H
#define LORIE_KEYMAP_H

#include <linux/input-event-codes.h>
#include <stddef.h>

/* Shared keycode table: Android keycode → Linux evdev keycode.
 * Defined in keymap.c to avoid static duplication across compilation units.
 */
extern int android_to_linux_keycode[304];

/* Create a read-only fd containing a minimal US-layout XKB keymap.
 * On success, returns fd >= 0 and writes the size to *out_size.
 * On failure, returns -1.  Caller must close the fd.
 */
int lorie_keymap_create_fd(size_t *out_size);

#endif
