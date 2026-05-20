#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma ide diagnostic ignored "UnusedParameter"
#pragma ide diagnostic ignored "DanglingPointer"
#pragma ide diagnostic ignored "ConstantConditionsOC"
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#pragma ide diagnostic ignored "UnreachableCode"
#pragma ide diagnostic ignored "OCUnusedMacroInspection"
#pragma ide diagnostic ignored "misc-no-recursion"
#pragma clang diagnostic ignored "-Wincompatible-pointer-types-discards-qualifiers"

#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES

#include "renderer.h"
#include <stdlib.h>
#include <string.h>
#include <media/NdkImageReader.h>

/* ------------------------------------------------------------------ */
/*  Internal surface list (intrusive doubly-linked)                    */
/* ------------------------------------------------------------------ */

/* Frame callback node (singly-linked list) */
typedef struct cb_node {
    lorie_wl_frame_callback cb;
    void* user_data;
    struct cb_node* next;
} cb_node;

typedef struct lorie_wl_surface_entry {
    void* wl_surface;
    LorieBuffer* buffer;

    /* Transform */
    lorie_wl_transform transform;

    /* Damage tracking */
    int damage_x, damage_y, damage_w, damage_h;
    bool damaged;

    /* GL state */
    bool buffer_attached;

    /* Frame callbacks */
    cb_node* callbacks;

    /* Intrusive list links */
    struct lorie_wl_surface_entry* next;
    struct lorie_wl_surface_entry* prev;
} lorie_wl_surface_entry;

typedef struct {
    lorie_wl_surface_entry* head;
    lorie_wl_surface_entry* tail;
    pthread_mutex_t lock;
} surface_list;

static surface_list surfaces = { NULL, NULL, PTHREAD_MUTEX_INITIALIZER };

/* ------------------------------------------------------------------ */
/*  EGL / GLES2 state                                                  */
/* ------------------------------------------------------------------ */

static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLSurface egl_surface = EGL_NO_SURFACE;
static EGLConfig egl_config = 0;

static ANativeWindow* current_win = NULL;
static ANativeWindow* default_win = NULL;
static EGLSurface default_surface = EGL_NO_SURFACE;

static GLuint g_program = 0;
static GLuint g_program_bgra = 0;
static GLint g_u_pos = 0, g_u_coords = 0;
static GLint g_u_pos_bgra = 0, g_u_coords_bgra = 0;

static volatile int filtering = GL_LINEAR; /* LINEAR looks better for scaled surfaces */

static EGLint configAttribs[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8, /* Wayland needs alpha for blending */
    EGL_NONE
};

static const EGLint ctxattribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
};

/* ------------------------------------------------------------------ */
/*  Shader sources (copied from lorie/renderer.c)                      */
/* ------------------------------------------------------------------ */

static const char vertexShaderSrc[] =
    "attribute vec4 position;\n"
    "attribute vec2 texCoords;"
    "varying vec2 outTexCoords;\n"
    "void main(void) {\n"
    "   outTexCoords = texCoords;\n"
    "   gl_Position = position;\n"
    "}\n";

#define FRAGMENT_SHADER(texture) \
    "precision mediump float;\n" \
    "varying vec2 outTexCoords;\n" \
    "uniform sampler2D texture;\n" \
    "void main(void) {\n" \
    "   gl_FragColor = texture2D(texture, outTexCoords)" texture ";\n" \
    "}\n"

static const char fragmentShaderSrc[] = FRAGMENT_SHADER();
static const char fragmentShaderBgraSrc[] = FRAGMENT_SHADER(".bgra");

#undef FRAGMENT_SHADER

/* ------------------------------------------------------------------ */
/*  Helper: EGL error logging                                          */
/* ------------------------------------------------------------------ */

