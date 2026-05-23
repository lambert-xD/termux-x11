/*
 * Stubs for LorieBuffer functions needed by tests.
 * The real implementations are in lorie/buffer.c and require EGL/GLES.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct LorieBuffer LorieBuffer;
typedef struct {
    int32_t width, height, stride;
    uint8_t format, type;
    uint64_t id;
    void* buffer;
    void* data;
} LorieBuffer_Desc;

typedef struct {
    int refcount;
    LorieBuffer_Desc desc;
} TestLorieBuffer;

static int g_force_alloc_fail = 0;

void lorie_test_force_alloc_fail(int fail) {
    g_force_alloc_fail = fail;
}

LorieBuffer* LorieBuffer_allocate(int32_t width, int32_t height, int8_t format, int8_t type) {
    if (g_force_alloc_fail)
        return NULL;
    (void)type;
    TestLorieBuffer *b = calloc(1, sizeof(TestLorieBuffer) + width * height * 4);
    if (!b) return NULL;
    b->refcount = 1;
    b->desc.width = width;
    b->desc.height = height;
    b->desc.stride = width * 4;
    b->desc.format = format;
    b->desc.type = 1; /* LORIEBUFFER_REGULAR */
    b->desc.data = (uint8_t*)b + sizeof(TestLorieBuffer);
    return (LorieBuffer*)b;
}

void __LorieBuffer_free(LorieBuffer* buffer) {
    free(buffer);
}

const LorieBuffer_Desc* LorieBuffer_description(LorieBuffer* buffer) {
    if (!buffer) return NULL;
    return &((TestLorieBuffer*)buffer)->desc;
}

void LorieBuffer_acquire(LorieBuffer* buffer) {
    if (!buffer) return;
    ((TestLorieBuffer*)buffer)->refcount++;
}

void LorieBuffer_release(LorieBuffer* buffer) {
    if (!buffer) return;
    if (--((TestLorieBuffer*)buffer)->refcount == 0)
        __LorieBuffer_free(buffer);
}

void LorieBuffer_attachToGL(LorieBuffer* buffer) {
    (void)buffer;
}

void LorieBuffer_bindTexture(LorieBuffer* buffer) {
    (void)buffer;
}
