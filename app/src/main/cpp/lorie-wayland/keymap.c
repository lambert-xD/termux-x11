#include "keymap.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Minimal US-layout XKB keymap string (~2 KB).
 * Self-contained: no include directives, no xkbcommon data files needed.
 * Covers the keycodes mapped in android_to_linux_keycode[].
 */
static const char lorie_xkb_keymap[] =
"xkb_keymap {\n"
"xkb_keycodes \"evdev\" { minimum=8; maximum=255;\n"
"<ESC>=9; <AE01>=10; <AE02>=11; <AE03>=12; <AE04>=13; <AE05>=14; <AE06>=15; <AE07>=16; <AE08>=17; <AE09>=18; <AE10>=19; <AE11>=20; <AE12>=21; <BKSP>=22; <TAB>=23;\n"
"<AD01>=24; <AD02>=25; <AD03>=26; <AD04>=27; <AD05>=28; <AD06>=29; <AD07>=30; <AD08>=31; <AD09>=32; <AD10>=33; <AD11>=34; <AD12>=35; <RTRN>=36; <LCTL>=37;\n"
"<AC01>=38; <AC02>=39; <AC03>=40; <AC04>=41; <AC05>=42; <AC06>=43; <AC07>=44; <AC08>=45; <AC09>=46; <AC10>=47; <AC11>=48; <TLDE>=49; <LFSH>=50; <BKSL>=51;\n"
"<AB01>=52; <AB02>=53; <AB03>=54; <AB04>=55; <AB05>=56; <AB06>=57; <AB07>=58; <AB08>=59; <AB09>=60; <AB10>=61; <RTSH>=62; <LALT>=64; <SPCE>=65; <CAPS>=66;\n"
"<FK01>=67; <FK02>=68; <FK03>=69; <FK04>=70; <FK05>=71; <FK06>=72; <FK07>=73; <FK08>=74; <FK09>=75; <FK10>=76; <NMLK>=77; <SCLK>=78;\n"
"<KP7>=79; <KP8>=80; <KP9>=81; <KPSU>=82; <KP4>=83; <KP5>=84; <KP6>=85; <KPAD>=86; <KP1>=87; <KP2>=88; <KP3>=89; <KP0>=90; <KPDL>=91;\n"
"<FK11>=95; <FK12>=96; <RCTL>=105; <RALT>=108; <HOME>=110; <UP>=111; <PGUP>=112; <LEFT>=113; <RGHT>=114; <END>=115; <DOWN>=116; <PGDN>=117; <INS>=118; <DELE>=119;\n"
"<LWIN>=133; <RWIN>=134; <COMP>=135; <MENU>=143; };\n"
"xkb_types \"complete\" { type \"ONE_LEVEL\" { modifiers=none; map[none]=1; }; type \"TWO_LEVEL\" { modifiers=Shift; map[none]=1; map[Shift]=2; }; };\n"
"xkb_compat \"complete\" { };\n"
"xkb_symbols \"us\" {\n"
" key <ESC> { [Escape] }; key <AE01> { [1,exclam] }; key <AE02> { [2,at] }; key <AE03> { [3,numbersign] }; key <AE04> { [4,dollar] }; key <AE05> { [5,percent] };\n"
" key <AE06> { [6,asciicircum] }; key <AE07> { [7,ampersand] }; key <AE08> { [8,asterisk] }; key <AE09> { [9,parenleft] }; key <AE10> { [0,parenright] };\n"
" key <AE11> { [minus,underscore] }; key <AE12> { [equal,plus] }; key <BKSP> { [BackSpace] }; key <TAB> { [Tab,ISO_Left_Tab] };\n"
" key <AD01> { [q,Q] }; key <AD02> { [w,W] }; key <AD03> { [e,E] }; key <AD04> { [r,R] }; key <AD05> { [t,T] }; key <AD06> { [y,Y] };\n"
" key <AD07> { [u,U] }; key <AD08> { [i,I] }; key <AD09> { [o,O] }; key <AD10> { [p,P] }; key <AD11> { [bracketleft,braceleft] }; key <AD12> { [bracketright,braceright] };\n"
" key <RTRN> { [Return] }; key <LCTL> { [Control_L] }; key <AC01> { [a,A] }; key <AC02> { [s,S] }; key <AC03> { [d,D] }; key <AC04> { [f,F] };\n"
" key <AC05> { [g,G] }; key <AC06> { [h,H] }; key <AC07> { [j,J] }; key <AC08> { [k,K] }; key <AC09> { [l,L] }; key <AC10> { [semicolon,colon] };\n"
" key <AC11> { [apostrophe,quotedbl] }; key <TLDE> { [grave,asciitilde] }; key <LFSH> { [Shift_L] }; key <BKSL> { [backslash,bar] };\n"
" key <AB01> { [z,Z] }; key <AB02> { [x,X] }; key <AB03> { [c,C] }; key <AB04> { [v,V] }; key <AB05> { [b,B] }; key <AB06> { [n,N] };\n"
" key <AB07> { [m,M] }; key <AB08> { [comma,less] }; key <AB09> { [period,greater] }; key <AB10> { [slash,question] }; key <RTSH> { [Shift_R] };\n"
" key <LALT> { [Alt_L] }; key <SPCE> { [space] }; key <CAPS> { [Caps_Lock] }; key <FK01> { [F1] }; key <FK02> { [F2] }; key <FK03> { [F3] };\n"
" key <FK04> { [F4] }; key <FK05> { [F5] }; key <FK06> { [F6] }; key <FK07> { [F7] }; key <FK08> { [F8] }; key <FK09> { [F9] }; key <FK10> { [F10] };\n"
" key <NMLK> { [Num_Lock] }; key <SCLK> { [Scroll_Lock] }; key <KP7> { [KP_7] }; key <KP8> { [KP_8] }; key <KP9> { [KP_9] }; key <KPSU> { [KP_Subtract] };\n"
" key <KP4> { [KP_4] }; key <KP5> { [KP_5] }; key <KP6> { [KP_6] }; key <KPAD> { [KP_Add] }; key <KP1> { [KP_1] }; key <KP2> { [KP_2] };\n"
" key <KP3> { [KP_3] }; key <KP0> { [KP_0] }; key <KPDL> { [KP_Decimal] }; key <FK11> { [F11] }; key <FK12> { [F12] };\n"
" key <RCTL> { [Control_R] }; key <RALT> { [Alt_R] }; key <HOME> { [Home] }; key <UP> { [Up] }; key <PGUP> { [Prior] };\n"
" key <LEFT> { [Left] }; key <RGHT> { [Right] }; key <END> { [End] }; key <DOWN> { [Down] }; key <PGDN> { [Next] };\n"
" key <INS> { [Insert] }; key <DELE> { [Delete] }; key <LWIN> { [Super_L] }; key <RWIN> { [Super_R] }; key <MENU> { [Menu] };\n"
"};\n"
"};\n";

