/* Mock GLES2/gl2.h for host unit tests.
 *
 * Provides the GL ES 2.0 types, macros and function prototypes referenced at
 * compile time by renderer.c / linux-dmabuf.c. lorie-wayland-tests links
 * against a stub GL implementation (see test_buffer_stubs.c) so the
 * compositor code can be parsed and exercised on a host without a real GPU.
 */

#ifndef MOCK_GLES2_GL2_H
#define MOCK_GLES2_GL2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef void GLvoid;
typedef char GLchar;

#define GL_FALSE 0
#define GL_TRUE 1

#define GL_NO_ERROR 0

#define GL_FLOAT 0x1406
#define GL_TEXTURE_2D 0x0DE1

#define GL_COLOR_BUFFER_BIT 0x00004000

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82

#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601

#define GL_SCISSOR_TEST 0x0C11
#define GL_TRIANGLE_STRIP 0x0005

void glActiveTexture(GLenum texture);
void glAttachShader(GLuint program, GLuint shader);
void glBindTexture(GLenum target, GLuint texture);
void glClear(GLbitfield mask);
void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void glCompileShader(GLuint shader);
GLuint glCreateProgram(void);
GLuint glCreateShader(GLenum type);
void glDeleteProgram(GLuint program);
void glDeleteShader(GLuint shader);
void glDeleteTextures(GLsizei n, const GLuint *textures);
void glDisable(GLenum cap);
void glDisableVertexAttribArray(GLuint index);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glEnable(GLenum cap);
void glEnableVertexAttribArray(GLuint index);
void glGenTextures(GLsizei n, GLuint *textures);
GLint glGetAttribLocation(GLuint program, const GLchar *name);
void glGetProgramiv(GLuint program, GLenum pname, GLint *params);
void glGetShaderiv(GLuint shader, GLenum pname, GLint *params);
GLint glGetUniformLocation(GLuint program, const GLchar *name);
void glLinkProgram(GLuint program);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glUniform1i(GLint location, GLint v0);
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUseProgram(GLuint program);
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const GLvoid *pointer);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_GLES2_GL2_H */
