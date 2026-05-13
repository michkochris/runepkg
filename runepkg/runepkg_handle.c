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
#include "runepkg_md5sums.h"
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
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0 && strcmp(entry->d_name, "lists") != 0) {
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
        }
    }

    /* Initialize installing packages hash table for cycle detection */
    if (!installing_packages) {
        installing_packages = runepkg_hash_create_table(INITIAL_HASH_TABLE_SIZE);
        if (!installing_packages) {
            fprintf(stderr, "Error: Failed to create installing hash table.\n");
            return -1;
        }
    }

    /* One concise verbose message summarizing hash table initialization */
    runepkg_log_verbose("Hash tables initialized for package management.\n");
    
    /* TODO: Load installed packages into hash table from persistent storage */
    /* COMPLETED: The call is now handled by the helper, validation done via assumption */
    
    /* Load installed packages into hash table */
    if (g_runepkg_db_dir) {
        DIR *dir = opendir(g_runepkg_db_dir);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0 && strcmp(entry->d_name, "lists") != 0) {
                    // Parse package name and version from directory name.
                    // Prefer the last '-' that is followed by a digit (version start),
                    // since Debian versions often contain dashes.
                    char pkg_name[PATH_MAX] = {0};
                    char pkg_version[PATH_MAX] = {0};
                    const char *ver_dash = NULL;
                    for (const char *p = entry->d_name; *p; p++) {
                        if (*p == '-' && *(p + 1) && isdigit((unsigned char)*(p + 1))) {
                            ver_dash = p;
                            break; /* first dash before version */
                        }
                    }
                    if (ver_dash && ver_dash != entry->d_name) {
                        size_t name_len = (size_t)(ver_dash - entry->d_name);
                        if (name_len < sizeof(pkg_name)) {
                            memcpy(pkg_name, entry->d_name, name_len);
                            pkg_name[name_len] = '\0';
                            strncpy(pkg_version, ver_dash + 1, sizeof(pkg_version) - 1);
                            pkg_version[sizeof(pkg_version) - 1] = '\0';
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
    /* One concise message for destroyed hash tables */
    runepkg_log_verbose("Hash tables destroyed and memory freed.\n");

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
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "lists") == 0) continue;

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
            printf("Looking for package... '%s' did you mean?\n\n", package_name);
            
            // Collect and display matching packages
            char matches[100][PATH_MAX];
            int match_idx = 0;
            
            // Re-scan directory to collect matches
            DIR *list_dir = opendir(g_runepkg_db_dir);
            if (list_dir) {
                struct dirent *entry;
                while ((entry = readdir(list_dir)) != NULL && match_idx < 100) {
                    if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0 && strcmp(entry->d_name, "lists") != 0) {
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
                printf("Looking for package... '%s' did you mean?\n\n", package_name);
                
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
        // Execute prerm
        extern int runepkg_execute_maintainer_script(const char *script_path, const PkgInfo *pkg_info, const char *action);
        runepkg_execute_maintainer_script(pkg_info.prerm, &pkg_info, "remove");

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

        // Execute postrm
        runepkg_execute_maintainer_script(pkg_info.postrm, &pkg_info, "purge");
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
    runepkg_util_motd();
    printf("*Built with ❤️ for the old school GNU/Linux community...*\n");
    printf("Copyright (c) 2025 runepkg (Runar Linux) All rights reserved.\n");
    printf("Contact: [michkochris@gmail.com] | [runepkg@gmail.com]\n");
}

void handle_list(const char *pattern) {
    runepkg_log_verbose("Listing installed packages...\n");

    print_package_data_header();

    printf("Listing installed packages...\n\n");
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
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "lists") == 0) continue;

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
        printf("Looking for package... '%s' did you mean?\n\n", package_name);
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
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "lists") == 0) continue;

        // Parse package name and version from directory name
        // Use the smarter logic: find the first dash followed by a digit
        char *pkg_name = NULL;
        char *pkg_version = NULL;
        char *ver_dash = NULL;

        for (char *p = entry->d_name; *p; p++) {
            if (*p == '-' && *(p + 1) && isdigit(*(p + 1))) {
                ver_dash = p;
                break;
            }
        }

        if (ver_dash) {
            size_t name_len = ver_dash - entry->d_name;
            pkg_name = malloc(name_len + 1);
            if (pkg_name) {
                strncpy(pkg_name, entry->d_name, name_len);
                pkg_name[name_len] = '\0';
            }
            pkg_version = strdup(ver_dash + 1);
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
                    // Match dpkg -S style: "package: /path/to/file"
                    printf("%s: /%s\n", pkg_name, pkg_info.file_list[i]);
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

void handle_list_files(const char *package_name) {
    if (!package_name || !g_runepkg_db_dir) {
        printf("Error: Invalid package name or config.\n");
        return;
    }

    print_package_data_header();

    // Try to resolve the package directory similarly to handle_status
    DIR *dir = opendir(g_runepkg_db_dir);
    if (!dir) {
        printf("Error: Cannot open runepkg database directory: %s\n", g_runepkg_db_dir);
        return;
    }

    struct dirent *entry;
    int match_count = 0;
    char match_name[PATH_MAX] = {0};
    char match_version[PATH_MAX] = {0};

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR) continue;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "lists") == 0) continue;

        // Exact match of full dir name
        if (strcmp(entry->d_name, package_name) == 0) {
            // parse version after last '-'
            char *last_dash = NULL;
            for (char *p = entry->d_name; *p; p++) {
                if (!last_dash && *p == '-' && *(p + 1) && isdigit(*(p + 1))) {
                    last_dash = p;
                }
            }
            if (last_dash) {
                size_t name_len = last_dash - entry->d_name;
                strncpy(match_name, entry->d_name, name_len);
                match_name[name_len] = '\0';
                strncpy(match_version, last_dash + 1, sizeof(match_version) - 1);
            } else {
                strncpy(match_name, entry->d_name, sizeof(match_name) - 1);
                match_version[0] = '\0';
            }
            match_count = 1;
            break;
        }

        // prefix match like 'binutils' matching 'binutils-2.45-8'
        size_t input_len = strlen(package_name);
        if (strncmp(entry->d_name, package_name, input_len) == 0 && entry->d_name[input_len] == '-') {
            const char *ver = entry->d_name + input_len + 1;
            if (*ver != '\0') {
                match_count++;
                if (match_count == 1) {
                    strncpy(match_name, package_name, sizeof(match_name) - 1);
                    strncpy(match_version, ver, sizeof(match_version) - 1);
                }
            }
        }
    }
    closedir(dir);

    if (match_count == 1) {
        PkgInfo pkg_info;
        runepkg_pack_init_package_info(&pkg_info);
        if (runepkg_storage_read_package_info(match_name, match_version, &pkg_info) == 0) {
            if (pkg_info.file_count > 0 && pkg_info.file_list) {
                for (int i = 0; i < pkg_info.file_count; i++) {
                    if (pkg_info.file_list[i] && pkg_info.file_list[i][0]) printf("%s\n", pkg_info.file_list[i]);
                }
            } else {
                printf("No files recorded for %s-%s\n", match_name, match_version);
            }
            runepkg_pack_free_package_info(&pkg_info);
            return;
        } else {
            printf("Failed to read package info for %s %s.\n", match_name, match_version);
            runepkg_pack_free_package_info(&pkg_info);
            return;
        }
    }

    // Multiple or no matches - show suggestions
    printf("Looking for package... '%s' did you mean?\n\n", package_name);
    char suggestions[100][PATH_MAX];
    int suggestion_count = runepkg_util_get_package_suggestions(package_name, g_runepkg_db_dir, suggestions, 100);
    if (suggestion_count > 0) {
        const char *items[100];
        for (int i = 0; i < suggestion_count; i++) items[i] = suggestions[i];
        runepkg_util_print_columns(items, suggestion_count);
    }
}

