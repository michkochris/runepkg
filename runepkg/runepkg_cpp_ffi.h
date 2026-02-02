/******************************************************************************/
/* Filename:    runepkg_cpp_ffi.h                                              */
/* Author:      <michkochris@gmail.com>                                         */
/* Date:        2025-01-04                                                      */
/* Description: C header for C++ FFI availability checks                       */
/* License:     GPL v3                                                         */
/******************************************************************************/

#ifndef RUNEPKG_CPP_FFI_H
#define RUNEPKG_CPP_FFI_H

#ifdef __cplusplus
extern "C" {
#endif

int runepkg_cpp_ffi_available(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNEPKG_CPP_FFI_H */
