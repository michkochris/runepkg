/*****************************************************************************
 * Filename:    runepkg_handle.c
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: High-level request handlers and initialization/cleanup for runepkg
 * LICENSE:     GPL v3
 * THIS IS FREE SOFTWARE; YOU CAN REDISTRIBUTE IT AND/OR MODIFY IT UNDER
 * THE TERMS OF THE GNU GENERAL PUBLIC LICENSE AS PUBLISHED BY THE FREE
 * SOFTWARE FOUNDATION; EITHER VERSION 3 OF THE LICENSE, OR (AT YOUR OPTION)
 * ANY LATER VERSION.
 * THIS PROGRAM IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. SEE THE
 * GNU GENERAL PUBLIC LICENSE FOR MORE DETAILS.
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <glob.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/statvfs.h>
#include <fnmatch.h>
#include <ctype.h>
#include <sys/ioctl.h>

#include "runepkg_handle.h"
#include "runepkg_config.h"
#include "runepkg_pack.h"
#include "runepkg_hash.h"
#include "runepkg_storage.h"
#include "runepkg_util.h"
#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "runepkg_completion.h"

/* Completion helpers are implemented in runepkg_completion.c */

/* clandestine dependency sibling install moved to runepkg_install.c */

/* Binary completion entrypoint: prints completion candidates to stdout.
 * This implementation first tries to read Bash's `COMP_LINE`/`COMP_POINT`
 * environment to reconstruct the full command line and infer the active
 * command (e.g. `install`) even when flags are interleaved. If that data
 * isn't available, it falls back to the previous `prev`-based behavior.
 */
/* Completion implementations moved to runepkg_completion.c */

/* Auto-package listing moved to runepkg_completion.c */


#ifdef __ANDROID__
int getloadavg(double loadavg[], int nelem) {
    FILE *fp = fopen("/proc/loadavg", "r");
    if (!fp) return -1;
    
    int count = 0;
    for (int i = 0; i < nelem && i < 3; i++) {
        if (fscanf(fp, "%lf", &loadavg[i]) != 1) break;
        count++;
    }
    
    fclose(fp);
    return (count > 0) ? count : -1;
}
#endif

runepkg_hash_table_t *installing_packages = NULL;

// Helper function to print package data header (used by handle_list and handle_remove)
int print_package_data_header(void) {
    if (!g_runepkg_db_dir) {
        return 0;
    }

    // Count packages
    DIR *dir = opendir(g_runepkg_db_dir);
    if (!dir) {
        return 0;
    }

    int pkg_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            pkg_count++;
        }
    }
    closedir(dir);

    // Calculate used space
    off_t used_space = runepkg_util_get_dir_size(g_runepkg_db_dir);

    // Get available space
    struct statvfs vfs;
    off_t avail_space = 0;
    if (statvfs(g_runepkg_db_dir, &vfs) == 0) {
        avail_space = (off_t)vfs.f_bavail * vfs.f_frsize;
    }

    // Format sizes
    char used_str[32], avail_str[32];
    runepkg_util_format_size(used_space, used_str, sizeof(used_str));
    runepkg_util_format_size(avail_space, avail_str, sizeof(avail_str));

    printf("Reading package data: %d packages, %s used, %s available\n", pkg_count, used_str, avail_str);
    return pkg_count;
}

// Helper function to calculate directory size recursively
/*
 * Calculate optimal number of threads for file operations
 * Based on available CPU cores and current system load
 */
/* calculate_optimal_threads moved to runepkg_install.c */

/*
 * runepkg_handle.c
 *
 * Central location for higher-level request handling (install/remove/etc.).
 */

