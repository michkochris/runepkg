/* runepkg_completion.h - completion helpers for runepkg
 */
#ifndef RUNEPKG_COMPLETION_H
#define RUNEPKG_COMPLETION_H

#include <stdint.h>

int is_completion_trigger(char *argv[]);
int prefix_search_and_print(const char *prefix);
void complete_deb_files(const char *partial);
void complete_file_paths(const char *partial);
void handle_binary_completion(const char *partial, const char *prev);
void handle_print_auto_pkgs(void);

#endif /* RUNEPKG_COMPLETION_H */
