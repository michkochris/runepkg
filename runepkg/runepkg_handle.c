/*****************************************************************************
 * Filename:    runepkg_handle.c
 * Author:      <michkochris@gmail.com>
 * Date:        2026-08-26
 * Description: High-level request handlers and initialization/cleanup for runepkg
 * LICENSE:     GPL v3
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include <sys/mman.h>
#include <fcntl.h>

#include "runepkg_portable.h"
#include "runepkg_defensive.h"
#include "runepkg_handle.h"
#include "runepkg_config.h"
#include "runepkg_pack.h"
#include "runepkg_hash.h"
#include "runepkg_storage.h"
#include "runepkg_util.h"
#include "runepkg_md5sums.h"
#include "runepkg_crypto.h"

#ifdef ENABLE_CPP_FFI
#include "runepkg_cpp_ffi.h"
#endif

#include "runepkg_completion.h"

static int compare_pkgs(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

#ifdef __ANDROID__
int getloadavg(double loadavg[], int nelem) {
    FILE *fp = fopen("/proc/loadavg", "r");
    int count = 0;
    int i;
    if (!fp) return -1;
    
    for (i = 0; i < nelem && i < 3; i++) {
        if (fscanf(fp, "%lf", &loadavg[i]) != 1) break;
        count++;
    }
    
    fclose(fp);
    return (count > 0) ? count : -1;
}
#endif

runepkg_hash_table_t *installing_packages = NULL;

int print_package_data_header(void) {
    size_t pkg_count;
    off_t total_installed_size = 0;
    size_t i;
    struct statvfs vfs;
    off_t avail_space = 0;
    char used_str[32], avail_str[32];

    if (!runepkg_main_hash_table) {
        return 0;
    }

    pkg_count = runepkg_main_hash_table->count;

    for (i = 0; i < runepkg_main_hash_table->size; i++) {
        runepkg_hash_node_t *node = runepkg_main_hash_table->buckets[i];
        while (node) {
            if (node->data.installed_size) {
                total_installed_size += (off_t)atol(node->data.installed_size) * 1024;
            }
            node = node->next;
        }
    }

    if (g_runepkg_db_dir && statvfs(g_runepkg_db_dir, &vfs) == 0) {
        avail_space = (off_t)vfs.f_bavail * vfs.f_frsize;
    }

    runepkg_util_format_size(total_installed_size, used_str, sizeof(used_str));
    runepkg_util_format_size(avail_space, avail_str, sizeof(avail_str));

    printf("Reading package data: %lu packages, %s used, %s available\n",
           (unsigned long)pkg_count, used_str, avail_str);

    return (int)pkg_count;
}

int runepkg_init(void) {
    runepkg_log_verbose("Initializing runepkg...\n");
    
    runepkg_init_paths();
    
    if (!runepkg_main_hash_table) {
        runepkg_main_hash_table = runepkg_hash_create_table(INITIAL_HASH_TABLE_SIZE);
        if (!runepkg_main_hash_table) {
            fprintf(stderr, "Error: Failed to create hash table for package management.\n");
            return -1;
        }
    }

    if (!installing_packages) {
        installing_packages = runepkg_hash_create_table(32);
        if (!installing_packages) {
            fprintf(stderr, "Error: Failed to create installing hash table.\n");
            return -1;
        }
    }
    if (!g_batch_planned_packages) {
        g_batch_planned_packages = runepkg_hash_create_table(128);
        if (!g_batch_planned_packages) {
            fprintf(stderr, "Error: Failed to create batch planning hash table.\n");
            return -1;
        }
    }

    runepkg_log_verbose("Hash tables initialized for package management.\n");

    if (g_completion_mode) {
        runepkg_log_verbose("Completion mode: skipping full package database load for performance.\n");
        return 0;
    }

    if (g_runepkg_db_dir) {
        DIR *dir;
        char *host_db_path;

        /* 1. Load native runepkg packages from top-level db dir */
        dir = opendir(g_runepkg_db_dir);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if ((entry->d_type == DT_DIR || entry->d_type == DT_UNKNOWN) && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0 && strcmp(entry->d_name, "lists") != 0 && strcmp(entry->d_name, "host") != 0) {
                    char pkg_name[PATH_MAX];
                    char pkg_version[PATH_MAX];
                    const char *ver_dash = NULL;

                    memset(pkg_name, 0, sizeof(pkg_name));
                    memset(pkg_version, 0, sizeof(pkg_version));

                    ver_dash = runepkg_util_find_version_separator(entry->d_name);
                    if (ver_dash && ver_dash != entry->d_name) {
                        size_t name_len = (size_t)(ver_dash - entry->d_name);
                        if (name_len < sizeof(pkg_name)) {
                            memcpy(pkg_name, entry->d_name, name_len);
                            pkg_name[name_len] = '\0';
                            runepkg_secure_strcpy(pkg_version, sizeof(pkg_version), ver_dash + 1);
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

        /* 2. Load persistent host-dpkg synced packages from host/ subdirectory */
        host_db_path = runepkg_util_concat_path(g_runepkg_db_dir, "host");
        if (host_db_path) {
            DIR *hdir = opendir(host_db_path);
            if (hdir) {
                struct dirent *hentry;
                while ((hentry = readdir(hdir)) != NULL) {
                    if ((hentry->d_type == DT_DIR || hentry->d_type == DT_UNKNOWN) && strcmp(hentry->d_name, ".") != 0 && strcmp(hentry->d_name, "..") != 0) {
                        char pkg_name[PATH_MAX];
                        char pkg_version[PATH_MAX];
                        const char *ver_dash = runepkg_util_find_version_separator(hentry->d_name);

                        if (ver_dash && ver_dash != hentry->d_name) {
                            size_t name_len = (size_t)(ver_dash - hentry->d_name);
                            runepkg_util_safe_strncpy(pkg_name, hentry->d_name, name_len + 1);
                            pkg_name[name_len] = '\0';
                            runepkg_secure_strcpy(pkg_version, sizeof(pkg_version), ver_dash + 1);

                            if (pkg_name[0] && pkg_version[0]) {
                                PkgInfo pkg_info;
                                /* We need a way to tell read_package_info to look in host/ */
                                /* Temporarily override g_runepkg_db_dir or use a new internal API */
                                char *old_db = g_runepkg_db_dir;
                                g_runepkg_db_dir = host_db_path;
                                if (runepkg_storage_read_package_info(pkg_name, pkg_version, &pkg_info) == 0) {
                                    runepkg_hash_add_package(runepkg_main_hash_table, &pkg_info);
                                    runepkg_pack_free_package_info(&pkg_info);
                                }
                                g_runepkg_db_dir = old_db;
                            }
                        }
                    }
                }
                closedir(hdir);
            }
            free(host_db_path);
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

    if (g_batch_planned_packages) {
        runepkg_hash_destroy_table(g_batch_planned_packages);
        g_batch_planned_packages = NULL;
    }
    runepkg_log_verbose("Hash tables destroyed and memory freed.\n");

    runepkg_config_cleanup();

    runepkg_log_verbose("runepkg cleanup completed.\n");
}

void handle_remove_stdin(void) {
    char line[PATH_MAX * 2];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *trimmed = runepkg_util_trim_whitespace(line);
        char *token;
        if (!trimmed || trimmed[0] == '\0') continue;

        token = strtok(trimmed, " \t");
        while (token) {
            handle_remove(token);
            token = strtok(NULL, " \t");
        }
    }
}

void handle_remove_listfile(const char *path) {
    FILE *fp = fopen(path, "r");
    char line[PATH_MAX * 2];

    if (!fp) {
        printf("Error: Cannot open list file: %s\n", path);
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *trimmed = runepkg_util_trim_whitespace(line);
        char *token;
        if (!trimmed || trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }

        token = strtok(trimmed, " \t");
        while (token) {
            handle_remove(token);
            token = strtok(NULL, " \t");
        }
    }

    fclose(fp);
}

int handle_remove(const char *package_name) {
    char name_buf[PATH_MAX];
    char *trimmed;
    char pkg_name[PATH_MAX];
    char pkg_version[PATH_MAX];
    const char *last_dash;
    PkgInfo pkg_info;

    if (!package_name || package_name[0] == '\0') {
        printf("Error: remove requires a package name.\n");
        return -1;
    }

    runepkg_util_safe_strncpy(name_buf, package_name, sizeof(name_buf));
    trimmed = runepkg_util_trim_whitespace(name_buf);
    if (!trimmed || trimmed[0] == '\0') {
        printf("Error: remove requires a package name.\n");
        return -1;
    }

    if (!g_runepkg_db_dir) {
        printf("Error: runepkg database directory not configured.\n");
        return -1;
    }

    memset(pkg_name, 0, sizeof(pkg_name));
    memset(pkg_version, 0, sizeof(pkg_version));

    last_dash = runepkg_util_find_version_separator(trimmed);
    if (last_dash && last_dash != trimmed) {
        size_t name_len = (size_t)(last_dash - trimmed);
        if (name_len < sizeof(pkg_name)) {
            memcpy(pkg_name, trimmed, name_len);
            pkg_name[name_len] = '\0';
            runepkg_secure_strcpy(pkg_version, sizeof(pkg_version), last_dash + 1);
        }
    }

    if (pkg_name[0] == '\0' || pkg_version[0] == '\0') {
        DIR *dir = opendir(g_runepkg_db_dir);
        struct dirent *entry;
        int match_count = 0;
        char match_name[PATH_MAX];
        char match_version[PATH_MAX];
        size_t input_len;

        if (!dir) {
            printf("Error: Cannot open runepkg database directory: %s\n", g_runepkg_db_dir);
            return -1;
        }

        memset(match_name, 0, sizeof(match_name));
        memset(match_version, 0, sizeof(match_version));
        input_len = strlen(trimmed);

        while ((entry = readdir(dir)) != NULL) {
            const char *ver_ptr;
            if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) continue;
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "lists") == 0) continue;

            ver_ptr = runepkg_util_find_version_separator(entry->d_name);
            if (ver_ptr && (size_t)(ver_ptr - entry->d_name) == input_len && strncmp(entry->d_name, trimmed, input_len) == 0) {
                match_count++;
                runepkg_secure_strcpy(match_name, sizeof(match_name), trimmed);
                runepkg_secure_strcpy(match_version, sizeof(match_version), ver_ptr + 1);
            }
        }

        closedir(dir);

        if (match_count == 1) {
            runepkg_secure_strcpy(pkg_name, sizeof(pkg_name), match_name);
            runepkg_secure_strcpy(pkg_version, sizeof(pkg_version), match_version);
        } else if (match_count > 1) {
            DIR *list_dir;
            print_package_data_header();
            printf("'%s' not found... did you mean?\n\n", package_name);

            list_dir = opendir(g_runepkg_db_dir);
            if (list_dir) {
                char matches[100][PATH_MAX];
                int match_idx = 0;
                struct dirent *sub_entry;
                while ((sub_entry = readdir(list_dir)) != NULL && match_idx < 100) {
                    if ((sub_entry->d_type == DT_DIR || sub_entry->d_type == DT_UNKNOWN) && strcmp(sub_entry->d_name, ".") != 0 && strcmp(sub_entry->d_name, "..") != 0 && strcmp(sub_entry->d_name, "lists") != 0) {
                        if (strstr(sub_entry->d_name, trimmed) != NULL) {
                            runepkg_secure_strcpy(matches[match_idx], sizeof(matches[match_idx]), sub_entry->d_name);
                            match_idx++;
                        }
                    }
                }
                closedir(list_dir);
                
                if (match_idx > 0) {
                    const char *items[100];
                    int i;
                    for (i = 0; i < match_idx; i++) {
                        items[i] = matches[i];
                    }
                    runepkg_util_print_columns(items, match_idx, "    ");
                }
            }
            
            return -2;
        } else {
            char suggestions[100][PATH_MAX];
            int suggestion_count = runepkg_util_get_package_suggestions(trimmed, g_runepkg_db_dir, suggestions, 100);
            
            if (suggestion_count > 0) {
                const char *items[100];
                int i;
                print_package_data_header();
                printf("'%s' not installed... did you mean?\n\n", package_name);
                
                for (i = 0; i < suggestion_count; i++) {
                    items[i] = suggestions[i];
                }
                runepkg_util_print_columns(items, suggestion_count, "    ");
                return -2;
            } else {
                printf("'%s' not installed... did you mean?\n\n", package_name);
                return -1;
            }
        }
    }

    if (runepkg_storage_read_package_info(pkg_name, pkg_version, &pkg_info) != 0) {
        printf("Error: package not installed: %s-%s\n", pkg_name, pkg_version);
        return -1;
    }

    if (g_verbose_mode) {
        char response[10];
        printf("Do you want to remove package %s-%s? [y/N] ", pkg_name, pkg_version);
        fflush(stdout);
        if (fgets(response, sizeof(response), stdin) == NULL || (response[0] != 'y' && response[0] != 'Y')) {
            printf("Removal cancelled.\n");
            runepkg_pack_free_package_info(&pkg_info);
            return -1;
        }
    }

    if (g_system_install_root && pkg_info.file_list && pkg_info.file_count > 0) {
        int i;
        extern int runepkg_execute_maintainer_script(const char *script_path, const PkgInfo *pkg_info, const char *action);
        runepkg_execute_maintainer_script(pkg_info.prerm, &pkg_info, "remove");

        for (i = 0; i < pkg_info.file_count; i++) {
            const char *rel = pkg_info.file_list[i];
            char *dst;
            if (!rel || rel[0] == '\0') continue;
            dst = runepkg_util_concat_path(g_system_install_root, rel);
            if (!dst) continue;
            if (unlink(dst) != 0) {
                runepkg_log_verbose("Remove: failed to delete %s\n", dst);
            }
            runepkg_util_free_and_null(&dst);
        }

        runepkg_execute_maintainer_script(pkg_info.postrm, &pkg_info, "purge");
    }

    runepkg_pack_free_package_info(&pkg_info);

    if (runepkg_storage_remove_package(pkg_name, pkg_version) != 0) {
        printf("Warning: failed to remove package metadata for %s-%s\n", pkg_name, pkg_version);
        return -1;
    }

    runepkg_storage_build_autocomplete_index();
    handle_update_pkglist();

    return 0;
}