int runepkg_init(void) {
    runepkg_log_verbose("Initializing runepkg...\n");
    
    /* Load configuration and create/verify configured directories */
    runepkg_init_paths();
    
    /* Initialize hash table if not already created */
    if (!runepkg_main_hash_table) {
        runepkg_main_hash_table = runepkg_hash_create_table(INITIAL_HASH_TABLE_SIZE);
        if (!runepkg_main_hash_table) {
            fprintf(stderr, "Error: Failed to create hash table for package management.\n");
            return -1;
        } else {
            runepkg_log_verbose("Hash table initialized for package management.\n");
        }
    }

    /* Initialize installing packages hash table for cycle detection */
    if (!installing_packages) {
        installing_packages = runepkg_hash_create_table(INITIAL_HASH_TABLE_SIZE);
        if (!installing_packages) {
            fprintf(stderr, "Error: Failed to create installing hash table.\n");
            return -1;
        } else {
            runepkg_log_verbose("Installing hash table initialized.\n");
        }
    }
    
    /* TODO: Load installed packages into hash table from persistent storage */
    /* COMPLETED: The call is now handled by the helper, validation done via assumption */
    
    /* Load installed packages into hash table */
    if (g_runepkg_db_dir) {
        DIR *dir = opendir(g_runepkg_db_dir);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                    // Parse package name and version from directory name
                    char pkg_name[PATH_MAX] = {0};
                    char pkg_version[PATH_MAX] = {0};
                    const char *last_dash = strrchr(entry->d_name, '-');
                    if (last_dash && last_dash != entry->d_name) {
                        size_t name_len = (size_t)(last_dash - entry->d_name);
                        if (name_len < sizeof(pkg_name)) {
                            memcpy(pkg_name, entry->d_name, name_len);
                            pkg_name[name_len] = '\0';
                            strncpy(pkg_version, last_dash + 1, sizeof(pkg_version) - 1);
                        }
                    }
                    
                    if (pkg_name[0] && pkg_version[0]) {
                        PkgInfo pkg_info;
                        if (runepkg_storage_read_package_info(pkg_name, pkg_version, &pkg_info) == 0) {
                            runepkg_hash_add_package(runepkg_main_hash_table, &pkg_info);
                            runepkg_pack_free_package_info(&pkg_info);
                        }
                    }
                }
            }
            closedir(dir);
        }
    }
    
    return 0;
}

void runepkg_cleanup(void) {
    runepkg_log_verbose("Cleaning up runepkg environment...\n");
    
    if (runepkg_main_hash_table) {
        runepkg_hash_destroy_table(runepkg_main_hash_table);
        runepkg_main_hash_table = NULL;
    }

    if (installing_packages) {
        runepkg_hash_destroy_table(installing_packages);
        installing_packages = NULL;
    }
    
    runepkg_config_cleanup();
    
    runepkg_log_verbose("runepkg cleanup completed.\n");
}

/* Install logic and threading moved to runepkg_install.c
 * Functions moved: handle_install_stdin, handle_install_listfile,
 * install_single_file (thread worker), and handle_install.
 */

void handle_remove_stdin(void) {
    char line[PATH_MAX * 2];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *trimmed = runepkg_util_trim_whitespace(line);
        if (!trimmed || trimmed[0] == '\0') continue;

        char *token = strtok(trimmed, " \t");
        while (token) {
            handle_remove(token);
            token = strtok(NULL, " \t");
        }
    }
}

