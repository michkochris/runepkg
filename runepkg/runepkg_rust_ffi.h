/******************************************************************************/
/* Filename:    runepkg_rust_ffi.h                                             */
/* Author:      <michkochris@gmail.com>                                         */
/* Date:        2025-01-04                                                      */
/* Description: C header for Rust FFI availability checks                      */
/* License:     GPL v3                                                         */
/******************************************************************************/

#ifndef RUNEPKG_RUST_FFI_H
#define RUNEPKG_RUST_FFI_H

#ifdef __cplusplus
extern "C" {
#endif

int runepkg_rust_ffi_available(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNEPKG_RUST_FFI_H */
