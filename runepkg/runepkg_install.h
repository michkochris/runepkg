/*****************************************************************************
 * Filename:    runepkg_install.h
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Install-related helpers
 ******************************************************************************/

#ifndef RUNEPKG_INSTALL_H
#define RUNEPKG_INSTALL_H

#include <stddef.h>
#include "runepkg_hash.h"

int handle_install(const char *deb_file_path);
void handle_install_stdin(void);
void handle_install_listfile(const char *path);

/**
 * @brief Performs a non-interactive installation of a single package as part of a batch.
 */
int runepkg_install_batch_item(const char *deb_file_path);

/**
 * @brief Executes a maintainer script (preinst, postinst, etc.)
 */
int runepkg_execute_maintainer_script(const char *script_path, const PkgInfo *pkg_info, const char *action);

/* Sibling .deb install helper: returns 1 if found/installed, 0 if not found, -1 on error. */
int clandestine_handle_install(const char *pkg_name, const char *origin_deb_path,
                               char ***attempted_list, int *attempted_count);

int calculate_optimal_threads(void);

/**
 * @brief Checks if a package (possibly virtual) is provided by any package in the hash table.
 */
int is_package_provided_by_table(runepkg_hash_table_t *table, const char *pkg_name);

#endif /* RUNEPKG_INSTALL_H */