int handle_remove(const char *package_name) {
    if (!package_name || package_name[0] == '\0') {
        printf("Error: remove requires a package name.\n");
        return -1;
    }

    char name_buf[PATH_MAX];
    runepkg_util_safe_strncpy(name_buf, package_name, sizeof(name_buf));
    char *trimmed = runepkg_util_trim_whitespace(name_buf);
    if (!trimmed || trimmed[0] == '\0') {
        printf("Error: remove requires a package name.\n");
        return -1;
    }

    if (!g_runepkg_db_dir) {
        printf("Error: runepkg database directory not configured.\n");
        return -1;
    }

    char pkg_name[PATH_MAX] = {0};
    char pkg_version[PATH_MAX] = {0};

    const char *last_dash = strrchr(trimmed, '-');
    if (last_dash && last_dash != trimmed && *(last_dash + 1) != '\0') {
        size_t name_len = (size_t)(last_dash - trimmed);
        if (name_len < sizeof(pkg_name)) {
            memcpy(pkg_name, trimmed, name_len);
            pkg_name[name_len] = '\0';
            strncpy(pkg_version, last_dash + 1, sizeof(pkg_version) - 1);
        }
    }

    if (pkg_name[0] == '\0' || pkg_version[0] == '\0') {
        DIR *dir = opendir(g_runepkg_db_dir);
        if (!dir) {
            printf("Error: Cannot open runepkg database directory: %s\n", g_runepkg_db_dir);
            return -1;
        }

        struct dirent *entry;
        int match_count = 0;
        char match_name[PATH_MAX] = {0};
        char match_version[PATH_MAX] = {0};

        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type != DT_DIR) continue;
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

            size_t input_len = strlen(trimmed);
            if (strncmp(entry->d_name, trimmed, input_len) == 0 && entry->d_name[input_len] == '-') {
                const char *ver = entry->d_name + input_len + 1;
                if (*ver != '\0') {
                    match_count++;
                    strncpy(match_name, trimmed, sizeof(match_name) - 1);
                    strncpy(match_version, ver, sizeof(match_version) - 1);
                }
            }
        }

        closedir(dir);

        if (match_count == 1) {
            strncpy(pkg_name, match_name, sizeof(pkg_name) - 1);
            strncpy(pkg_version, match_version, sizeof(pkg_version) - 1);
        } else if (match_count > 1) {
            // Multiple matches - show them like -l does
            print_package_data_header();
            printf("Looking for package... '%s' did you mean?\n", package_name);
            
            // Collect and display matching packages
            char matches[100][PATH_MAX];
            int match_idx = 0;
            
            // Re-scan directory to collect matches
            DIR *list_dir = opendir(g_runepkg_db_dir);
            if (list_dir) {
                struct dirent *entry;
                while ((entry = readdir(list_dir)) != NULL && match_idx < 100) {
                    if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                        if (strstr(entry->d_name, trimmed) != NULL) {
                            strncpy(matches[match_idx], entry->d_name, PATH_MAX - 1);
                            matches[match_idx][PATH_MAX - 1] = '\0';
                            match_idx++;
                        }
                    }
                }
                closedir(list_dir);
                
                // Display in columns
                const char *items[100];
                for (int i = 0; i < match_idx; i++) {
                    items[i] = matches[i];
                }
                runepkg_util_print_columns(items, match_idx);
            }
            
            return -2; // Special code: showed suggestions, no removal performed
        } else {
            // No exact matches found - show suggestions if any packages contain the search string
            char suggestions[100][PATH_MAX];
            int suggestion_count = runepkg_util_get_package_suggestions(trimmed, g_runepkg_db_dir, suggestions, 100);
            
            if (suggestion_count > 0) {
                // Show suggestions in the same clean format
                print_package_data_header();
                printf("Looking for package... '%s' did you mean?\n", package_name);
                
                const char *items[100];
                for (int i = 0; i < suggestion_count; i++) {
                    items[i] = suggestions[i];
                }
                runepkg_util_print_columns(items, suggestion_count);
                return -2; // Suggestions shown, no removal performed
            } else {
                printf("Error: package not installed: %s\n", trimmed);
                return -1;
            }
        }
    }

    PkgInfo pkg_info;
    if (runepkg_storage_read_package_info(pkg_name, pkg_version, &pkg_info) != 0) {
        printf("Error: package not installed: %s-%s\n", pkg_name, pkg_version);
        return -1;
    }

    // Confirmation prompt in verbose mode
    if (g_verbose_mode) {
        printf("Do you want to remove package %s-%s? [y/N] ", pkg_name, pkg_version);
        fflush(stdout);
        char response[10];
        if (fgets(response, sizeof(response), stdin) == NULL || (response[0] != 'y' && response[0] != 'Y')) {
            printf("Removal cancelled.\n");
            runepkg_pack_free_package_info(&pkg_info);
            return -1;
        }
    }

    if (g_system_install_root && pkg_info.file_list && pkg_info.file_count > 0) {
        for (int i = 0; i < pkg_info.file_count; i++) {
            const char *rel = pkg_info.file_list[i];
            if (!rel || rel[0] == '\0') continue;
            char *dst = runepkg_util_concat_path(g_system_install_root, rel);
            if (!dst) continue;
            if (unlink(dst) != 0) {
                runepkg_log_verbose("Remove: failed to delete %s\n", dst);
            }
            runepkg_util_free_and_null(&dst);
        }
    }

    runepkg_pack_free_package_info(&pkg_info);

    if (runepkg_storage_remove_package(pkg_name, pkg_version) != 0) {
        printf("Warning: failed to remove package metadata for %s-%s\n", pkg_name, pkg_version);
        runepkg_pack_free_package_info(&pkg_info);
        return -1;
    }

    // Rebuild autocomplete index after remove
    runepkg_storage_build_autocomplete_index();

    // Update the text autocomplete list
    handle_update_pkglist();

    runepkg_pack_free_package_info(&pkg_info);
    return 0;
}

void handle_version(void) {
    printf("runepkg v0.1.0 - The Runar Linux package manager\n");
    printf("Copyright (c) 2025 runepkg (Runar Linux) All rights reserved.\n");
    printf("Licensed under GPL v3\n");
}

void handle_list(const char *pattern) {
    runepkg_log_verbose("Listing installed packages...\n");

    print_package_data_header();

    printf("Listing installed packages...\n");
    int listed = runepkg_storage_list_packages(pattern);
    if (pattern && listed == 0) {
        printf("No packages match '%s'.\n", pattern);
    }
    if (g_runepkg_db_dir) runepkg_log_verbose("  Database dir: %s\n", g_runepkg_db_dir);
}

