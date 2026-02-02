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
#include <time.h>
#include <unistd.h>

#include "runepkg_handle.h"
#include "runepkg_config.h"
#include "runepkg_pack.h"
#include "runepkg_hash.h"
#include "runepkg_storage.h"
#include "runepkg_util.h"

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
    
    /* TODO: Load installed packages into hash table from persistent storage */
    
    return 0;
}

void runepkg_cleanup(void) {
    runepkg_log_verbose("Cleaning up runepkg environment...\n");
    
    if (runepkg_main_hash_table) {
        runepkg_hash_destroy_table(runepkg_main_hash_table);
        runepkg_main_hash_table = NULL;
    }
    
    runepkg_config_cleanup();
    
    runepkg_log_verbose("runepkg cleanup completed.\n");
}

void handle_install_stdin(void) {
    char line[PATH_MAX * 2];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *trimmed = runepkg_util_trim_whitespace(line);
        if (!trimmed || trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }

        char *token = strtok(trimmed, " \t");
        while (token) {
            if (strstr(token, ".deb") != NULL) {
                handle_install(token);
            }
            token = strtok(NULL, " \t");
        }
    }
}

void handle_install_listfile(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("Error: Cannot open list file: %s\n", path);
        return;
    }

    char line[PATH_MAX * 2];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *trimmed = runepkg_util_trim_whitespace(line);
        if (!trimmed || trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }

        char *token = strtok(trimmed, " \t");
        while (token) {
            if (strstr(token, ".deb") != NULL) {
                handle_install(token);
            }
            token = strtok(NULL, " \t");
        }
    }

    fclose(fp);
}

void handle_install(const char *deb_file_path) {
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    runepkg_log_verbose("Installing package from: %s\n", deb_file_path);

    if (g_verbose_mode) {
        struct stat file_stat;
        if (stat(deb_file_path, &file_stat) == 0) {
            printf("%ld bytes\n", file_stat.st_size);
            runepkg_log_verbose("File permissions: %o\n", file_stat.st_mode & 0777);
            runepkg_log_verbose("File last modified: %s", ctime(&file_stat.st_mtime));
        } else {
            printf("FAILED to stat file\n");
        }
    }

    if (!g_control_dir) {
        runepkg_log_verbose("ERROR: g_control_dir is NULL - configuration not loaded properly\n");
        return;
    }

    PkgInfo pkg_info;
    runepkg_pack_init_package_info(&pkg_info);

    int result = runepkg_pack_extract_and_collect_info(deb_file_path, g_control_dir, &pkg_info);

    if (result == 0) {
        if (g_verbose_mode) {
            runepkg_pack_print_package_info(&pkg_info);
        } else {
            printf("Package: %s (%s)\n",
                   pkg_info.package_name ? pkg_info.package_name : "(unknown)",
                   pkg_info.version ? pkg_info.version : "(unknown)");
        }

        if (runepkg_main_hash_table) {
            if (runepkg_hash_add_package(runepkg_main_hash_table, &pkg_info) == 0) {
                if (!g_verbose_mode) {
                    printf("Internal database: added\n");
                } else {
                    printf("Package successfully added to internal database.\n\n");
                }

                PkgInfo *stored_pkg = runepkg_hash_search(runepkg_main_hash_table, pkg_info.package_name);
                if (stored_pkg && g_verbose_mode) {
                    runepkg_hash_print_package_info(stored_pkg);
                }
            } else {
                printf("Warning: Failed to add package to internal database.\n");
            }
        }

        if (pkg_info.package_name && pkg_info.version) {
            if (runepkg_storage_create_package_directory(pkg_info.package_name, pkg_info.version) == 0) {
                if (runepkg_storage_write_package_info(pkg_info.package_name, pkg_info.version, &pkg_info) == 0) {
                    if (!g_verbose_mode) {
                        printf("Persistent storage: added\n");
                    } else {
                        printf("Package successfully added to persistent storage.\n");
                    }
                } else {
                    printf("Warning: Failed to write package info to persistent storage.\n");
                }
            } else {
                printf("Warning: Failed to create package directory in persistent storage.\n");
            }
        } else {
            printf("Warning: Cannot add to persistent storage - missing package name or version.\n");
        }

        if (g_system_install_root && pkg_info.data_dir_path && pkg_info.file_count > 0 && pkg_info.file_list) {
            int install_errors = 0;
            for (int i = 0; i < pkg_info.file_count; i++) {
                const char *rel = pkg_info.file_list[i];
                if (!rel || rel[0] == '\0') continue;

                char *src = runepkg_util_concat_path(pkg_info.data_dir_path, rel);
                char *dst = runepkg_util_concat_path(g_system_install_root, rel);
                if (!src || !dst) {
                    runepkg_util_free_and_null(&src);
                    runepkg_util_free_and_null(&dst);
                    install_errors++;
                    continue;
                }

                struct stat st;
                if (lstat(src, &st) != 0) {
                    runepkg_log_verbose("Install: failed to stat %s: %s\n", src, strerror(errno));
                    runepkg_util_free_and_null(&src);
                    runepkg_util_free_and_null(&dst);
                    install_errors++;
                    continue;
                }

                if (S_ISDIR(st.st_mode)) {
                    if (runepkg_util_create_dir_recursive(dst, 0755) != 0) install_errors++;
                } else if (S_ISREG(st.st_mode)) {
                    char *dst_copy = strdup(dst);
                    if (dst_copy) {
                        char *parent = dirname(dst_copy);
                        if (parent) runepkg_util_create_dir_recursive(parent, 0755);
                        free(dst_copy);
                    }
                    if (runepkg_util_copy_file(src, dst) != 0) install_errors++;
                } else if (S_ISLNK(st.st_mode)) {
                    char link_target[PATH_MAX];
                    ssize_t len = readlink(src, link_target, sizeof(link_target) - 1);
                    if (len >= 0) {
                        link_target[len] = '\0';
                        char *dst_copy = strdup(dst);
                        if (dst_copy) {
                            char *parent = dirname(dst_copy);
                            if (parent) runepkg_util_create_dir_recursive(parent, 0755);
                            free(dst_copy);
                        }
                        unlink(dst);
                        if (symlink(link_target, dst) != 0) install_errors++;
                    } else {
                        runepkg_log_verbose("Install: failed to read symlink %s\n", src);
                        install_errors++;
                    }
                }

                runepkg_util_free_and_null(&src);
                runepkg_util_free_and_null(&dst);
            }

            if (install_errors == 0) printf("Files installed to: %s\n", g_system_install_root);
            else printf("Install completed with %d file errors.\n", install_errors);
        }

        printf("Package information collection completed successfully.\n");
    } else {
        printf("Error: Failed to extract package or collect information.\n");
    }

    runepkg_pack_free_package_info(&pkg_info);

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double install_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
    runepkg_log_verbose("Total installation time: %.6f seconds\n", install_time);
}

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

