#include "renderer.h"
#include "../../lorie/buffer.h"
#include <pthread.h>
#include <android/log.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "compositor.h"

/* === Renderer implementation === */

#define LOG_TAG "lorie-wl-renderer"

static const char vertex_shader_src[] =
    "attribute vec4 position;\n"
    "attribute vec2 texCoords;\n"
    "varying vec2 outTexCoords;\n"
    "uniform mat4 transform;\n"
    "void main(void) {\n"
    "    outTexCoords = texCoords;\n"
    "    gl_Position = transform * position;\n"
    "}\n";

static const char fragment_shader_src[] =
    "precision mediump float;\n"
    "varying vec2 outTexCoords;\n"
    "uniform sampler2D texture;\n"
    "void main(void) {\n"
    "    gl_FragColor = texture2D(texture, outTexCoords);\n"
    "}\n";

static const GLfloat quad_vertices[] = {
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
    -1.0f,  1.0f,  0.0f, 1.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
};

atomic_int lorie_renderer_filtering = ATOMIC_VAR_INIT(GL_NEAREST);

struct renderer_surface {
    struct wl_list link;
    struct lorie_surface *surface;
    int z_index;
    pixman_region32_t accumulated_damage;
    float transform[16];
};

struct lorie_renderer {
    pthread_mutex_t egl_lock;
    pthread_mutex_t surfaces_lock;
    struct wl_list surfaces;
    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLSurface egl_surface;
    EGLConfig egl_config;
    ANativeWindow *current_window;
    GLuint program;
    GLint u_texture, u_transform, a_position, a_texcoords;
    int first_commit;
};

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint compiled;
    glGetShaderiv(s, GL_COMPILE_STATUS, &compiled);
    if (!compiled) { glDeleteShader(s); return 0; }
    return s;
}

static GLuint create_program(const char *vsrc, const char *fsrc) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vsrc);
    if (!vs) return 0;
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fsrc);
    if (!fs) { glDeleteShader(vs); return 0; }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint linked;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) { glDeleteProgram(prog); return 0; }
    return prog;
}

struct lorie_renderer *lorie_renderer_create(void) {
    struct lorie_renderer *r = (struct lorie_renderer*)calloc(1, sizeof(*r));
    if (!r) return NULL;
    pthread_mutex_init(&r->egl_lock, NULL);
    pthread_mutex_init(&r->surfaces_lock, NULL);
    wl_list_init(&r->surfaces);
    r->egl_display = EGL_NO_DISPLAY;
    r->egl_context = EGL_NO_CONTEXT;
    r->egl_surface = EGL_NO_SURFACE;
    r->first_commit = 1;
    return r;
}

void lorie_renderer_destroy(struct lorie_renderer *r) {
    if (!r) return;
    lorie_renderer_fini(r);
    pthread_mutex_destroy(&r->egl_lock);
    pthread_mutex_destroy(&r->surfaces_lock);
    free(r);
}

int lorie_renderer_init(struct lorie_renderer *r) {
    pthread_mutex_lock(&r->egl_lock);
    r->egl_display = eglGetDisplay(NULL);
    if (r->egl_display == EGL_NO_DISPLAY) {
        pthread_mutex_unlock(&r->egl_lock);
        return -1;
    }
    EGLint major, minor;
    if (!eglInitialize(r->egl_display, &major, &minor)) {
        r->egl_display = EGL_NO_DISPLAY;
        pthread_mutex_unlock(&r->egl_lock);
        return -1;
    }
    EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLint n;
    eglChooseConfig(r->egl_display, attribs, &r->egl_config, 1, &n);
    if (n < 1) {
        eglTerminate(r->egl_display);
        r->egl_display = EGL_NO_DISPLAY;
        pthread_mutex_unlock(&r->egl_lock);
        return -1;
    }
    EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    r->egl_context = eglCreateContext(r->egl_display, r->egl_config,
                                       EGL_NO_CONTEXT, ctx_attribs);
    if (r->egl_context == EGL_NO_CONTEXT) {
        eglTerminate(r->egl_display);
        r->egl_display = EGL_NO_DISPLAY;
        pthread_mutex_unlock(&r->egl_lock);
        return -1;
    }
    pthread_mutex_unlock(&r->egl_lock);
    return 0;
}