int handle_status(const char *package_name) {
    if (!package_name || !g_runepkg_db_dir) {
        printf("Error: Invalid package name or config.\n");
        return -1;
    }

    print_package_data_header();

    // First pass: count exact matches
    DIR *dir = opendir(g_runepkg_db_dir);
    if (!dir) {
        printf("Error: Cannot open runepkg database directory: %s\n", g_runepkg_db_dir);
        return -1;
    }

    struct dirent *entry;
    int exact_match_count = 0;
    char exact_match_name[PATH_MAX] = {0};
    char exact_match_version[PATH_MAX] = {0};

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR) continue;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        // Check for exact match first
        if (strcmp(entry->d_name, package_name) == 0) {
            exact_match_count = 1;
            // For exact match, parse name and version from the directory name
            // Find the last '-' followed by a digit (version separator)
            char *last_dash = NULL;
            for (char *p = entry->d_name; *p; p++) {
                if (!last_dash && *p == '-' && *(p + 1) && isdigit(*(p + 1))) {
                    last_dash = p;
                }
            }
            if (last_dash) {
                size_t name_len = last_dash - entry->d_name;
                strncpy(exact_match_name, entry->d_name, name_len);
                exact_match_name[name_len] = '\0';
                strcpy(exact_match_version, last_dash + 1);
            } else {
                // No version part, use the name as-is
                strncpy(exact_match_name, entry->d_name, sizeof(exact_match_name) - 1);
                exact_match_version[0] = '\0';
            }
            break; // Exact match found, no need to continue
        }

        // Check for prefix matches (for partial names like "binutils")
        size_t name_len = strlen(package_name);
        if (strncmp(entry->d_name, package_name, name_len) == 0 && entry->d_name[name_len] == '-') {
            const char *ver = entry->d_name + name_len + 1;
            if (*ver != '\0') {
                exact_match_count++;
                if (exact_match_count == 1) {
                    // Store the first match
                    strncpy(exact_match_name, package_name, sizeof(exact_match_name) - 1);
                }
            }
        }
    }
    closedir(dir);

    if (exact_match_count == 1) {
        // Exactly one match - show status
        PkgInfo pkg_info;
        runepkg_pack_init_package_info(&pkg_info);
        if (runepkg_storage_read_package_info(exact_match_name, exact_match_version, &pkg_info) == 0) {
            printf("Package: %s\n", exact_match_name);
            printf("Version: %s\n", pkg_info.version ? pkg_info.version : "(unknown)");
            printf("Architecture: %s\n", pkg_info.architecture ? pkg_info.architecture : "(unknown)");
            printf("Maintainer: %s\n", pkg_info.maintainer ? pkg_info.maintainer : "(unknown)");
            printf("Description: %s\n", pkg_info.description ? pkg_info.description : "(unknown)");
            printf("Depends: %s\n", pkg_info.depends ? pkg_info.depends : "(none)");
            printf("Installed-Size: %s\n", pkg_info.installed_size ? pkg_info.installed_size : "(unknown)");
            printf("Section: %s\n", pkg_info.section ? pkg_info.section : "(unknown)");
            printf("Priority: %s\n", pkg_info.priority ? pkg_info.priority : "(unknown)");
            printf("Homepage: %s\n", pkg_info.homepage ? pkg_info.homepage : "(unknown)");
            printf("Files installed: %d\n", pkg_info.file_count);
            runepkg_pack_free_package_info(&pkg_info);
            return 0;
        } else {
            printf("Failed to read package info for %s %s.\n", exact_match_name, exact_match_version);
            runepkg_pack_free_package_info(&pkg_info);
            return -1;
        }
    } else {
        // Multiple matches or no matches - show suggestions
        printf("Looking for package... '%s' did you mean?\n", package_name);
        char suggestions[100][PATH_MAX];
        int match_count = runepkg_util_get_package_suggestions(package_name, g_runepkg_db_dir, suggestions, 100);
        if (match_count > 0) {
            const char *items[100];
            for (int i = 0; i < match_count; i++) {
                items[i] = suggestions[i];
            }
            runepkg_util_print_columns(items, match_count);
        }
        return -2; // Special code: showed suggestions, no status shown
    }
}

