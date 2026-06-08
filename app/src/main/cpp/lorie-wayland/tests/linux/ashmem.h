/* Mock linux/ashmem.h for host unit tests.
 *
 * The real header (provided by the Android NDK sysroot / bionic) declares the
 * ioctl request codes and limits used to talk to /dev/ashmem. It is not
 * available on a plain Linux host toolchain. The host test suite never
 * exercises the ashmem code paths (lorie/buffer.c is not compiled into
 * lorie-wayland-tests — see test_buffer_stubs.c), so this mock only needs to
 * provide the symbols referenced at *compile time* by lorie/buffer.h /
 * lorie/buffer.c so that translation units which #include <linux/ashmem.h>
 * (transitively, via lorie/buffer.h) succeed on host.
 */

#ifndef MOCK_LINUX_ASHMEM_H
#define MOCK_LINUX_ASHMEM_H

#include <stddef.h>
#include <sys/ioctl.h>

#define ASHMEM_NAME_LEN 256

#define __ASHMEMIOC 0x77

#define ASHMEM_SET_NAME _IOW(__ASHMEMIOC, 1, char[ASHMEM_NAME_LEN])
#define ASHMEM_GET_NAME _IOR(__ASHMEMIOC, 2, char[ASHMEM_NAME_LEN])
#define ASHMEM_SET_SIZE _IOW(__ASHMEMIOC, 3, size_t)
#define ASHMEM_GET_SIZE _IO(__ASHMEMIOC, 4)
#define ASHMEM_SET_PROT_MASK _IOW(__ASHMEMIOC, 5, unsigned long)
#define ASHMEM_GET_PROT_MASK _IO(__ASHMEMIOC, 6)
#define ASHMEM_PIN _IOW(__ASHMEMIOC, 7, unsigned int)
#define ASHMEM_UNPIN _IOW(__ASHMEMIOC, 8, unsigned int)
#define ASHMEM_GET_PIN_STATUS _IO(__ASHMEMIOC, 9)
#define ASHMEM_PURGE_ALL_CACHES _IO(__ASHMEMIOC, 10)

#endif /* MOCK_LINUX_ASHMEM_H */