static int printEglError(const char* msg, int line) {
    char descBuf[32] = {0};
    char* desc;
    int err = eglGetError();
    switch(err) {
#define E(code, text) case code: desc = (char*) text; break
        case EGL_SUCCESS: desc = NULL;
        E(EGL_NOT_INITIALIZED, "EGL not initialized or failed to initialize");
        E(EGL_BAD_ACCESS, "Resource inaccessible");
        E(EGL_BAD_ALLOC, "Cannot allocate resources");
        E(EGL_BAD_ATTRIBUTE, "Unrecognized attribute or attribute value");
        E(EGL_BAD_CONTEXT, "Invalid EGL context");
        E(EGL_BAD_CONFIG, "Invalid EGL frame buffer configuration");
        E(EGL_BAD_CURRENT_SURFACE, "Current surface is no longer valid");
        E(EGL_BAD_DISPLAY, "Invalid EGL display");
        E(EGL_BAD_SURFACE, "Invalid surface");
        E(EGL_BAD_MATCH, "Inconsistent arguments");
        E(EGL_BAD_PARAMETER, "Invalid argument");
        E(EGL_BAD_NATIVE_PIXMAP, "Invalid native pixmap");
        E(EGL_BAD_NATIVE_WINDOW, "Invalid native window");
        E(EGL_CONTEXT_LOST, "Context lost");
#undef E
        default:
            snprintf(descBuf, sizeof(descBuf) - 1, "Unknown error (%d)", err);
            desc = descBuf;
    }
    if (desc)
        loge_wl("renderer: %s: %s (%s:%d)\n", msg, desc, __FILE__, line);
    return 0;
}

static void checkGlError(int line) {
    GLenum error;
    char* desc = NULL;
    for (error = glGetError(); error; error = glGetError()) {
        switch (error) {
#define E(code) case code: desc = (char*)#code; break
            E(GL_INVALID_ENUM);
            E(GL_INVALID_VALUE);
            E(GL_INVALID_OPERATION);
            E(GL_STACK_OVERFLOW_KHR);
            E(GL_STACK_UNDERFLOW_KHR);
            E(GL_OUT_OF_MEMORY);
            E(GL_INVALID_FRAMEBUFFER_OPERATION);
            E(GL_CONTEXT_LOST_KHR);
            default: continue;
#undef E
        }
        loge_wl("GLES %d ERROR: %s.\n", line, desc);
        return;
    }
}

#define checkGlError() checkGlError(__LINE__)

/* ------------------------------------------------------------------ */
/*  Shader helpers                                                     */
/* ------------------------------------------------------------------ */

static GLuint loadShader(GLenum shaderType, const char* pSource) {
    GLint compiled = 0, infoLen = 0;
    GLuint shader = glCreateShader(shaderType);
    if (!shader) return 0;
    glShaderSource(shader, 1, &pSource, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled) return shader;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
    if (infoLen) {
        char* buf = malloc(infoLen);
        glGetShaderInfoLog(shader, infoLen, NULL, buf);
        loge_wl("Could not compile shader %d:\n%s\n", shaderType, buf);
        free(buf);
    }
    glDeleteShader(shader);
    return 0;
}

static GLuint createProgram(const char* p_vertex_source, const char* p_fragment_source) {
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, p_vertex_source);
    GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, p_fragment_source);
    if (!pixelShader || !vertexShader) return 0;

    GLuint program = glCreateProgram();
    if (!program) return 0;

    glAttachShader(program, vertexShader);
    glAttachShader(program, pixelShader);
    glLinkProgram(program);

    GLint linkStatus = GL_FALSE, bufLength = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_TRUE) return program;

    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
    if (bufLength) {
        char* buf = malloc(bufLength);
        glGetProgramInfoLog(program, bufLength, NULL, buf);
        loge_wl("Could not link program:\n%s\n", buf);
        free(buf);
    }
    glDeleteProgram(program);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Surface list helpers                                               */
/* ------------------------------------------------------------------ */

static void surface_list_add(lorie_wl_surface_entry* entry) {
    pthread_mutex_lock(&surfaces.lock);
    entry->next = NULL;
    entry->prev = surfaces.tail;
    if (surfaces.tail)
        surfaces.tail->next = entry;
    else
        surfaces.head = entry;
    surfaces.tail = entry;
    pthread_mutex_unlock(&surfaces.lock);
}

static void surface_list_remove(lorie_wl_surface_entry* entry) {
    pthread_mutex_lock(&surfaces.lock);
    if (entry->prev)
        entry->prev->next = entry->next;
    else
        surfaces.head = entry->next;
    if (entry->next)
        entry->next->prev = entry->prev;
    else
        surfaces.tail = entry->prev;
    pthread_mutex_unlock(&surfaces.lock);
}

