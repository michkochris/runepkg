/* Install-related helpers (implemented in runepkg_install.c). */
#ifndef RUNEPKG_INSTALL_H
#define RUNEPKG_INSTALL_H

#include <stddef.h>

int handle_install(const char *deb_file_path);
void handle_install_stdin(void);
void handle_install_listfile(const char *path);

/* Sibling .deb install helper: returns 1 if found/installed, 0 if not found, -1 on error. */
int clandestine_handle_install(const char *pkg_name, const char *origin_deb_path,
                               char ***attempted_list, int *attempted_count);

int calculate_optimal_threads(void);

#endif /* RUNEPKG_INSTALL_H */
