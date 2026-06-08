/* Mock android/hardware_buffer.h for host unit tests.
 *
 * Provides the minimal opaque type and macros referenced by lorie/buffer.h so
 * that translation units which transitively #include <android/hardware_buffer.h>
 * (e.g. surface.c -> lorie/buffer.h) can be parsed by a plain host (GCC)
 * toolchain. lorie/buffer.c (the real implementation using these APIs) is not
 * compiled into lorie-wayland-tests — see test_buffer_stubs.c — so no function
 * bodies or full struct layouts are required here.
 */

#ifndef MOCK_ANDROID_HARDWARE_BUFFER_H
#define MOCK_ANDROID_HARDWARE_BUFFER_H

#include <stdint.h>

/* Clang nullability annotations are not understood by host GCC. Bionic's
 * <sys/cdefs.h> normally neutralizes them on non-Clang compilers; provide the
 * same neutralization here for the host build. */
#ifndef _Nullable
#define _Nullable
#endif
#ifndef _Nonnull
#define _Nonnull
#endif
#ifndef __unused
#define __unused __attribute__((__unused__))
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct AHardwareBuffer;
typedef struct AHardwareBuffer AHardwareBuffer;

typedef struct AHardwareBuffer_Desc {
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    uint32_t format;
    uint64_t usage;
    uint32_t stride;
    uint32_t rfu0;
    uint64_t rfu1;
} AHardwareBuffer_Desc;

#define AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM 1
#define AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM 2
#define AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM 3
#define AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM 4
/* AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM is (re)defined locally in
 * lorie/buffer.h with the same value (5); intentionally not duplicated here
 * to avoid a macro redefinition warning. */

#define AHARDWAREBUFFER_USAGE_CPU_READ_NEVER 0UL
#define AHARDWAREBUFFER_USAGE_CPU_READ_RARELY 2UL
#define AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN 3UL
#define AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER 0UL
#define AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY (2UL << 4)
#define AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN (3UL << 4)
#define AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE (1UL << 8)
#define AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER (1UL << 9)

void AHardwareBuffer_acquire(AHardwareBuffer* _Nonnull buffer);
void AHardwareBuffer_release(AHardwareBuffer* _Nonnull buffer);
int AHardwareBuffer_allocate(const AHardwareBuffer_Desc* _Nonnull desc,
                             AHardwareBuffer* _Nullable* _Nonnull outBuffer);
void AHardwareBuffer_describe(const AHardwareBuffer* _Nonnull buffer,
                              AHardwareBuffer_Desc* _Nonnull outDesc);
int AHardwareBuffer_lock(AHardwareBuffer* _Nonnull buffer, uint64_t usage,
                         int32_t fence, const void* _Nullable rect,
                         void* _Nullable* _Nonnull outVirtualAddress);
int AHardwareBuffer_unlock(AHardwareBuffer* _Nonnull buffer, int32_t* _Nullable fence);
int AHardwareBuffer_sendHandleToUnixSocket(const AHardwareBuffer* _Nonnull buffer, int socketFd);
int AHardwareBuffer_recvHandleFromUnixSocket(int socketFd, AHardwareBuffer* _Nullable* _Nonnull outBuffer);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_ANDROID_HARDWARE_BUFFER_H */
