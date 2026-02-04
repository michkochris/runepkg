/*****************************************************************************
 * Filename:    runepkg_handle.h
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Declarations for high-level request handlers and init/cleanup
 * LICENSE:     GPL v3
 * THIS IS FREE SOFTWARE; YOU CAN REDISTRIBUTE IT AND/OR MODIFY IT UNDER
 * THE TERMS OF THE GNU GENERAL PUBLIC LICENSE AS PUBLISHED BY THE FREE
 * SOFTWARE FOUNDATION; EITHER VERSION 3 OF THE LICENSE, OR (AT YOUR OPTION)
 * ANY LATER VERSION.
 * THIS PROGRAM IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. SEE THE
 * GNU GENERAL PUBLIC LICENSE FOR MORE DETAILS.
 ******************************************************************************/

#ifndef RUNEPKG_HANDLE_H
#define RUNEPKG_HANDLE_H

#include <stdbool.h>
#include "runepkg_hash.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Global verbose flag defined in runepkg_cli.c */
extern bool g_verbose_mode;

/* Global force mode flag */
extern bool g_force_mode;

/* Hash table for tracking packages currently being installed (for cycle detection) */
extern runepkg_hash_table_t *installing_packages;

int runepkg_init(void);
void runepkg_cleanup(void);

int handle_install(const char *deb_file_path);
void handle_install_stdin(void);
void handle_install_listfile(const char *path);
int handle_remove(const char *package_name);
void handle_remove_stdin(void);

void handle_list(const char *pattern);
int handle_status(const char *package_name);
void handle_search(const char *query);
void handle_print_config(void);
void handle_print_config_file(void);
void handle_print_pkglist_file(void);
void handle_version(void);
void handle_update_pkglist(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNEPKG_HANDLE_H */