void lorie_renderer_fini(struct lorie_renderer *r) {
    pthread_mutex_lock(&r->egl_lock);
    if (r->egl_surface != EGL_NO_SURFACE) {
        eglMakeCurrent(r->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(r->egl_display, r->egl_surface);
        r->egl_surface = EGL_NO_SURFACE;
    }
    if (r->egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(r->egl_display, r->egl_context);
        r->egl_context = EGL_NO_CONTEXT;
    }
    if (r->egl_display != EGL_NO_DISPLAY) {
        eglTerminate(r->egl_display);
        r->egl_display = EGL_NO_DISPLAY;
    }
    if (r->current_window) {
        ANativeWindow_release(r->current_window);
        r->current_window = NULL;
    }
    if (r->program) {
        glDeleteProgram(r->program);
        r->program = 0;
    }
    pthread_mutex_unlock(&r->egl_lock);
}

void lorie_renderer_set_window(struct lorie_renderer *r, ANativeWindow *window) {
    pthread_mutex_lock(&r->egl_lock);
    if (r->egl_surface != EGL_NO_SURFACE) {
        eglMakeCurrent(r->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(r->egl_display, r->egl_surface);
        r->egl_surface = EGL_NO_SURFACE;
    }
    if (r->current_window) {
        ANativeWindow_release(r->current_window);
        r->current_window = NULL;
    }
    if (window) {
        r->current_window = window;
        ANativeWindow_acquire(window);
        r->egl_surface = eglCreateWindowSurface(r->egl_display, r->egl_config, window, NULL);
        if (r->egl_surface == EGL_NO_SURFACE) {
            ANativeWindow_release(r->current_window);
            r->current_window = NULL;
            pthread_mutex_unlock(&r->egl_lock);
            return;
        }
        eglMakeCurrent(r->egl_display, r->egl_surface, r->egl_surface, r->egl_context);
        eglSwapInterval(r->egl_display, 1);
        if (!r->program) {
            r->program = create_program(vertex_shader_src, fragment_shader_src);
            r->u_texture = glGetUniformLocation(r->program, "texture");
            r->u_transform = glGetUniformLocation(r->program, "transform");
            r->a_position = glGetAttribLocation(r->program, "position");
            r->a_texcoords = glGetAttribLocation(r->program, "texCoords");
        }
    }
    pthread_mutex_unlock(&r->egl_lock);
}

void lorie_renderer_add_surface(struct lorie_renderer *r, struct lorie_surface *s) {
    pthread_mutex_lock(&r->surfaces_lock);
    struct renderer_surface *rs = (struct renderer_surface*)calloc(1, sizeof(*rs));
    if (rs) {
        rs->surface = s;
        rs->z_index = 0;
        pixman_region32_init(&rs->accumulated_damage);
        wl_list_insert(r->surfaces.prev, &rs->link);
    }
    pthread_mutex_unlock(&r->surfaces_lock);
}

void lorie_renderer_remove_surface(struct lorie_renderer *r, struct lorie_surface *s) {
    pthread_mutex_lock(&r->surfaces_lock);
    struct renderer_surface *rs, *tmp;
    wl_list_for_each_safe(rs, tmp, &r->surfaces, link) {
        if (rs->surface == s) {
            wl_list_remove(&rs->link);
            pixman_region32_fini(&rs->accumulated_damage);
            free(rs);
            break;
        }
    }
    pthread_mutex_unlock(&r->surfaces_lock);
}

void lorie_renderer_damage_surface(struct lorie_renderer *r, struct lorie_surface *s,
                                    int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!r || !s || w <= 0 || h <= 0) return;
    pthread_mutex_lock(&r->surfaces_lock);
    struct renderer_surface *rs;
    wl_list_for_each(rs, &r->surfaces, link) {
        if (rs->surface == s) {
            pixman_region32_union_rect(&rs->accumulated_damage,
                                       &rs->accumulated_damage,
                                       x, y, w, h);
            break;
        }
    }
    pthread_mutex_unlock(&r->surfaces_lock);
}

pixman_region32_t *lorie_renderer_surface_get_damage(struct lorie_renderer *r,
                                                       struct lorie_surface *s) {
    if (!r || !s) return NULL;
    pixman_region32_t *result = NULL;
    pthread_mutex_lock(&r->surfaces_lock);
    struct renderer_surface *rs;
    wl_list_for_each(rs, &r->surfaces, link) {
        if (rs->surface == s) {
            result = &rs->accumulated_damage;
            break;
        }
    }
    pthread_mutex_unlock(&r->surfaces_lock);
    return result;
}

const float *lorie_renderer_surface_get_transform(struct lorie_renderer *r,
                                                    struct lorie_surface *s) {
    if (!r || !s) return NULL;
    const float *result = NULL;
    pthread_mutex_lock(&r->surfaces_lock);
    struct renderer_surface *rs;
    wl_list_for_each(rs, &r->surfaces, link) {
        if (rs->surface == s) {
            result = rs->transform;
            break;
        }
    }
    pthread_mutex_unlock(&r->surfaces_lock);
    return result;
}

static void get_output_size(struct lorie_surface *s, int32_t *out_w, int32_t *out_h) {
    *out_w = 1920;
    *out_h = 1080;
    if (!s->compositor || wl_list_empty(&s->compositor->outputs))
        return;
    struct lorie_output *output =
        wl_container_of(s->compositor->outputs.next, output, link);
    if (output->width > 0 && output->height > 0) {
        *out_w = output->width;
        *out_h = output->height;
    }
}

static void compute_transform_matrix(struct lorie_surface *s, float *M) {
    int32_t out_w, out_h;
    get_output_size(s, &out_w, &out_h);

    float sx = (float)s->logical_width / out_w;
    float sy = (float)s->logical_height / out_h;
    float tx = -1.0f + (float)(2 * s->x + s->logical_width) / out_w;
    float ty = 1.0f - (float)(2 * s->y + s->logical_height) / out_h;

    float cos_r = 1.0f, sin_r = 0.0f;
    int flip_x = 1;

    switch (s->buffer_transform) {
        case WL_OUTPUT_TRANSFORM_NORMAL:
            cos_r = 1.0f; sin_r = 0.0f; flip_x = 1; break;
        case WL_OUTPUT_TRANSFORM_90:
            cos_r = 0.0f; sin_r = 1.0f; flip_x = 1; break;
        case WL_OUTPUT_TRANSFORM_180:
            cos_r = -1.0f; sin_r = 0.0f; flip_x = 1; break;
        case WL_OUTPUT_TRANSFORM_270:
            cos_r = 0.0f; sin_r = -1.0f; flip_x = 1; break;
        case WL_OUTPUT_TRANSFORM_FLIPPED:
            cos_r = 1.0f; sin_r = 0.0f; flip_x = -1; break;
        case WL_OUTPUT_TRANSFORM_FLIPPED_90:
            cos_r = 0.0f; sin_r = 1.0f; flip_x = -1; break;
        case WL_OUTPUT_TRANSFORM_FLIPPED_180:
            cos_r = -1.0f; sin_r = 0.0f; flip_x = -1; break;
        case WL_OUTPUT_TRANSFORM_FLIPPED_270:
            cos_r = 0.0f; sin_r = -1.0f; flip_x = -1; break;
        default:
            cos_r = 1.0f; sin_r = 0.0f; flip_x = 1; break;
    }

    /* M = T * S * R * F  (column-major) */
    float rf00 = cos_r * flip_x;
    float rf01 = -sin_r;
    float rf10 = sin_r * flip_x;
    float rf11 = cos_r;

    float srf00 = sx * rf00;
    float srf01 = sx * rf01;
    float srf10 = sy * rf10;
    float srf11 = sy * rf11;

    M[0]  = srf00;  M[4]  = srf01;  M[8]  = 0.0f;  M[12] = 0.0f;
    M[1]  = srf10;  M[5]  = srf11;  M[9]  = 0.0f;  M[13] = 0.0f;
    M[2]  = 0.0f;   M[6]  = 0.0f;   M[10] = 1.0f;  M[14] = 0.0f;
    M[3]  = tx;     M[7]  = ty;     M[11] = 0.0f;  M[15] = 1.0f;
}

static int cmp_z(const void *a, const void *b) {
    struct renderer_surface * const *ra = (struct renderer_surface * const *)a;
    struct renderer_surface * const *rb = (struct renderer_surface * const *)b;
    return (*ra)->z_index - (*rb)->z_index;
}

int lorie_renderer_commit(struct lorie_renderer *r) {
    struct renderer_surface *rs;
    int count = 0;
    pthread_mutex_lock(&r->surfaces_lock);
    wl_list_for_each(rs, &r->surfaces, link) count++;
    struct renderer_surface *sorted[64];
    int i = 0;
    wl_list_for_each(rs, &r->surfaces, link) {
        if (i < 64) sorted[i++] = rs;
    }
    pthread_mutex_unlock(&r->surfaces_lock);

    if (count > 1) qsort(sorted, count, sizeof(sorted[0]), cmp_z);

    /* Compute per-surface transform matrices before EGL lock */
    for (int j = 0; j < count; j++) {
        struct renderer_surface *rs = sorted[j];
        struct lorie_surface *s = rs->surface;
        if (s) {
            compute_transform_matrix(s, rs->transform);
        }
    }

    /* Fire frame callbacks BEFORE acquiring EGL lock */
    for (int j = 0; j < count; j++) {
        struct lorie_surface *s = sorted[j]->surface;
        if (s) {
            struct lorie_frame_callback *cb, *cb_tmp;
            wl_list_for_each_safe(cb, cb_tmp, &s->frame_callbacks, link) {
                wl_callback_send_done(cb->resource, 0);
                wl_resource_destroy(cb->resource);
            }
            wl_list_init(&s->frame_callbacks);
        }
    }

    pthread_mutex_lock(&r->egl_lock);
    if (r->egl_surface == EGL_NO_SURFACE || r->egl_display == EGL_NO_DISPLAY) {
        pthread_mutex_unlock(&r->egl_lock);
        return -1;
    }
    eglMakeCurrent(r->egl_display, r->egl_surface, r->egl_surface, r->egl_context);

    if (r->first_commit) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        r->first_commit = 0;
    }

    if (r->program) {
        glUseProgram(r->program);
        glUniform1i(r->u_texture, 0);
        glVertexAttribPointer(r->a_position, 2, GL_FLOAT, GL_FALSE,
                              4 * sizeof(GLfloat), quad_vertices);
        glVertexAttribPointer(r->a_texcoords, 2, GL_FLOAT, GL_FALSE,
                              4 * sizeof(GLfloat), quad_vertices + 2);
        glEnableVertexAttribArray(r->a_position);
        glEnableVertexAttribArray(r->a_texcoords);
        glActiveTexture(GL_TEXTURE0);

        for (int j = 0; j < count; j++) {
            struct renderer_surface *rs = sorted[j];
            struct lorie_surface *s = rs->surface;
            if (!pixman_region32_not_empty(&rs->accumulated_damage)) {
                continue;
            }
            if (s && s->buffer) {
                LorieBuffer *lb = (LorieBuffer*)s->buffer;
                LorieBuffer_attachToGL(lb);
                LorieBuffer_bindTexture(lb);
            } else if (s && s->buffer_resource) {
                /* Non-SHM buffer attached but not yet imported — draw placeholder */
            } else {
                continue;
            }
            pixman_box32_t *bbox = pixman_region32_extents(&rs->accumulated_damage);
            glUniformMatrix4fv(r->u_transform, 1, GL_FALSE, rs->transform);
            glEnable(GL_SCISSOR_TEST);
            glScissor(bbox->x1, bbox->y1,
                      bbox->x2 - bbox->x1, bbox->y2 - bbox->y1);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glDisable(GL_SCISSOR_TEST);
            pixman_region32_clear(&rs->accumulated_damage);
        }

        glDisableVertexAttribArray(r->a_position);
        glDisableVertexAttribArray(r->a_texcoords);
    }

    eglSwapBuffers(r->egl_display, r->egl_surface);
    pthread_mutex_unlock(&r->egl_lock);
    return 0;
}
