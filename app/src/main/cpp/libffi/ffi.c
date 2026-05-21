/* Minimal libffi stub for Android NDK build verification.
 * See ffi.h for rationale.
 */

#include "ffi.h"
#include <android/log.h>

#define LOG_TAG "libffi-stub"

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
    (void)cif; (void)abi; (void)nargs; (void)rtype; (void)atypes;
    return FFI_OK;
}

void ffi_call(ffi_cif *cif, void (*fn)(void), void *rvalue, void **avalue) {
    (void)cif; (void)fn; (void)rvalue; (void)avalue;
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG,
                        "ffi_call stub invoked — real libffi not linked");
}
