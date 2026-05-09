/******************************************************************************/
/* Filename:    runepkg_rust_ffi.c                                             */
/* Author:      <michkochris@gmail.com>                                         */
/* Date:        2025-01-04                                                      */
/* Description: C-side glue for calling into Rust components.                  */
/* License:     GPL v3                                                         */
/******************************************************************************/

#include "runepkg_rust_ffi.h"

#ifndef WITH_RUST_FFI
int runepkg_rust_ffi_available(void) {
    return 0;
}
#else
/* Symbol provided by librunepkg_ffi (Rust static/shared library). */
#endif