void handle_print_config(void) {
    printf("runepkg Configuration:\n");
    printf("=====================\n");
    if (g_runepkg_base_dir) printf("  Base Directory:      %s\n", g_runepkg_base_dir);
    else printf("  Base Directory:      (not set)\n");
    if (g_control_dir) printf("  Control Directory:   %s\n", g_control_dir);
    else printf("  Control Directory:   (not set)\n");
    if (g_install_dir_internal) printf("  Install Directory:   %s\n", g_install_dir_internal);
    else printf("  Install Directory:   (not set)\n");
    if (g_system_install_root) printf("  System Install Root: %s\n", g_system_install_root);
    else printf("  System Install Root: (not set)\n");
    if (g_runepkg_db_dir) printf("  Database Directory:  %s\n", g_runepkg_db_dir);
    else printf("  Database Directory:  (not set)\n");
    if (g_download_dir) printf("  Download Directory:  %s\n", g_download_dir);
    else printf("  Download Directory:  (not set)\n");
    if (g_build_dir) printf("  Build Directory:     %s\n", g_build_dir);
    else printf("  Build Directory:     (not set)\n");
    if (g_debs_dir) printf("  Debs Directory:      %s\n", g_debs_dir);
    else printf("  Debs Directory:      (not set)\n");

    printf("  Cleanup: %s\n", g_cleanup_extract_dirs ? "yes" : "no");
    extern bool g_md5_checks;
    printf("  MD5 Checks: %s\n", g_md5_checks ? "yes" : "no");

    if (g_sources_count > 0 && g_sources) {
        printf("\nConfigured Sources:\n");
        printf("==================\n");
        for (int i = 0; i < g_sources_count; i++) {
            if (g_sources[i]) {
                printf("  [%d] %s %s %s %s\n", i + 1,
                       g_sources[i]->type,
                       g_sources[i]->url,
                       g_sources[i]->suite,
                       g_sources[i]->components ? g_sources[i]->components : "");
            }
        }
    } else {
        printf("\nNo sources configured.\n");
    }
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

    char **packages = NULL;
    int count = 0;

    // Scan installed packages
    if (g_runepkg_db_dir) {
        DIR *dir = opendir(g_runepkg_db_dir);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type != DT_DIR) continue;
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "lists") == 0) continue;

                packages = realloc(packages, (count + 1) * sizeof(char *));
                if (packages) {
                    packages[count++] = strdup(entry->d_name);
                }
            }
            closedir(dir);
        }
    }

    // Scan build directory
    if (g_build_dir) {
        DIR *dir = opendir(g_build_dir);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type != DT_DIR) continue;
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

                // Check for duplicates
                bool exists = false;
                for (int i = 0; i < count; i++) {
                    if (strcmp(packages[i], entry->d_name) == 0) {
                        exists = true;
                        break;
                    }
                }
                if (exists) continue;

                packages = realloc(packages, (count + 1) * sizeof(char *));
                if (packages) {
                    packages[count++] = strdup(entry->d_name);
                }
            }
            closedir(dir);
        }
    }

    // Write sorted unique list to text file
    if (count > 0) {
        // qsort(packages, count, sizeof(char *), compare_packages); // compare_packages is static in storage.c, need it here or use local
        // Using a simple bubble sort or just relying on binary index for speed,
        // but for text file let's just write them.
        for (int i = 0; i < count; i++) {
            // If it's in build_dir, write the absolute path
            if (g_build_dir) {
                char *full = runepkg_util_concat_path(g_build_dir, packages[i]);
                if (full) {
                    struct stat st;
                    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
                        fprintf(txt_file, "%s\n", full);
                        free(full);
                        free(packages[i]);
                        continue;
                    }
                    free(full);
                }
            }
            fprintf(txt_file, "%s\n", packages[i]);
            free(packages[i]);
        }
        free(packages);
    }

    fclose(txt_file);

    /* Build the binary mmap index so completion is fast. This avoids
     * overwriting the proper binary format with plain text. */
    if (runepkg_storage_build_autocomplete_index() != 0) {
        runepkg_log_verbose("Warning: Failed to build binary autocomplete index.\n");
    } else {
        runepkg_log_verbose("Autocomplete list updated and binary index built.\n");
    }
}

