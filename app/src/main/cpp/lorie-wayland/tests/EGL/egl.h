/* Mock EGL/egl.h for host unit tests.
 *
 * The real EGL headers come from the Android NDK / GPU vendor SDKs and are not
 * present on a plain Linux host toolchain. lorie-wayland-tests links against a
 * stub EGL/GLES2 implementation (see test_buffer_stubs.c and friends) so the
 * compositor code can be parsed and exercised without a real GPU. This header
 * only needs to provide the types, macros and function prototypes referenced
 * at compile time by renderer.c / linux-dmabuf.c / the test sources.
 */

#ifndef MOCK_EGL_EGL_H
#define MOCK_EGL_EGL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int EGLBoolean;
typedef int EGLint;
typedef unsigned int EGLenum;
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void *EGLSurface;
typedef void *EGLClientBuffer;
typedef void *EGLImageKHR;
typedef void *NativeWindowType;
typedef void *NativeDisplayType;

#define EGL_FALSE 0
#define EGL_TRUE 1

#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NO_IMAGE_KHR ((EGLImageKHR)0)
#define EGL_DEFAULT_DISPLAY ((NativeDisplayType)0)

#define EGL_NONE 0x3038
#define EGL_SUCCESS 0x3000

#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_SURFACE_TYPE 0x3033
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_WINDOW_BIT 0x0004
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_EXTENSIONS 0x3055

#define EGL_CONTEXT_CLIENT_VERSION 0x3098

/* EGL_EXT_image_dma_buf_import / EGL_KHR_image_base */
#define EGL_LINUX_DMA_BUF_EXT 0x3270
#define EGL_LINUX_DRM_FOURCC_EXT 0x3271
#define EGL_DMA_BUF_PLANE0_FD_EXT 0x3272
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT 0x3273
#define EGL_DMA_BUF_PLANE0_PITCH_EXT 0x3274
#define EGL_DMA_BUF_PLANE1_FD_EXT 0x3275
#define EGL_DMA_BUF_PLANE1_OFFSET_EXT 0x3276
#define EGL_DMA_BUF_PLANE1_PITCH_EXT 0x3277
#define EGL_DMA_BUF_PLANE2_FD_EXT 0x3278
#define EGL_DMA_BUF_PLANE2_OFFSET_EXT 0x3279
#define EGL_DMA_BUF_PLANE2_PITCH_EXT 0x327A
#define EGL_DMA_BUF_PLANE3_FD_EXT 0x3440
#define EGL_DMA_BUF_PLANE3_OFFSET_EXT 0x3441
#define EGL_DMA_BUF_PLANE3_PITCH_EXT 0x3442

EGLDisplay eglGetDisplay(NativeDisplayType display_id);
EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor);
EGLBoolean eglTerminate(EGLDisplay dpy);
EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *configs, EGLint config_size, EGLint *num_config);
EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context, const EGLint *attrib_list);
EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx);
EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  NativeWindowType win, const EGLint *attrib_list);
EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface);
EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval);
EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value);
const char *eglQueryString(EGLDisplay dpy, EGLint name);
void (*eglGetProcAddress(const char *procname))(void);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_EGL_EGL_H */
