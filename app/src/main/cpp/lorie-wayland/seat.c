#include "input.h"
#include "compositor.h"
#include <stdlib.h>

struct lorie_pointer { struct wl_list link; struct wl_resource *r; };
struct lorie_keyboard { struct wl_list link; struct wl_resource *r; };
struct lorie_touch { struct wl_list link; struct wl_resource *r; };

static void pd(struct wl_resource *res) {
    struct lorie_pointer *p = wl_resource_get_user_data(res);
    if (p) { wl_list_remove(&p->link); free(p); }
}
static void kd(struct wl_resource *res) {
    struct lorie_keyboard *k = wl_resource_get_user_data(res);
    if (k) { wl_list_remove(&k->link); free(k); }
}
static void td(struct wl_resource *res) {
    struct lorie_touch *t = wl_resource_get_user_data(res);
    if (t) { wl_list_remove(&t->link); free(t); }
}

void seat_get_pointer(struct wl_client *c, struct wl_resource *res, uint32_t id) {
    struct lorie_input *in = wl_resource_get_user_data(res);
    struct lorie_pointer *p = calloc(1, sizeof(*p));
    if (!p) { wl_client_post_no_memory(c); return; }
    p->r = wl_resource_create(c, &wl_pointer_interface, wl_resource_get_version(res), id);
    if (!p->r) { free(p); wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(p->r, NULL, p, pd);
    wl_list_insert(&in->pointers, &p->link);
}

void seat_get_keyboard(struct wl_client *c, struct wl_resource *res, uint32_t id) {
    struct lorie_input *in = wl_resource_get_user_data(res);
    struct lorie_keyboard *k = calloc(1, sizeof(*k));
    if (!k) { wl_client_post_no_memory(c); return; }
    k->r = wl_resource_create(c, &wl_keyboard_interface, wl_resource_get_version(res), id);
    if (!k->r) { free(k); wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(k->r, NULL, k, kd);
    wl_list_insert(&in->keyboards, &k->link);
    wl_keyboard_send_keymap(k->r, WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP, -1, 0);
    wl_keyboard_send_repeat_info(k->r, 40, 400);
}

void seat_get_touch(struct wl_client *c, struct wl_resource *res, uint32_t id) {
    struct lorie_input *in = wl_resource_get_user_data(res);
    struct lorie_touch *t = calloc(1, sizeof(*t));
    if (!t) { wl_client_post_no_memory(c); return; }
    t->r = wl_resource_create(c, &wl_touch_interface, wl_resource_get_version(res), id);
    if (!t->r) { free(t); wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(t->r, NULL, t, td);
    wl_list_insert(&in->touches, &t->link);
}