static void fire_callbacks(lorie_wl_surface_entry* entry) {
    cb_node* node = entry->callbacks;
    entry->callbacks = NULL;
    while (node) {
        cb_node* next = node->next;
        if (node->cb)
            node->cb(node->user_data);
        free(node);
        node = next;
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int lorie_wl_renderer_init(void) {
    EGLint major, minor;
    EGLint numConfigs;
    AImageReader* reader = NULL;

    if (egl_display != EGL_NO_DISPLAY)
        return 0; /* Already initialized */

    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY)
        return printEglError("Got no EGL display", __LINE__);

    if (eglInitialize(egl_display, &major, &minor) != EGL_TRUE)
        return printEglError("Unable to initialize EGL", __LINE__);

    log_wl("Initialized EGL version %d.%d\n", major, minor);
    eglBindAPI(EGL_OPENGL_ES_API);

    if (eglChooseConfig(egl_display, configAttribs, &egl_config, 1, &numConfigs) != EGL_TRUE)
        return printEglError("eglChooseConfig failed", __LINE__);

    egl_context = eglCreateContext(egl_display, egl_config, NULL, ctxattribs);
    if (egl_context == EGL_NO_CONTEXT)
        return printEglError("eglCreateContext failed", __LINE__);

    /* Default 1x1 surface for context validity when no window is attached */
    if (AImageReader_new(1, 1, AIMAGE_FORMAT_RGBA_8888, 2, &reader) != AMEDIA_OK) {
        loge_wl("Failed to initialise ImageReader");
        return -1;
    }
    AImageReader_getWindow(reader, &default_win);
    if (!default_win) {
        loge_wl("Failed to obtain ImageReader native window");
        return -1;
    }
    ANativeWindow_acquire(default_win);

    default_surface = eglCreateWindowSurface(egl_display, egl_config, default_win, NULL);
    egl_surface = default_surface;
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
    eglSwapInterval(egl_display, 0);

    g_program = createProgram(vertexShaderSrc, fragmentShaderSrc);
    g_program_bgra = createProgram(vertexShaderSrc, fragmentShaderBgraSrc);
    if (!g_program || !g_program_bgra) {
        loge_wl("Failed to create shader programs");
        return -1;
    }

    g_u_pos = glGetAttribLocation(g_program, "position");
    g_u_coords = glGetAttribLocation(g_program, "texCoords");
    g_u_pos_bgra = glGetAttribLocation(g_program_bgra, "position");
    g_u_coords_bgra = glGetAttribLocation(g_program_bgra, "texCoords");

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    return 0;
}

void lorie_wl_renderer_fini(void) {
    pthread_mutex_lock(&surfaces.lock);
    lorie_wl_surface_entry* entry = surfaces.head;
    while (entry) {
        lorie_wl_surface_entry* next = entry->next;
        if (entry->buffer)
            LorieBuffer_release(entry->buffer);
        fire_callbacks(entry);
        free(entry);
        entry = next;
    }
    surfaces.head = surfaces.tail = NULL;
    pthread_mutex_unlock(&surfaces.lock);

    if (egl_surface != EGL_NO_SURFACE && egl_surface != default_surface)
        eglDestroySurface(egl_display, egl_surface);
    if (default_surface != EGL_NO_SURFACE)
        eglDestroySurface(egl_display, default_surface);
    if (egl_context != EGL_NO_CONTEXT)
        eglDestroyContext(egl_display, egl_context);
    if (egl_display != EGL_NO_DISPLAY)
        eglTerminate(egl_display);

    if (current_win)
        ANativeWindow_release(current_win);
    if (default_win)
        ANativeWindow_release(default_win);

    egl_display = EGL_NO_DISPLAY;
    egl_context = EGL_NO_CONTEXT;
    egl_surface = EGL_NO_SURFACE;
    egl_config = 0;
    current_win = NULL;
    default_win = NULL;
    default_surface = EGL_NO_SURFACE;
}

void lorie_wl_renderer_set_window(ANativeWindow* window) {
    if (current_win == window)
        return;

    if (egl_surface != EGL_NO_SURFACE && egl_surface != default_surface) {
        eglMakeCurrent(egl_display, default_surface, default_surface, egl_context);
        eglDestroySurface(egl_display, egl_surface);
    }

    if (current_win)
        ANativeWindow_release(current_win);

    current_win = window;
    if (current_win)
        ANativeWindow_acquire(current_win);

    if (current_win) {
        egl_surface = eglCreateWindowSurface(egl_display, egl_config, current_win, NULL);
        if (egl_surface == EGL_NO_SURFACE) {
            printEglError("eglCreateWindowSurface failed", __LINE__);
            egl_surface = default_surface;
            current_win = NULL;
            return;
        }
    } else {
        egl_surface = default_surface;
    }

    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
    eglSwapInterval(egl_display, 0);

    if (current_win) {
        int w = ANativeWindow_getWidth(current_win);
        int h = ANativeWindow_getHeight(current_win);
        glViewport(0, 0, w, h);
    }
}

lorie_wl_surface_entry* lorie_wl_renderer_add_surface(void* wl_surface,
                                                       LorieBuffer* buffer) {
    lorie_wl_surface_entry* entry = calloc(1, sizeof(*entry));
    if (!entry) return NULL;

    entry->wl_surface = wl_surface;
    entry->buffer = buffer;
    if (buffer)
        LorieBuffer_acquire(buffer);

    entry->transform = (lorie_wl_transform){
        .x = 0.0f, .y = 0.0f, .scale_x = 1.0f, .scale_y = 1.0f
    };
    entry->damaged = true; /* force first draw */
    entry->damage_x = entry->damage_y = 0;
    entry->damage_w = entry->damage_h = 0;
    entry->buffer_attached = false;
    entry->callbacks = NULL;

    surface_list_add(entry);
    return entry;
}

void lorie_wl_renderer_remove_surface(lorie_wl_surface_entry* entry) {
    if (!entry) return;
    surface_list_remove(entry);
    if (entry->buffer)
        LorieBuffer_release(entry->buffer);
    fire_callbacks(entry);
    free(entry);
}

void lorie_wl_renderer_set_surface_buffer(lorie_wl_surface_entry* entry,
                                          LorieBuffer* buffer) {
    if (!entry) return;
    if (entry->buffer)
        LorieBuffer_release(entry->buffer);
    entry->buffer = buffer;
    if (buffer)
        LorieBuffer_acquire(buffer);
    entry->buffer_attached = false;
    entry->damaged = true;
}

void lorie_wl_renderer_set_surface_transform(lorie_wl_surface_entry* entry,
                                             const lorie_wl_transform* transform) {
    if (!entry || !transform) return;
    entry->transform = *transform;
    entry->damaged = true;
}

void lorie_wl_renderer_damage_surface(lorie_wl_surface_entry* entry,
                                      int x, int y, int w, int h) {
    if (!entry) return;
    if (!entry->damaged) {
        entry->damage_x = x;
        entry->damage_y = y;
        entry->damage_w = w;
        entry->damage_h = h;
        entry->damaged = true;
    } else {
        /* Grow bounding box to include new damage */
        int x2 = entry->damage_x + entry->damage_w;
        int y2 = entry->damage_y + entry->damage_h;
        int nx2 = x + w;
        int ny2 = y + h;
        entry->damage_x = (x < entry->damage_x) ? x : entry->damage_x;
        entry->damage_y = (y < entry->damage_y) ? y : entry->damage_y;
        entry->damage_w = ((nx2 > x2) ? nx2 : x2) - entry->damage_x;
        entry->damage_h = ((ny2 > y2) ? ny2 : y2) - entry->damage_y;
    }
}

void lorie_wl_renderer_add_frame_callback(lorie_wl_surface_entry* entry,
                                          lorie_wl_frame_callback cb,
                                          void* user_data) {
    if (!entry || !cb) return;
    cb_node* node = malloc(sizeof(*node));
    if (!node) return;
    node->cb = cb;
    node->user_data = user_data;
    node->next = entry->callbacks;
    entry->callbacks = node;
}

void* lorie_wl_surface_get_wl_surface(lorie_wl_surface_entry* entry) {
    return entry ? entry->wl_surface : NULL;
}

LorieBuffer* lorie_wl_surface_get_buffer(lorie_wl_surface_entry* entry) {
    return entry ? entry->buffer : NULL;
}

/* ------------------------------------------------------------------ */
/*  Drawing helpers                                                    */
/* ------------------------------------------------------------------ */

static inline void bindTexture(GLuint id) {
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void drawQuad(float x0, float y0, float x1, float y1,
                     float tx0, float ty0, float tx1, float ty1,
                     bool bgra) {
    float coords[16] = {
        x0, y0, tx0, ty0,
        x1, y0, tx1, ty0,
        x0, y1, tx0, ty1,
        x1, y1, tx1, ty1,
    };

    GLuint p = bgra ? g_u_pos_bgra : g_u_pos;
    GLuint c = bgra ? g_u_coords_bgra : g_u_coords;
    GLuint prog = bgra ? g_program_bgra : g_program;

    glUseProgram(prog);
    glVertexAttribPointer(p, 2, GL_FLOAT, GL_FALSE, 16, coords);
    glVertexAttribPointer(c, 2, GL_FLOAT, GL_FALSE, 16, &coords[2]);
    glEnableVertexAttribArray(p);
    glEnableVertexAttribArray(c);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    checkGlError();
}

static void drawSurface(lorie_wl_surface_entry* entry, int output_w, int output_h) {
    if (!entry->buffer) return;

    const LorieBuffer_Desc* desc = LorieBuffer_description(entry->buffer);
    if (!desc || desc->width <= 0 || desc->height <= 0) return;

    /* Attach buffer to GL exactly once per buffer attachment.
     * LorieBuffer_attachToGL() calls glGenTextures() unconditionally,
     * so calling it multiple times would leak texture IDs. */
    if (!entry->buffer_attached) {
        LorieBuffer_attachToGL(entry->buffer);
        entry->buffer_attached = true;
    }

    LorieBuffer_bindTexture(entry->buffer);
    /* Apply filtering (LorieBuffer_bindTexture doesn't set this) */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Compute surface geometry in normalized device coordinates (-1..1) */
    float surf_w = (float) desc->width * entry->transform.scale_x;
    float surf_h = (float) desc->height * entry->transform.scale_y;

    float x0 = 2.0f * entry->transform.x / output_w - 1.0f;
    float y0 = 1.0f - 2.0f * (entry->transform.y + surf_h) / output_h;
    float x1 = 2.0f * (entry->transform.x + surf_w) / output_w - 1.0f;
    float y1 = 1.0f - 2.0f * entry->transform.y / output_h;

    /* Texture coordinate factor for stride != width (FD-backed buffers) */
    float xfactor = (desc->type == LORIEBUFFER_FD)
                        ? (float) desc->width / (float) desc->stride
                        : 1.0f;

    bool bgra = (desc->format == AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM);

    drawQuad(x0, y0, x1, y1, 0.0f, 1.0f, xfactor, 0.0f, bgra);
}

/* ------------------------------------------------------------------ */
/*  Commit / present                                                   */
/* ------------------------------------------------------------------ */

void lorie_wl_renderer_commit(void) {
    if (egl_surface == EGL_NO_SURFACE || !current_win)
        return;

    int output_w = ANativeWindow_getWidth(current_win);
    int output_h = ANativeWindow_getHeight(current_win);
    if (output_w <= 0 || output_h <= 0)
        return;

    glViewport(0, 0, output_w, output_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    pthread_mutex_lock(&surfaces.lock);

    lorie_wl_surface_entry* entry = surfaces.head;
    while (entry) {
        if (entry->damaged && entry->buffer) {
            drawSurface(entry, output_w, output_h);
        }
        entry = entry->next;
    }

    eglSwapBuffers(egl_display, egl_surface);

    /* Fire frame callbacks and reset damage */
    entry = surfaces.head;
    while (entry) {
        fire_callbacks(entry);
        entry->damaged = false;
        entry = entry->next;
    }

    pthread_mutex_unlock(&surfaces.lock);
}