void handle_remove(const char *package_name) {
    if (!package_name || package_name[0] == '\0') {
        printf("Error: remove requires a package name.\n");
        return;
    }

    char name_buf[PATH_MAX];
    runepkg_util_safe_strncpy(name_buf, package_name, sizeof(name_buf));
    char *trimmed = runepkg_util_trim_whitespace(name_buf);
    if (!trimmed || trimmed[0] == '\0') {
        printf("Error: remove requires a package name.\n");
        return;
    }

    if (!g_runepkg_db_dir) {
        printf("Error: runepkg database directory not configured.\n");
        return;
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
            return;
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
            printf("Error: multiple installed packages match '%s'. Use full name from list.\n", package_name);
            return;
        } else {
            printf("Error: package not installed: %s\n", trimmed);
            return;
        }
    }

    PkgInfo pkg_info;
    if (runepkg_storage_read_package_info(pkg_name, pkg_version, &pkg_info) != 0) {
        printf("Error: package not installed: %s-%s\n", pkg_name, pkg_version);
        return;
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
    } else {
        printf("Removed package: %s-%s\n", pkg_name, pkg_version);
    }
}

void handle_version(void) {
    printf("runepkg v0.1.0 - The Runar Linux package manager\n");
    printf("Copyright (c) 2025 runepkg (Runar Linux) All rights reserved.\n");
    printf("Licensed under GPL v3\n");
}

void handle_list(void) {
    runepkg_log_verbose("Listing installed packages...\n");
    printf("Listing installed packages...\n");
    if (runepkg_storage_list_packages() != 0) {
        printf("Warning: Failed to list packages from persistent storage.\n");
    }
    if (g_runepkg_db_dir) runepkg_log_verbose("  Database dir: %s\n", g_runepkg_db_dir);
}

void handle_status(const char *package_name) {
    printf("Showing status for package: %s (placeholder)\n", package_name);
}

void handle_search(const char *query) {
    printf("Searching for packages with query: %s (placeholder)\n", query);
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

