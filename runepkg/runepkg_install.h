// Header for install-related helpers moved out of runepkg_handle.c
#ifndef RUNEPKG_INSTALL_H
#define RUNEPKG_INSTALL_H

#include <stddef.h>

int handle_install(const char *deb_file_path);
void handle_install_stdin(void);
void handle_install_listfile(const char *path);

/* Exposed helper that attempts to find and install a sibling .deb for a
 * dependency package name. Returns 1 if found/installed, 0 otherwise.
 */
int clandestine_handle_install(const char *pkg_name, const char *origin_deb_path, char ***attempted_list, int *attempted_count);

/* Calculate optimal number of threads for file installation */
int calculate_optimal_threads(void);

#endif // RUNEPKG_INSTALL_H
/* runepkg_install.h - install and threading helpers for runepkg */
#ifndef RUNEPKG_INSTALL_H
#define RUNEPKG_INSTALL_H

#include <pthread.h>
#include "runepkg_hash.h"

int handle_install(const char *deb_file_path);
void handle_install_stdin(void);
void handle_install_listfile(const char *path);

/* Look for a dependency .deb sibling next to a parent deb and attempt install.
 * Returns 1 if a match was found and an install attempted, 0 if not found,
 * and -1 on internal error.
 */
int clandestine_handle_install(const char *pkgname, const char *parent_deb_path, char ***attempted_deps_ptr, int *attempted_count_ptr);

/* Calculate optimal thread count for file installs */
int calculate_optimal_threads(void);

#endif /* RUNEPKG_INSTALL_H */