int handle_unpack(const char *deb_path) {
    if (!deb_path) return -1;
    if (!g_build_dir) {
        runepkg_util_error("build_dir is not configured. Professional unpacking requires a workspace.\n");
        return -1;
    }

    runepkg_log_verbose("Unpacking %s to build_dir...\n", deb_path);

    PkgInfo pkg_info;
    runepkg_pack_init_package_info(&pkg_info);

    // Temporary extraction to get name/version
    if (runepkg_pack_extract_and_collect_info(deb_path, g_control_dir, &pkg_info) != 0) {
        runepkg_util_error("Failed to extract metadata from %s\n", deb_path);
        runepkg_pack_free_package_info(&pkg_info);
        return -1;
    }

    size_t pkg_ver_len = strlen(pkg_info.package_name) + strlen(pkg_info.version) + 2;
    char *pkg_ver_name = malloc(pkg_ver_len);
    if (!pkg_ver_name) {
        runepkg_pack_cleanup_extraction_workspace(&pkg_info);
        runepkg_pack_free_package_info(&pkg_info);
        return -1;
    }
    snprintf(pkg_ver_name, pkg_ver_len, "%s-%s", pkg_info.package_name, pkg_info.version);

    char *target_dir = runepkg_util_concat_path(g_build_dir, pkg_ver_name);
    free(pkg_ver_name);

    runepkg_pack_cleanup_extraction_workspace(&pkg_info);
    runepkg_pack_free_package_info(&pkg_info);

    if (runepkg_util_create_dir_recursive(target_dir, 0755) != 0) {
        free(target_dir);
        return -1;
    }

    if (runepkg_util_extract_deb_complete(deb_path, target_dir) != 0) {
        runepkg_util_error("Unpack failed for %s\n", deb_path);
        free(target_dir);
        return -1;
    }

    printf("Successfully unpacked %s to %s\n", deb_path, target_dir);
    free(target_dir);
    return 0;
}

