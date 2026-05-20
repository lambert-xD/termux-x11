#include "renderer.h"
#include "../../lorie/buffer.h"
#include <pthread.h>
#include <android/log.h>
#include <stdlib.h>

/* === NDK stubs for standalone compilation === */
typedef void *EGLDisplay; typedef void *EGLContext;
typedef void *EGLSurface; typedef void *EGLConfig;
typedef unsigned int EGLBoolean; typedef int EGLint;
typedef unsigned int GLenum; typedef unsigned int GLuint;
typedef int GLint; typedef unsigned char GLboolean;
typedef float GLfloat; typedef int GLsizei;
typedef unsigned int GLbitfield;
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_FALSE 0
#define EGL_NONE 0x3038
#define EGL_SURFACE_TYPE 0x3025
#define EGL_WINDOW_BIT 0x0004
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TEXTURE0 0x84C0
#define GL_TRIANGLE_STRIP 0x0005
#define GL_FLOAT 0x1406
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_NEAREST 0x2600
#define GL_TRUE 1
#define GL_FALSE 0

extern EGLDisplay eglGetDisplay(void*);
extern EGLBoolean eglInitialize(EGLDisplay, EGLint*, EGLint*);
extern EGLBoolean eglTerminate(EGLDisplay);
extern EGLBoolean eglChooseConfig(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
extern EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
extern EGLBoolean eglDestroyContext(EGLDisplay, EGLContext);
extern EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, void*, const EGLint*);
extern EGLBoolean eglDestroySurface(EGLDisplay, EGLSurface);
extern EGLBoolean eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
extern EGLBoolean eglSwapBuffers(EGLDisplay, EGLSurface);
extern EGLBoolean eglSwapInterval(EGLDisplay, EGLint);

extern GLuint glCreateShader(GLenum);
extern void glShaderSource(GLuint, GLsizei, const char*const*, const GLint*);
extern void glCompileShader(GLuint);
extern void glGetShaderiv(GLuint, GLenum, GLint*);
extern void glDeleteShader(GLuint);
extern GLuint glCreateProgram(void);
extern void glAttachShader(GLuint, GLuint);
extern void glLinkProgram(GLuint);
extern void glGetProgramiv(GLuint, GLenum, GLint*);
extern void glDeleteProgram(GLuint);
extern void glUseProgram(GLuint);
extern GLint glGetUniformLocation(GLuint, const char*);
extern GLint glGetAttribLocation(GLuint, const char*);
extern void glUniform1i(GLint, GLint);
extern void glVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
extern void glEnableVertexAttribArray(GLuint);
extern void glDisableVertexAttribArray(GLuint);
extern void glActiveTexture(GLenum);
extern void glDrawArrays(GLenum, GLint, GLsizei);
extern void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat);
extern void glClear(GLbitfield);

#include "compositor.h"

/* === Renderer implementation === */

#define LOG_TAG "lorie-wl-renderer"

static const char vertex_shader_src[] =
    "attribute vec4 position;\n"
    "attribute vec2 texCoords;\n"
    "varying vec2 outTexCoords;\n"
    "void main(void) {\n"
    "    outTexCoords = texCoords;\n"
    "    gl_Position = position;\n"
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
    GLint u_texture, a_position, a_texcoords;
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
            free(rs);
            break;
        }
    }
    pthread_mutex_unlock(&r->surfaces_lock);
}

void lorie_renderer_damage_surface(struct lorie_renderer *r, struct lorie_surface *s,
                                    int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)r; (void)s; (void)x; (void)y; (void)w; (void)h;
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

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

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
            struct lorie_surface *s = sorted[j]->surface;
            if (s && s->buffer) {
                LorieBuffer *lb = (LorieBuffer*)s->buffer;
                LorieBuffer_attachToGL(lb);
                LorieBuffer_bindTexture(lb);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            } else if (s && s->buffer_resource) {
                /* Non-SHM buffer attached but not yet imported — draw placeholder */
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }
        }

        glDisableVertexAttribArray(r->a_position);
        glDisableVertexAttribArray(r->a_texcoords);
    }

    eglSwapBuffers(r->egl_display, r->egl_surface);
    pthread_mutex_unlock(&r->egl_lock);
    return 0;
}