int lorie_keymap_create_fd(size_t *out_size) {
    if (!out_size) return -1;
    size_t len = strlen(lorie_xkb_keymap);
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";
    char path[256];
    int n = snprintf(path, sizeof(path), "%s/lorie-keymap-XXXXXX", tmpdir);
    if (n < 0 || (size_t)n >= sizeof(path)) return -1;
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) { close(fd); return -1; }
    (void)unlink(path);
    ssize_t written = write(fd, lorie_xkb_keymap, len);
    if (written != (ssize_t)len) { close(fd); return -1; }
    if (lseek(fd, 0, SEEK_SET) != 0) { close(fd); return -1; }
    *out_size = len;
    return fd;
}

int android_to_linux_keycode[304] = {
    [ 4   /* ANDROID_KEYCODE_BACK */] = KEY_ESC,
    [ 7   /* ANDROID_KEYCODE_0 */] = KEY_0,
    [ 8   /* ANDROID_KEYCODE_1 */] = KEY_1,
    [ 9   /* ANDROID_KEYCODE_2 */] = KEY_2,
    [ 10  /* ANDROID_KEYCODE_3 */] = KEY_3,
    [ 11  /* ANDROID_KEYCODE_4 */] = KEY_4,
    [ 12  /* ANDROID_KEYCODE_5 */] = KEY_5,
    [ 13  /* ANDROID_KEYCODE_6 */] = KEY_6,
    [ 14  /* ANDROID_KEYCODE_7 */] = KEY_7,
    [ 15  /* ANDROID_KEYCODE_8 */] = KEY_8,
    [ 16  /* ANDROID_KEYCODE_9 */] = KEY_9,
    [ 17  /* ANDROID_KEYCODE_STAR */] = KEY_KPASTERISK,
    [ 19  /* ANDROID_KEYCODE_DPAD_UP */] = KEY_UP,
    [ 20  /* ANDROID_KEYCODE_DPAD_DOWN */] = KEY_DOWN,
    [ 21  /* ANDROID_KEYCODE_DPAD_LEFT */] = KEY_LEFT,
    [ 22  /* ANDROID_KEYCODE_DPAD_RIGHT */] = KEY_RIGHT,
    [ 23  /* ANDROID_KEYCODE_DPAD_CENTER */] = KEY_ENTER,
    [ 24  /* ANDROID_KEYCODE_VOLUME_UP */] = KEY_VOLUMEUP,
    [ 25  /* ANDROID_KEYCODE_VOLUME_DOWN */] = KEY_VOLUMEDOWN,
    [ 26  /* ANDROID_KEYCODE_POWER */] = KEY_POWER,
    [ 27  /* ANDROID_KEYCODE_CAMERA */] = KEY_CAMERA,
    [ 28  /* ANDROID_KEYCODE_CLEAR */] = KEY_CLEAR,
    [ 29  /* ANDROID_KEYCODE_A */] = KEY_A,
    [ 30  /* ANDROID_KEYCODE_B */] = KEY_B,
    [ 31  /* ANDROID_KEYCODE_C */] = KEY_C,
    [ 32  /* ANDROID_KEYCODE_D */] = KEY_D,
    [ 33  /* ANDROID_KEYCODE_E */] = KEY_E,
    [ 34  /* ANDROID_KEYCODE_F */] = KEY_F,
    [ 35  /* ANDROID_KEYCODE_G */] = KEY_G,
    [ 36  /* ANDROID_KEYCODE_H */] = KEY_H,
    [ 37  /* ANDROID_KEYCODE_I */] = KEY_I,
    [ 38  /* ANDROID_KEYCODE_J */] = KEY_J,
    [ 39  /* ANDROID_KEYCODE_K */] = KEY_K,
    [ 40  /* ANDROID_KEYCODE_L */] = KEY_L,
    [ 41  /* ANDROID_KEYCODE_M */] = KEY_M,
    [ 42  /* ANDROID_KEYCODE_N */] = KEY_N,
    [ 43  /* ANDROID_KEYCODE_O */] = KEY_O,
    [ 44  /* ANDROID_KEYCODE_P */] = KEY_P,
    [ 45  /* ANDROID_KEYCODE_Q */] = KEY_Q,
    [ 46  /* ANDROID_KEYCODE_R */] = KEY_R,
    [ 47  /* ANDROID_KEYCODE_S */] = KEY_S,
    [ 48  /* ANDROID_KEYCODE_T */] = KEY_T,
    [ 49  /* ANDROID_KEYCODE_U */] = KEY_U,
    [ 50  /* ANDROID_KEYCODE_V */] = KEY_V,
    [ 51  /* ANDROID_KEYCODE_W */] = KEY_W,
    [ 52  /* ANDROID_KEYCODE_X */] = KEY_X,
    [ 53  /* ANDROID_KEYCODE_Y */] = KEY_Y,
    [ 54  /* ANDROID_KEYCODE_Z */] = KEY_Z,
    [ 55  /* ANDROID_KEYCODE_COMMA */] = KEY_COMMA,
    [ 56  /* ANDROID_KEYCODE_PERIOD */] = KEY_DOT,
    [ 57  /* ANDROID_KEYCODE_ALT_LEFT */] = KEY_LEFTALT,
    [ 58  /* ANDROID_KEYCODE_ALT_RIGHT */] = KEY_RIGHTALT,
    [ 59  /* ANDROID_KEYCODE_SHIFT_LEFT */] = KEY_LEFTSHIFT,
    [ 60  /* ANDROID_KEYCODE_SHIFT_RIGHT */] = KEY_RIGHTSHIFT,
    [ 61  /* ANDROID_KEYCODE_TAB */] = KEY_TAB,
    [ 62  /* ANDROID_KEYCODE_SPACE */] = KEY_SPACE,
    [ 64  /* ANDROID_KEYCODE_EXPLORER */] = KEY_WWW,
    [ 65  /* ANDROID_KEYCODE_ENVELOPE */] = KEY_MAIL,
    [ 66  /* ANDROID_KEYCODE_ENTER */] = KEY_ENTER,
    [ 67  /* ANDROID_KEYCODE_DEL */] = KEY_BACKSPACE,
    [ 68  /* ANDROID_KEYCODE_GRAVE */] = KEY_GRAVE,
    [ 69  /* ANDROID_KEYCODE_MINUS */] = KEY_MINUS,
    [ 70  /* ANDROID_KEYCODE_EQUALS */] = KEY_EQUAL,
    [ 71  /* ANDROID_KEYCODE_LEFT_BRACKET */] = KEY_LEFTBRACE,
    [ 72  /* ANDROID_KEYCODE_RIGHT_BRACKET */] = KEY_RIGHTBRACE,
    [ 73  /* ANDROID_KEYCODE_BACKSLASH */] = KEY_BACKSLASH,
    [ 74  /* ANDROID_KEYCODE_SEMICOLON */] = KEY_SEMICOLON,
    [ 75  /* ANDROID_KEYCODE_APOSTROPHE */] = KEY_APOSTROPHE,
    [ 76  /* ANDROID_KEYCODE_SLASH */] = KEY_SLASH,
    [ 81  /* ANDROID_KEYCODE_PLUS */] = KEY_KPPLUS,
    [ 82  /* ANDROID_KEYCODE_MENU */] = KEY_CONTEXT_MENU,
    [ 84  /* ANDROID_KEYCODE_SEARCH */] = KEY_SEARCH,
    [ 85  /* ANDROID_KEYCODE_MEDIA_PLAY_PAUSE */] = KEY_PLAYPAUSE,
    [ 86  /* ANDROID_KEYCODE_MEDIA_STOP */] = KEY_STOP_RECORD,
    [ 87  /* ANDROID_KEYCODE_MEDIA_NEXT */] = KEY_NEXTSONG,
    [ 88  /* ANDROID_KEYCODE_MEDIA_PREVIOUS */] = KEY_PREVIOUSSONG,
    [ 89  /* ANDROID_KEYCODE_MEDIA_REWIND */] = KEY_REWIND,
    [ 90  /* ANDROID_KEYCODE_MEDIA_FAST_FORWARD */] = KEY_FASTFORWARD,
    [ 91  /* ANDROID_KEYCODE_MUTE */] = KEY_MUTE,
    [ 92  /* ANDROID_KEYCODE_PAGE_UP */] = KEY_PAGEUP,
    [ 93  /* ANDROID_KEYCODE_PAGE_DOWN */] = KEY_PAGEDOWN,
    [ 111  /* ANDROID_KEYCODE_ESCAPE */] = KEY_ESC,
    [ 112  /* ANDROID_KEYCODE_FORWARD_DEL */] = KEY_DELETE,
    [ 113  /* ANDROID_KEYCODE_CTRL_LEFT */] = KEY_LEFTCTRL,
    [ 114  /* ANDROID_KEYCODE_CTRL_RIGHT */] = KEY_RIGHTCTRL,
    [ 115  /* ANDROID_KEYCODE_CAPS_LOCK */] = KEY_CAPSLOCK,
    [ 116  /* ANDROID_KEYCODE_SCROLL_LOCK */] = KEY_SCROLLLOCK,
    [ 117  /* ANDROID_KEYCODE_META_LEFT */] = KEY_LEFTMETA,
    [ 118  /* ANDROID_KEYCODE_META_RIGHT */] = KEY_RIGHTMETA,
    [ 120  /* ANDROID_KEYCODE_SYSRQ */] = KEY_PRINT,
    [ 121  /* ANDROID_KEYCODE_BREAK */] = KEY_BREAK,
    [ 122  /* ANDROID_KEYCODE_MOVE_HOME */] = KEY_HOME,
    [ 123  /* ANDROID_KEYCODE_MOVE_END */] = KEY_END,
    [ 124  /* ANDROID_KEYCODE_INSERT */] = KEY_INSERT,
    [ 125  /* ANDROID_KEYCODE_FORWARD */] = KEY_FORWARD,
    [ 126  /* ANDROID_KEYCODE_MEDIA_PLAY */] = KEY_PLAYCD,
    [ 127  /* ANDROID_KEYCODE_MEDIA_PAUSE */] = KEY_PAUSECD,
    [ 128  /* ANDROID_KEYCODE_MEDIA_CLOSE */] = KEY_CLOSECD,
    [ 129  /* ANDROID_KEYCODE_MEDIA_EJECT */] = KEY_EJECTCD,
    [ 130  /* ANDROID_KEYCODE_MEDIA_RECORD */] = KEY_RECORD,
    [ 131  /* ANDROID_KEYCODE_F1 */] = KEY_F1,
    [ 132  /* ANDROID_KEYCODE_F2 */] = KEY_F2,
    [ 133  /* ANDROID_KEYCODE_F3 */] = KEY_F3,
    [ 134  /* ANDROID_KEYCODE_F4 */] = KEY_F4,
    [ 135  /* ANDROID_KEYCODE_F5 */] = KEY_F5,
    [ 136  /* ANDROID_KEYCODE_F6 */] = KEY_F6,
    [ 137  /* ANDROID_KEYCODE_F7 */] = KEY_F7,
    [ 138  /* ANDROID_KEYCODE_F8 */] = KEY_F8,
    [ 139  /* ANDROID_KEYCODE_F9 */] = KEY_F9,
    [ 140  /* ANDROID_KEYCODE_F10 */] = KEY_F10,
    [ 141  /* ANDROID_KEYCODE_F11 */] = KEY_F11,
    [ 142  /* ANDROID_KEYCODE_F12 */] = KEY_F12,
    [ 143  /* ANDROID_KEYCODE_NUM_LOCK */] = KEY_NUMLOCK,
    [ 144  /* ANDROID_KEYCODE_NUMPAD_0 */] = KEY_KP0,
    [ 145  /* ANDROID_KEYCODE_NUMPAD_1 */] = KEY_KP1,
    [ 146  /* ANDROID_KEYCODE_NUMPAD_2 */] = KEY_KP2,
    [ 147  /* ANDROID_KEYCODE_NUMPAD_3 */] = KEY_KP3,
    [ 148  /* ANDROID_KEYCODE_NUMPAD_4 */] = KEY_KP4,
    [ 149  /* ANDROID_KEYCODE_NUMPAD_5 */] = KEY_KP5,
    [ 150  /* ANDROID_KEYCODE_NUMPAD_6 */] = KEY_KP6,
    [ 151  /* ANDROID_KEYCODE_NUMPAD_7 */] = KEY_KP7,
    [ 152  /* ANDROID_KEYCODE_NUMPAD_8 */] = KEY_KP8,
    [ 153  /* ANDROID_KEYCODE_NUMPAD_9 */] = KEY_KP9,
    [ 154  /* ANDROID_KEYCODE_NUMPAD_DIVIDE */] = KEY_KPSLASH,
    [ 155  /* ANDROID_KEYCODE_NUMPAD_MULTIPLY */] = KEY_KPASTERISK,
    [ 156  /* ANDROID_KEYCODE_NUMPAD_SUBTRACT */] = KEY_KPMINUS,
    [ 157  /* ANDROID_KEYCODE_NUMPAD_ADD */] = KEY_KPPLUS,
    [ 158  /* ANDROID_KEYCODE_NUMPAD_DOT */] = KEY_KPDOT,
    [ 159  /* ANDROID_KEYCODE_NUMPAD_COMMA */] = KEY_KPCOMMA,
    [ 160  /* ANDROID_KEYCODE_NUMPAD_ENTER */] = KEY_KPENTER,
    [ 161  /* ANDROID_KEYCODE_NUMPAD_EQUALS */] = KEY_KPEQUAL,
    [ 162  /* ANDROID_KEYCODE_NUMPAD_LEFT_PAREN */] = KEY_KPLEFTPAREN,
    [ 163  /* ANDROID_KEYCODE_NUMPAD_RIGHT_PAREN */] = KEY_KPRIGHTPAREN,
    [ 164  /* ANDROID_KEYCODE_VOLUME_MUTE */] = KEY_MUTE,
    [ 165  /* ANDROID_KEYCODE_INFO */] = KEY_INFO,
    [ 166  /* ANDROID_KEYCODE_CHANNEL_UP */] = KEY_CHANNELUP,
    [ 167  /* ANDROID_KEYCODE_CHANNEL_DOWN */] = KEY_CHANNELDOWN,
    [ 168  /* ANDROID_KEYCODE_ZOOM_IN */] = KEY_ZOOMIN,
    [ 169  /* ANDROID_KEYCODE_ZOOM_OUT */] = KEY_ZOOMOUT,
    [ 170  /* ANDROID_KEYCODE_TV */] = KEY_TV,
    [ 208  /* ANDROID_KEYCODE_CALENDAR */] = KEY_CALENDAR,
    [ 210  /* ANDROID_KEYCODE_CALCULATOR */] = KEY_CALC,
};
