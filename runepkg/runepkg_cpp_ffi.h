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
int runepkg_update(void);
int runepkg_repo_search(const char *query);
char* runepkg_repo_download(const char *pkg_name);
int runepkg_upgrade(void);
int runepkg_repo_source_download(const char *pkg_name);
int runepkg_source_build(const char *dsc_path);

#ifdef __cplusplus
}
#endif

#endif /* RUNEPKG_CPP_FFI_H */
