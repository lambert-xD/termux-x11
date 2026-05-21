/* Minimal libffi stub for Android NDK build verification.
 * TODO(PR-1-followup): Replace with real libffi build from source or submodule.
 * This stub provides enough symbols for wayland-server to compile and link.
 * ffi_call() is a no-op; real Wayland client request handling requires
 * a proper libffi implementation (see PR 2+ for client integration).
 */

#ifndef LIBFFI_H
#define LIBFFI_H

#include <stddef.h>

#define FFI_DEFAULT_ABI 0

#define FFI_OK 0
#define FFI_BAD_TYPEDEF 1
#define FFI_BAD_ABI 2

typedef struct _ffi_type {
    size_t size;
    unsigned short alignment;
    unsigned short type;
    struct _ffi_type **elements;
} ffi_type;

extern ffi_type ffi_type_void;
extern ffi_type ffi_type_sint32;
extern ffi_type ffi_type_uint32;
extern ffi_type ffi_type_pointer;
extern ffi_type ffi_type_sint64;
extern ffi_type ffi_type_uint64;
extern ffi_type ffi_type_float;
extern ffi_type ffi_type_double;

typedef struct {
    unsigned char used;
    unsigned char abi;
    unsigned short nargs;
    ffi_type **arg_types;
    ffi_type *rtype;
    unsigned bytes;
} ffi_cif;

#ifdef __cplusplus
extern "C" {
#endif

int ffi_prep_cif(ffi_cif *cif, unsigned int abi, unsigned int nargs,
                 ffi_type *rtype, ffi_type **atypes);
void ffi_call(ffi_cif *cif, void (*fn)(void), void *rvalue, void **avalue);

#ifdef __cplusplus
}
#endif

#endif /* LIBFFI_H */