int handle_build(const char *source_dir, const char *output_name) {
    const char *src = source_dir;
    if (!src) src = g_build_dir;
    if (!src) {
        runepkg_util_error("No source directory specified and build_dir not configured.\n");
        return -1;
    }

    char *out_allocated = NULL;
    const char *out = output_name;

    if (!out) {
        // Try to determine professional filename from control file
        char *control_path = runepkg_util_concat_path(src, "control/control");
        if (control_path && runepkg_util_file_exists(control_path)) {
            char *pkg = runepkg_util_get_config_value(control_path, "Package", ':');
            char *ver = runepkg_util_get_config_value(control_path, "Version", ':');
            char *arch = runepkg_util_get_config_value(control_path, "Architecture", ':');

            if (pkg && ver && arch) {
                size_t len = strlen(pkg) + strlen(ver) + strlen(arch) + 7; // _ _ .deb \0
                char *filename = malloc(len);
                if (filename) {
                    snprintf(filename, len, "%s_%s_%s.deb", pkg, ver, arch);

                    // Route to g_debs_dir
                    if (g_debs_dir) {
                        out_allocated = runepkg_util_concat_path(g_debs_dir, filename);
                        free(filename);
                    } else {
                        out_allocated = filename;
                    }
                    out = out_allocated;
                }
            }
            free(pkg); free(ver); free(arch);
        }
        free(control_path);

        // Fallback to basename if control parsing failed
        if (!out) {
            char *bname = basename((char*)src);
            char *filename = malloc(strlen(bname) + 5);
            if (filename) {
                sprintf(filename, "%s.deb", bname);

                // Route to g_debs_dir
                if (g_debs_dir) {
                    out_allocated = runepkg_util_concat_path(g_debs_dir, filename);
                    free(filename);
                } else {
                    out_allocated = filename;
                }
                out = out_allocated;
            }
        }
    }

    if (!out) return -1;

    int ret = runepkg_util_create_deb(src, out);
    if (ret == 0) {
        printf("\033[1;32m[build]\033[0m Successfully built package: %s\n", out);
    }

    free(out_allocated);
    return ret;
}