void handle_list(const char *pattern) {
    int listed;
    runepkg_log_verbose("Listing installed packages...\n");

    print_package_data_header();

    printf("Listing installed packages...\n\n");
    listed = runepkg_storage_list_packages(pattern);
    if (pattern && listed == 0) {
        printf("No packages match '%s'.\n", pattern);
    }
    if (g_runepkg_db_dir) runepkg_log_verbose("  Database dir: %s\n", g_runepkg_db_dir);
}

int handle_status(const char *package_name) {
    DIR *dir;
    struct dirent *entry;
    int exact_match_count = 0;
    char exact_match_name[PATH_MAX];
    char exact_match_version[PATH_MAX];
    size_t name_len;

    if (!package_name || !g_runepkg_db_dir) {
        printf("Error: Invalid package name or config.\n");
        return -1;
    }

    print_package_data_header();

    dir = opendir(g_runepkg_db_dir);
    if (!dir) {
        printf("Error: Cannot open runepkg database directory: %s\n", g_runepkg_db_dir);
        return -1;
    }

    memset(exact_match_name, 0, sizeof(exact_match_name));
    memset(exact_match_version, 0, sizeof(exact_match_version));

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) continue;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "lists") == 0) continue;

        if (strcmp(entry->d_name, package_name) == 0) {
            const char *last_dash = NULL;
            exact_match_count = 1;
            last_dash = runepkg_util_find_version_separator(entry->d_name);
            if (last_dash) {
                name_len = (size_t)(last_dash - entry->d_name);
                runepkg_util_safe_strncpy(exact_match_name, entry->d_name, name_len + 1);
                exact_match_name[name_len] = '\0';
                runepkg_secure_strcpy(exact_match_version, sizeof(exact_match_version), last_dash + 1);
            } else {
                runepkg_secure_strcpy(exact_match_name, sizeof(exact_match_name), entry->d_name);
                exact_match_version[0] = '\0';
            }
            break;
        }

        {
            name_len = strlen(package_name);
            if (strncmp(entry->d_name, package_name, name_len) == 0 && entry->d_name[name_len] == '-') {
                const char *ver = entry->d_name + name_len + 1;
                if (*ver != '\0' && isdigit((unsigned char)*ver)) {
                    exact_match_count++;
                    if (exact_match_count == 1) {
                        runepkg_secure_strcpy(exact_match_name, sizeof(exact_match_name), package_name);
                        runepkg_secure_strcpy(exact_match_version, sizeof(exact_match_version), ver);
                    }
                }
            }
        }
    }
    closedir(dir);

    if (exact_match_count == 1) {
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
            if (exact_match_version[0] != '\0') {
                printf("Failed to read package info for %s %s.\n", exact_match_name, exact_match_version);
            } else {
                printf("Failed to read package info for %s.\n", exact_match_name);
            }
            runepkg_pack_free_package_info(&pkg_info);
            return -1;
        }
    } else {
        if (exact_match_count == 0) {
            char suggestions[12][PATH_MAX];
            int count;
            printf("'%s' not installed... did you mean?\n\n", package_name);
            count = runepkg_completion_get_repo_suggestions(package_name, suggestions, 12);
            if (count > 0) {
                const char *items[12];
                int i;
                for (i = 0; i < count; i++) items[i] = suggestions[i];
                runepkg_util_print_columns(items, count, "    ");
            }
        } else {
            char suggestions[100][PATH_MAX];
            int match_num;
            printf("'%s' not found... did you mean?\n\n", package_name);
            match_num = runepkg_util_get_package_suggestions(package_name, g_runepkg_db_dir, suggestions, 100);
            if (match_num > 0) {
                const char *items[100];
                int i;
                for (i = 0; i < match_num; i++) {
                    items[i] = suggestions[i];
                }
                runepkg_util_print_columns(items, match_num, "    ");
            }
        }
        return -2;
    }
}

