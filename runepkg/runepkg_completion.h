/*****************************************************************************
 * Filename:    runepkg_completion.h
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Completion helpers for runepkg
 ******************************************************************************/

#ifndef RUNEPKG_COMPLETION_H
#define RUNEPKG_COMPLETION_H

#include "runepkg_portable.h"
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

int is_completion_trigger(char *argv[]);
int prefix_search_and_print(const char *prefix);
int repo_prefix_search_and_print(const char *prefix);
int repo_src_prefix_search_and_print(const char *prefix);
void complete_deb_files(const char *partial);
void complete_file_paths(const char *partial);
void handle_binary_completion(const char *partial, const char *prev);
void handle_print_auto_pkgs(void);
int runepkg_completion_get_repo_suggestions(const char *search_name, char suggestions[][PATH_MAX], int max_suggestions);

#endif /* RUNEPKG_COMPLETION_H */
