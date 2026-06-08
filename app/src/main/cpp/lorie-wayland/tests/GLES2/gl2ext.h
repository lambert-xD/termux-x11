/* Mock GLES2/gl2ext.h for host unit tests.
 *
 * No OES/EXT symbols from this header are referenced directly by the
 * compositor sources at compile time (glEGLImageTargetTexture2DOES is loaded
 * dynamically via eglGetProcAddress and stored as a function pointer), so
 * this mock only needs to satisfy `#include <GLES2/gl2ext.h>`.
 */

#ifndef MOCK_GLES2_GL2EXT_H
#define MOCK_GLES2_GL2EXT_H

#include <GLES2/gl2.h>
#include <EGL/egl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *GLeglImageOES;
typedef void (*PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)(GLenum target, GLeglImageOES image);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_GLES2_GL2EXT_H */