void handle_search(const char *file_pattern) {
    if (!file_pattern || !g_runepkg_db_dir) {
        printf("Error: Invalid file pattern or config.\n");
        return;
    }

    print_package_data_header();

    DIR *dir = opendir(g_runepkg_db_dir);
    if (!dir) {
        printf("Error: Cannot open runepkg database directory: %s\n", g_runepkg_db_dir);
        return;
    }

    struct dirent *entry;
    int found_matches = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR) continue;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        // Parse package name and version from directory name
        char *pkg_name = NULL;
        char *pkg_version = NULL;
        char *last_dash = strrchr(entry->d_name, '-');
        if (last_dash) {
            size_t name_len = last_dash - entry->d_name;
            pkg_name = malloc(name_len + 1);
            if (pkg_name) {
                strncpy(pkg_name, entry->d_name, name_len);
                pkg_name[name_len] = '\0';
            }
            pkg_version = strdup(last_dash + 1);
        } else {
            pkg_name = strdup(entry->d_name);
            pkg_version = strdup("");
        }

        // Read package info
        PkgInfo pkg_info;
        runepkg_pack_init_package_info(&pkg_info);

        if (runepkg_storage_read_package_info(pkg_name, pkg_version, &pkg_info) == 0) {
            // Check if any files match the pattern
            for (int i = 0; i < pkg_info.file_count; i++) {
                if (strstr(pkg_info.file_list[i], file_pattern) != NULL) {
                    printf("%s: %s\n", pkg_name, pkg_info.file_list[i]);
                    found_matches = 1;
                }
            }
        }

        runepkg_pack_free_package_info(&pkg_info);
        free(pkg_name);
        free(pkg_version);
    }

    closedir(dir);

    if (!found_matches) {
        printf("No packages found containing files matching '%s'\n", file_pattern);
    }
}

void handle_print_config(void) {
    printf("runepkg Configuration:\n");
    printf("=====================\n");
    if (g_runepkg_base_dir) printf("  Base Directory:     %s\n", g_runepkg_base_dir);
    else printf("  Base Directory:     (not set)\n");
    if (g_control_dir) printf("  Control Directory:  %s\n", g_control_dir);
    else printf("  Control Directory:  (not set)\n");
    if (g_install_dir_internal) printf("  Install Directory:  %s\n", g_install_dir_internal);
    else printf("  Install Directory:  (not set)\n");
    if (g_system_install_root) printf("  System Install Root: %s\n", g_system_install_root);
    else printf("  System Install Root: (not set)\n");
    if (g_runepkg_db_dir) printf("  Database Directory: %s\n", g_runepkg_db_dir);
    else printf("  Database Directory: (not set)\n");
}

void handle_print_config_file(void) {
    char *config_path = runepkg_get_config_file_path();
    if (config_path) {
        printf("Configuration file in use: %s\n", config_path);
        free(config_path);
    } else {
        printf("No configuration file found.\n");
        printf("Searched locations:\n");
        printf("  1. $RUNEPKG_CONFIG_PATH environment variable\n");
        printf("  2. /etc/runepkg/runepkgconfig (system-wide)\n");
        printf("  3. ~/.runepkgconfig (user-specific)\n");
    }
}

void handle_print_pkglist_file(void) {
    printf("Autocomplete files:\n");
    if (g_pkglist_txt_path) printf("  Text file: %s\n", g_pkglist_txt_path);
    else printf("  Text file: (not set)\n");
    if (g_pkglist_bin_path) printf("  Binary file: %s\n", g_pkglist_bin_path);
    else printf("  Binary file: (not set)\n");
}

void handle_update_pkglist(void) {
    FILE *txt_file = fopen(g_pkglist_txt_path, "w");
    if (!txt_file) {
        fprintf(stderr, "Error: Cannot open runepkg_autocomplete.txt for writing: %s\n", g_pkglist_txt_path);
        return;
    }

    FILE *bin_file = fopen(g_pkglist_bin_path, "w");
    if (!bin_file) {
        fprintf(stderr, "Error: Cannot open runepkg_autocomplete.bin for writing: %s\n", g_pkglist_bin_path);
        fclose(txt_file);
        return;
    }

    // Scan installed packages
    if (g_runepkg_db_dir) {
        DIR *dir = opendir(g_runepkg_db_dir);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type != DT_DIR) continue;
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

                // Use the full directory name as package name
                fprintf(txt_file, "%s\n", entry->d_name);
                fprintf(bin_file, "%s\n", entry->d_name);
            }
            closedir(dir);
        }
    }

    fclose(txt_file);
    fclose(bin_file);

    runepkg_log_verbose("Autocomplete list updated.\n");
}

