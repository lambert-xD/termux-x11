#ifndef LORIE_RENDERER_H
#define LORIE_RENDERER_H

#include <android/native_window.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <pixman.h>

struct lorie_surface;

struct lorie_renderer;

struct lorie_renderer *lorie_renderer_create(void);
void lorie_renderer_destroy(struct lorie_renderer *r);
int lorie_renderer_init(struct lorie_renderer *r);
void lorie_renderer_fini(struct lorie_renderer *r);

void lorie_renderer_set_window(struct lorie_renderer *r, ANativeWindow *window);
void lorie_renderer_add_surface(struct lorie_renderer *r, struct lorie_surface *s);
void lorie_renderer_remove_surface(struct lorie_renderer *r, struct lorie_surface *s);
void lorie_renderer_damage_surface(struct lorie_renderer *r, struct lorie_surface *s,
                                    int32_t x, int32_t y, int32_t w, int32_t h);
int lorie_renderer_commit(struct lorie_renderer *r);

/* Test helpers */
pixman_region32_t *lorie_renderer_surface_get_damage(struct lorie_renderer *r,
                                                       struct lorie_surface *s);
const float *lorie_renderer_surface_get_transform(struct lorie_renderer *r,
                                                    struct lorie_surface *s);
int lorie_renderer_is_first_commit(struct lorie_renderer *r);
int lorie_renderer_surface_was_drawn(struct lorie_renderer *r,
                                       struct lorie_surface *s);
int lorie_renderer_surface_count(struct lorie_renderer *r);

/* DMA-BUF import helpers */
int lorie_renderer_has_dmabuf_import(struct lorie_renderer *r);
void* lorie_renderer_egl_display(struct lorie_renderer *r);
void* lorie_renderer_egl_create_image_khr(struct lorie_renderer *r);
void* lorie_renderer_egl_destroy_image_khr(struct lorie_renderer *r);
void* lorie_renderer_gl_egl_image_target_texture2d_oes(struct lorie_renderer *r);

extern atomic_int lorie_renderer_filtering;

#endif /* LORIE_RENDERER_H */
