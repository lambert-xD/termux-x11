/*
 * Stubs for LorieBuffer functions needed by tests.
 * The real implementations are in lorie/buffer.c and require EGL/GLES.
 */

#include <stdint.h>
#include <stdlib.h>

typedef struct LorieBuffer LorieBuffer;
typedef struct {
    int32_t width, height, stride;
    uint8_t format, type;
    uint64_t id;
    void* buffer;
    void* data;
} LorieBuffer_Desc;

LorieBuffer* LorieBuffer_allocate(int32_t width, int32_t height, int8_t format, int8_t type) {
    (void)width; (void)height; (void)format; (void)type;
    return NULL; /* Tests run without real buffer import */
}

void __LorieBuffer_free(LorieBuffer* buffer) {
    (void)buffer;
}

const LorieBuffer_Desc* LorieBuffer_description(LorieBuffer* buffer) {
    (void)buffer;
    return NULL;
}

void LorieBuffer_attachToGL(LorieBuffer* buffer) {
    (void)buffer;
}

void LorieBuffer_bindTexture(LorieBuffer* buffer) {
    (void)buffer;
}
