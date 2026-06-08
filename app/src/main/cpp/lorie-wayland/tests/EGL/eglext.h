/* Mock EGL/eglext.h for host unit tests.
 *
 * All the KHR/EXT types and macros referenced by this codebase
 * (EGLImageKHR, EGL_NO_IMAGE_KHR, EGL_LINUX_DMA_BUF_EXT, ...) are already
 * provided by our EGL/egl.h mock, so this header simply re-includes it for
 * source compatibility with `#include <EGL/eglext.h>`.
 */

#ifndef MOCK_EGL_EGLEXT_H
#define MOCK_EGL_EGLEXT_H

#include <EGL/egl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard Khronos function-pointer typedefs for extensions resolved at
 * runtime via eglGetProcAddress(). */
typedef EGLImageKHR (*PFNEGLCREATEIMAGEKHRPROC)(EGLDisplay dpy, EGLContext ctx,
                                                EGLenum target, EGLClientBuffer buffer,
                                                const EGLint *attrib_list);
typedef EGLBoolean (*PFNEGLDESTROYIMAGEKHRPROC)(EGLDisplay dpy, EGLImageKHR image);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_EGL_EGLEXT_H */