void handle_search(const char *file_pattern) {
    DIR *dir;
    struct dirent *entry;
    int found_matches = 0;

    if (!file_pattern || !g_runepkg_db_dir) {
        printf("Error: Invalid file pattern or config.\n");
        return;
    }

    print_package_data_header();

    dir = opendir(g_runepkg_db_dir);
    if (!dir) {
        printf("Error: Cannot open runepkg database directory: %s\n", g_runepkg_db_dir);
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char *pkg_name = NULL;
        char *pkg_version = NULL;
        const char *ver_dash = NULL;

        if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) continue;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "lists") == 0) continue;

        ver_dash = runepkg_util_find_version_separator(entry->d_name);

        if (ver_dash) {
            size_t name_len = (size_t)(ver_dash - entry->d_name);
            pkg_name = malloc(name_len + 1);
            if (pkg_name) {
                runepkg_util_safe_strncpy(pkg_name, entry->d_name, name_len + 1);
                pkg_name[name_len] = '\0';
            }
            pkg_version = strdup(ver_dash + 1);
        } else {
            pkg_name = strdup(entry->d_name);
            pkg_version = strdup("");
        }

        {
            PkgInfo pkg_info;
            runepkg_pack_init_package_info(&pkg_info);

            if (runepkg_storage_read_package_info(pkg_name, pkg_version, &pkg_info) == 0) {
                int i;
                for (i = 0; i < pkg_info.file_count; i++) {
                    if (strstr(pkg_info.file_list[i], file_pattern) != NULL) {
                        printf("%s: /%s\n", pkg_name, pkg_info.file_list[i]);
                        found_matches = 1;
                    }
                }
            }

            runepkg_pack_free_package_info(&pkg_info);
        }
        free(pkg_name);
        free(pkg_version);
    }

    closedir(dir);

    if (!found_matches) {
        printf("No packages found containing files matching '%s'\n", file_pattern);
    }
}

