#ifndef LORIE_KEYMAP_H
#define LORIE_KEYMAP_H

#include <linux/input-event-codes.h>

/* Shared keycode table: Android keycode → Linux evdev keycode.
 * Defined in keymap.c to avoid static duplication across compilation units.
 */
extern int android_to_linux_keycode[304];

#endif