int handle_md5_check(const char *package_name) {
    if (!package_name) return -1;

    print_package_data_header();

    // Find installed package info
    PkgInfo pkg_info;
    runepkg_pack_init_package_info(&pkg_info);

    // Simplified: reuse logic to find package dir from handle_status
    DIR *dir = opendir(g_runepkg_db_dir);
    if (!dir) return -1;

    struct dirent *entry;
    char found_pkg[PATH_MAX] = {0};
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR) continue;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "lists") == 0) continue;

        if (strcmp(entry->d_name, package_name) == 0) {
            strncpy(found_pkg, entry->d_name, sizeof(found_pkg)-1);
            break;
        }
        size_t len = strlen(package_name);
        if (strncmp(entry->d_name, package_name, len) == 0 && entry->d_name[len] == '-') {
            strncpy(found_pkg, entry->d_name, sizeof(found_pkg)-1);
            break;
        }
    }
    closedir(dir);

    if (!found_pkg[0]) {
        runepkg_util_error("Package %s not found.\n", package_name);
        return -1;
    }

    // Parse version from found_pkg
    char *v = strrchr(found_pkg, '-');
    if (!v) return -1;
    *v = '\0';
    char *version = v + 1;

    if (runepkg_storage_read_package_info(found_pkg, version, &pkg_info) != 0) {
        runepkg_util_error("Failed to read metadata for %s-%s\n", found_pkg, version);
        return -1;
    }

    // Use system root for verification
    if (!g_system_install_root) return -1;

    char pkg_db_path[PATH_MAX];
    runepkg_storage_get_package_path(found_pkg, version, pkg_db_path);
    char *md5sums_path = runepkg_util_concat_path(pkg_db_path, "md5sums");

    if (!runepkg_util_file_exists(md5sums_path)) {
        printf("No md5sums file for %s, cannot verify.\n", package_name);
        free(md5sums_path);
        runepkg_pack_free_package_info(&pkg_info);
        return -1;
    }

    FILE *f = fopen(md5sums_path, "r");
    if (!f) {
        free(md5sums_path);
        return -1;
    }

    printf("\033[1;34m[integrity]\033[0m Verifying %s integrity...\n", found_pkg);
    char line[PATH_MAX + 64];
    int total = 0, ok = 0, fail = 0;
    while (fgets(line, sizeof(line), f)) {
        char exp[33], rel[PATH_MAX];
        if (sscanf(line, "%32s  %s", exp, rel) != 2) continue;
        total++;

        char *full = runepkg_util_concat_path(g_system_install_root, rel);

        char act[33];
        if (runepkg_md5_file(full, act) == 0) {
            if (strcmp(exp, act) == 0) {
                ok++;
            } else {
                printf("\033[1;31mFAILED\033[0m: %s (expected %s, got %s)\n", rel, exp, act);
                fail++;
            }
        } else {
            printf("\033[1;33mMISSING\033[0m: %s\n", rel);
            fail++;
        }
        free(full);
    }
    fclose(f);
    free(md5sums_path);

    if (fail == 0) {
        printf("\033[1;32m[integrity]\033[0m Verification complete: all %d files OK.\n", total);
    } else {
        printf("\033[1;31m[integrity]\033[0m Verification complete: %d ok, %d failed out of %d files.\n", ok, fail, total);
    }

    pkg_info.md5_verified = (fail == 0);
    runepkg_storage_write_package_info(found_pkg, version, &pkg_info);

    runepkg_pack_free_package_info(&pkg_info);
    return (fail == 0) ? 0 : -1;
}
