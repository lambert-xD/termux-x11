/* Minimal libffi-compatible dispatcher for Wayland.
 *
 * This is not a complete libffi implementation. It supports the integer and
 * pointer argument shapes used by Wayland request/event dispatch so the
 * Android build can execute real client/server protocol callbacks until the
 * project vendors full libffi.
 */

#include "ffi.h"
#include <stdint.h>

ffi_type ffi_type_void     = { 1, 1, 0, NULL };
ffi_type ffi_type_sint32   = { 4, 4, 0, NULL };
ffi_type ffi_type_uint32   = { 4, 4, 0, NULL };
ffi_type ffi_type_pointer  = { sizeof(void*), sizeof(void*), 0, NULL };
ffi_type ffi_type_sint64   = { 8, 8, 0, NULL };
ffi_type ffi_type_uint64   = { 8, 8, 0, NULL };
ffi_type ffi_type_float    = { 4, 4, 0, NULL };
ffi_type ffi_type_double   = { 8, 8, 0, NULL };

int ffi_prep_cif(ffi_cif *cif, unsigned int abi, unsigned int nargs,
                 ffi_type *rtype, ffi_type **atypes) {
    if (!cif) return FFI_BAD_TYPEDEF;
    cif->abi = (unsigned char)abi;
    cif->nargs = (unsigned short)nargs;
    cif->rtype = rtype;
    cif->arg_types = atypes;
    cif->bytes = 0;
    cif->used = 1;
    return FFI_OK;
}

static uintptr_t ffi_arg_word(ffi_type *type, void *value) {
    if (type == &ffi_type_pointer)
        return (uintptr_t)*(void **)value;
    if (type == &ffi_type_sint32)
        return (uintptr_t)(intptr_t)*(int32_t *)value;
    if (type == &ffi_type_uint32)
        return (uintptr_t)*(uint32_t *)value;
    if (type == &ffi_type_sint64)
        return (uintptr_t)*(int64_t *)value;
    if (type == &ffi_type_uint64)
        return (uintptr_t)*(uint64_t *)value;
    return (uintptr_t)*(void **)value;
}

void ffi_call(ffi_cif *cif, void (*fn)(void), void *rvalue, void **avalue) {
    (void)rvalue;
    if (!cif || !fn || !avalue) return;

    uintptr_t a[20] = {0};
    unsigned int nargs = cif->nargs;
    if (nargs > 20) nargs = 20;
    for (unsigned int i = 0; i < nargs; i++)
        a[i] = ffi_arg_word(cif->arg_types[i], avalue[i]);

    typedef void (*ffi_fn_20)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                              uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                              uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                              uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    ((ffi_fn_20)fn)(a[0], a[1], a[2], a[3], a[4],
                    a[5], a[6], a[7], a[8], a[9],
                    a[10], a[11], a[12], a[13], a[14],
                    a[15], a[16], a[17], a[18], a[19]);
}
