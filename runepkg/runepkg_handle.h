/*****************************************************************************
 * Filename:    runepkg_handle.h
 * Author:      <michkochris@gmail.com>
 * Date:        2026-08-26
 * Description: Header for request handlers and lifecycle routines
 * LICENSE:     GPL v3
 ******************************************************************************/

#ifndef RUNEPKG_HANDLE_H
#define RUNEPKG_HANDLE_H

#include "runepkg_portable.h"
#include "runepkg_pack.h"
#include "runepkg_hash.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Global flags (unique to handle/cli) */
extern bool g_verbose_mode;
extern bool g_force_mode;
extern bool g_debug_mode;
extern bool g_did_install;

/* Global state tracking table */
extern runepkg_hash_table_t *installing_packages;

/* Lifecycle */
int runepkg_init(void);
void runepkg_cleanup(void);

/* Basic Package Management */
int print_package_data_header(void);
int handle_install(const char *deb_path);
void handle_install_stdin(void);
void handle_install_listfile(const char *path);
int handle_remove(const char *package_name);
void handle_remove_stdin(void);
void handle_remove_listfile(const char *path);
void handle_list(const char *pattern);
int handle_status(const char *package_name);
void handle_search(const char *file_pattern);
void handle_list_files(const char *package_name);
int handle_unpack(const char *deb_path, const char *dest_dir);
int handle_md5_check(const char *package_name);
void handle_verify_package(const char *package_name);
void handle_version(void);

/* Build & Packaging */
int handle_build(const char *source_dir, const char *output_name);
int handle_source_build(const char *dsc_path);
int handle_source_build_split(const char *dsc_path, const char *target_pkg);
int handle_source_build_sysroot(const char *dsc_path);

/* Configuration & Profiles */
void handle_print_config(void);
void handle_print_config_file(void);
void handle_print_pkglist_file(void);
void handle_update_pkglist(void);
int handle_switch(const char *switch_arg);
void handle_print_profile(void);

/* Toolchain Bootstrap & Cross-Forge Engine */
int handle_bootstrap(const char *target_name);
int handle_bootstrap_with_targets(const char *target_name, const char **pkg_names, int count);
int handle_build_toolchain(const char *target_name);
int handle_build_toolchain_with_targets(const char *target_name, const char **pkg_names, int count);

/* 70k+ Repository Graph Harvester & Target Dependency Resolver */
int handle_resolve_tree(const char *pkg_name);

#ifdef __cplusplus
}
#endif

#endif /* RUNEPKG_HANDLE_H */
