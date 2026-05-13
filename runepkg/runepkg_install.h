/* Install-related helpers (implemented in runepkg_install.c). */
#ifndef RUNEPKG_INSTALL_H
#define RUNEPKG_INSTALL_H

#include <stddef.h>

#include "runepkg_hash.h"

int handle_install(const char *deb_file_path);
void handle_install_stdin(void);
void handle_install_listfile(const char *path);

/**
 * @brief Executes a maintainer script (preinst, postinst, etc.)
 * @param script_path Absolute path to the script file.
 * @param pkg_info Pointer to the package info structure.
 * @param action The action being performed (e.g., "install", "configure").
 * @return 0 on success, or non-zero script exit code.
 */
int runepkg_execute_maintainer_script(const char *script_path, const PkgInfo *pkg_info, const char *action);

/* Sibling .deb install helper: returns 1 if found/installed, 0 if not found, -1 on error. */
int clandestine_handle_install(const char *pkg_name, const char *origin_deb_path,
                               char ***attempted_list, int *attempted_count);

int calculate_optimal_threads(void);

#endif /* RUNEPKG_INSTALL_H */