void handle_list_files(const char *package_name) {
    DIR *dir;
    struct dirent *entry;
    int match_count = 0;
    char match_name[PATH_MAX];
    char match_version[PATH_MAX];
    size_t input_len;

    if (!package_name || !g_runepkg_db_dir) {
        printf("Error: Invalid package name or config.\n");
        return;
    }

    print_package_data_header();

    dir = opendir(g_runepkg_db_dir);
    if (!dir) {
        printf("Error: Cannot open runepkg database directory: %s\n", g_runepkg_db_dir);
        return;
    }

    memset(match_name, 0, sizeof(match_name));
    memset(match_version, 0, sizeof(match_version));

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) continue;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "lists") == 0) continue;

        if (strcmp(entry->d_name, package_name) == 0) {
            const char *last_dash = NULL;
            last_dash = runepkg_util_find_version_separator(entry->d_name);
            if (last_dash) {
                size_t name_len = (size_t)(last_dash - entry->d_name);
                runepkg_util_safe_strncpy(match_name, entry->d_name, name_len + 1);
                match_name[name_len] = '\0';
                runepkg_secure_strcpy(match_version, sizeof(match_version), last_dash + 1);
            } else {
                runepkg_secure_strcpy(match_name, sizeof(match_name), entry->d_name);
                match_version[0] = '\0';
            }
            match_count = 1;
            break;
        }

        input_len = strlen(package_name);
        if (strncmp(entry->d_name, package_name, input_len) == 0 && entry->d_name[input_len] == '-') {
            const char *ver = entry->d_name + input_len + 1;
            if (*ver != '\0') {
                match_count++;
                if (match_count == 1) {
                    runepkg_secure_strcpy(match_name, sizeof(match_name), package_name);
                    runepkg_secure_strcpy(match_version, sizeof(match_version), ver);
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
                int i;
                for (i = 0; i < pkg_info.file_count; i++) {
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

    if (match_count == 0) {
        char suggestions[12][PATH_MAX];
        int count;
        printf("'%s' not installed... did you mean?\n\n", package_name);
        count = runepkg_completion_get_repo_suggestions(package_name, suggestions, 12);
        if (count > 0) {
            const char *items[12];
            int i;
            for (i = 0; i < count; i++) items[i] = suggestions[i];
            runepkg_util_print_columns(items, count, "    ");
        }
    } else {
        char suggestions[100][PATH_MAX];
        int suggestion_count;
        printf("'%s' not found... did you mean?\n\n", package_name);
        suggestion_count = runepkg_util_get_package_suggestions(package_name, g_runepkg_db_dir, suggestions, 100);
        if (suggestion_count > 0) {
            const char *items[100];
            int i;
            for (i = 0; i < suggestion_count; i++) items[i] = suggestions[i];
            runepkg_util_print_columns(items, suggestion_count, "    ");
        }
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
    {
        extern bool g_md5_checks;
        printf("  MD5 Checks: %s\n", g_md5_checks ? "yes" : "no");
    }

    if (g_sources_count > 0 && g_sources) {
        int i;
        printf("\nConfigured Sources:\n");
        printf("==================\n");
        for (i = 0; i < g_sources_count; i++) {
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
    char **packages = NULL;
    int count = 0;

    if (!txt_file) {
        fprintf(stderr, "Error: Cannot open runepkg_autocomplete.txt for writing: %s\n", g_pkglist_txt_path);
        return;
    }

    if (g_runepkg_db_dir) {
        DIR *dir = opendir(g_runepkg_db_dir);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) continue;
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "lists") == 0) continue;

                packages = realloc(packages, (count + 1) * sizeof(char *));
                if (packages) {
                    packages[count++] = strdup(entry->d_name);
                }
            }
            closedir(dir);
        }
    }

    if (g_build_dir) {
        DIR *dir = opendir(g_build_dir);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) continue;
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

                packages = realloc(packages, (count + 1) * sizeof(char *));
                if (packages) {
                    packages[count++] = strdup(entry->d_name);
                }
            }
            closedir(dir);
        }
    }

    if (count > 0) {
        int i;
        int unique_count = 0;

        qsort(packages, count, sizeof(char *), compare_pkgs);

        for (i = 0; i < count; i++) {
            if (i == 0 || strcmp(packages[i], packages[i-1]) != 0) {
                packages[unique_count++] = packages[i];
            } else {
                free(packages[i]);
            }
        }
        count = unique_count;

        for (i = 0; i < count; i++) {
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

    fflush(txt_file);
    fsync(fileno(txt_file));
    fclose(txt_file);

    if (runepkg_storage_build_autocomplete_index() != 0) {
        runepkg_log_verbose("Warning: Failed to build binary autocomplete index.\n");
    } else {
        runepkg_log_verbose("Autocomplete list updated and binary index built.\n");
    }
}

int handle_unpack(const char *deb_path, const char *dest_dir) {
    PkgInfo pkg_info;
    char *target_dir = NULL;

    if (!deb_path) return -1;

    if (!runepkg_util_file_exists(deb_path)) {
        fprintf(stderr, "\033[1;31mError:\033[0m File '%s' not found.\n", deb_path);
        return -1;
    }

    if (!g_control_dir) {
        runepkg_util_error("control_dir is not configured in runepkgconfig.\n");
        return -1;
    }

    runepkg_log_verbose("Unpacking %s into control_dir (%s)...\n", deb_path, g_control_dir);

    runepkg_pack_init_package_info(&pkg_info);

    if (runepkg_pack_extract_and_collect_info(deb_path, g_control_dir, &pkg_info) != 0) {
        runepkg_util_error("Failed to extract .deb archive into control_dir for %s\n", deb_path);
        runepkg_pack_free_package_info(&pkg_info);
        return -1;
    }

    if (dest_dir && dest_dir[0] != '\0') {
        target_dir = strdup(dest_dir);
    } else {
        char *pkg_ver_name = NULL;
        if (pkg_info.package_name && pkg_info.version) {
            size_t pkg_ver_len = strlen(pkg_info.package_name) + strlen(pkg_info.version) + 2;
            pkg_ver_name = (char *)malloc(pkg_ver_len);
            if (pkg_ver_name) {
                snprintf(pkg_ver_name, pkg_ver_len, "%s-%s", pkg_info.package_name, pkg_info.version);
            }
        }
        if (!pkg_ver_name) {
            pkg_ver_name = strdup(pkg_info.package_name ? pkg_info.package_name : "unpacked");
        }

        if (g_build_dir) {
            target_dir = runepkg_util_concat_path(g_build_dir, pkg_ver_name);
        } else {
            target_dir = strdup(pkg_ver_name);
        }
        free(pkg_ver_name);
    }

    if (!target_dir) {
        runepkg_pack_cleanup_extraction_workspace(&pkg_info);
        runepkg_pack_free_package_info(&pkg_info);
        return -1;
    }

    if (runepkg_util_create_dir_recursive(target_dir, 0755) != 0) {
        runepkg_util_error("Failed to create target directory %s\n", target_dir);
        runepkg_pack_cleanup_extraction_workspace(&pkg_info);
        runepkg_pack_free_package_info(&pkg_info);
        free(target_dir);
        return -1;
    }

    if (pkg_info.data_dir_path && runepkg_util_file_exists(pkg_info.data_dir_path)) {
        if (runepkg_util_copy_dir_recursive(pkg_info.data_dir_path, target_dir) != 0) {
            runepkg_util_error("Failed to copy data payload to %s\n", target_dir);
            runepkg_pack_cleanup_extraction_workspace(&pkg_info);
            runepkg_pack_free_package_info(&pkg_info);
            free(target_dir);
            return -1;
        }
    }

    printf("\033[1;32m[unpack]\033[0m Successfully unpacked %s data content to %s\n", deb_path, target_dir);

    if (g_cleanup_extract_dirs) {
        runepkg_log_verbose("[unpack] Automatic cleanup enabled: removing workspace in control_dir...\n");
        runepkg_pack_cleanup_extraction_workspace(&pkg_info);
    } else {
        printf("\033[1;34m[unpack]\033[0m Control workspace retained in %s\n",
               pkg_info.extraction_workspace_path ? pkg_info.extraction_workspace_path : g_control_dir);
    }

    runepkg_pack_free_package_info(&pkg_info);
    free(target_dir);
    return 0;
}

int handle_build(const char *source_dir, const char *output_name) {
    const char *src = source_dir;
    char *out_allocated = NULL;
    const char *out = output_name;

    if (!src) src = g_build_dir;
    if (!src) {
        runepkg_util_error("No source directory specified and build_dir not configured.\n");
        return -1;
    }

    if (!out) {
        char *control_path = runepkg_util_concat_path(src, "control/control");
        if (control_path && runepkg_util_file_exists(control_path)) {
            char *pkg = runepkg_util_get_config_value(control_path, "Package", ':');
            char *ver = runepkg_util_get_config_value(control_path, "Version", ':');
            char *arch = runepkg_util_get_config_value(control_path, "Architecture", ':');

            if (pkg && ver && arch) {
                size_t len = strlen(pkg) + strlen(ver) + strlen(arch) + 7;
                char *filename = malloc(len);
                if (filename) {
                    snprintf(filename, len, "%s_%s_%s.deb", pkg, ver, arch);

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

        if (!out) {
            char *bname = basename((char*)src);
            char *filename = runepkg_secure_sprintf(strlen(bname) + 5, "%s.deb", bname);
            if (filename) {
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

    {
        int ret = runepkg_util_create_deb(src, out);
        if (ret == 0) {
            printf("\033[1;32m[build]\033[0m Successfully built package: %s\n", out);
        }

        free(out_allocated);
        return ret;
    }
}

/* -------------------------------------------------------------------------- */
/* 70k+ Repository Graph Resolver & Harvester Handlers                         */
/* -------------------------------------------------------------------------- */
int handle_resolve_tree(const char *pkg_name) {
#ifdef ENABLE_CPP_FFI
    if (!pkg_name) {
        printf("Error: resolve-tree requires a target package name.\n");
        return -1;
    }
    return runepkg_resolver_dump_tree(pkg_name);
#else
    (void)pkg_name;
    printf("Notice: Target dependency resolver requires a C++ build with FFI enabled.\n");
    printf("Rebuild with 'make all' to enable this feature.\n");
    return -1;
#endif
}

int handle_md5_check(const char *package_name) {
    PkgInfo pkg_info;
    DIR *dir;
    struct dirent *entry;
    char found_pkg[PATH_MAX];
    char *v;
    char *version;
    char pkg_db_path[PATH_MAX];
    char *md5sums_path;
    FILE *f;

    if (!package_name) return -1;

    print_package_data_header();

    runepkg_pack_init_package_info(&pkg_info);

    dir = opendir(g_runepkg_db_dir);
    if (!dir) return -1;

    memset(found_pkg, 0, sizeof(found_pkg));
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) continue;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "lists") == 0) continue;

        if (strcmp(entry->d_name, package_name) == 0) {
            runepkg_secure_strcpy(found_pkg, sizeof(found_pkg), entry->d_name);
            break;
        }
        {
            size_t len = strlen(package_name);
            if (strncmp(entry->d_name, package_name, len) == 0 && entry->d_name[len] == '-') {
                runepkg_secure_strcpy(found_pkg, sizeof(found_pkg), entry->d_name);
                break;
            }
        }
    }
    closedir(dir);

    if (!found_pkg[0]) {
        char suggestions[100][PATH_MAX];
        int suggestion_count;
        printf("'%s' not installed... did you mean?\n\n", package_name);
        suggestion_count = runepkg_util_get_package_suggestions(package_name, g_runepkg_db_dir, suggestions, 100);
        if (suggestion_count > 0) {
            const char *items[100];
            int i;
            for (i = 0; i < suggestion_count; i++) items[i] = suggestions[i];
            runepkg_util_print_columns(items, suggestion_count, "    ");
        }
        return -1;
    }

    v = strrchr(found_pkg, '-');
    if (!v) return -1;
    *v = '\0';
    version = v + 1;

    if (runepkg_storage_read_package_info(found_pkg, version, &pkg_info) != 0) {
        runepkg_util_error("Failed to read metadata for %s-%s\n", found_pkg, version);
        return -1;
    }

    if (!g_system_install_root) return -1;

    runepkg_storage_get_package_path(found_pkg, version, pkg_db_path);
    md5sums_path = runepkg_util_concat_path(pkg_db_path, "md5sums");

    if (!runepkg_util_file_exists(md5sums_path)) {
        printf("No md5sums file for %s, cannot verify.\n", package_name);
        free(md5sums_path);
        runepkg_pack_free_package_info(&pkg_info);
        return -1;
    }

    f = fopen(md5sums_path, "r");
    if (!f) {
        free(md5sums_path);
        return -1;
    }

    printf("\033[1;34m[integrity]\033[0m Verifying %s integrity...\n", found_pkg);
    {
        char line[PATH_MAX + 64];
        int total = 0, ok = 0, fail = 0;
        while (fgets(line, sizeof(line), f)) {
            char exp[33], rel[PATH_MAX];
            if (sscanf(line, "%32s  %s", exp, rel) != 2) continue;
            total++;

            {
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
    }

    runepkg_pack_free_package_info(&pkg_info);
    return 0;
}

void handle_version(void) {
    printf("  \033[1;30m[#####]\033[0m  runepkg\n");
    printf("  \033[1;30m[#\033[1;36m\\ /\033[1;30m#]\033[0m  version 1.0.4\n");
    printf("  \033[1;30m[# \033[1;36mV \033[1;30m#]\033[0m  \n");
    printf("  \033[1;30m[#####]\033[0m  GPL-V3\n\n");
    printf("*Built with ❤️ for the old school GNU/Linux community...*\n");
    printf("Copyright (c) 2026 runepkg (Runar Linux) All rights reserved.\n");
    printf("Contact: [michkochris@gmail.com] | [runepkg@gmail.com]\n");
}

void handle_verify_package(const char *package_name) {
    if (!package_name) return;

    if (!runepkg_crypto_is_enabled()) {
        printf("\033[1;33m[security]\033[0m Cryptographic verification is currently disabled in config.\n");
        printf("Set 'gpg_check=yes' in runepkgconfig to enable.\n");
        return;
    }

    printf("\033[1;34m[security]\033[0m Initiating cryptographic audit for %s...\n", package_name);
    printf("Status: GPG Verification is active.\n");
    printf("Please provide a signed source or repository index for verification.\n");
}
