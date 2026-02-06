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
int calculate_optimal_threads(void) {
    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cores <= 0) {
        num_cores = 1; // Fallback
    }
    
    // Get 1-minute load average
    double loadavg[3];
    if (getloadavg(loadavg, 3) == -1) {
        loadavg[0] = 0.0; // Fallback if can't get load
    }
    
    // Base threads on cores
    int base_threads = (int)num_cores;
    
    // Adjust based on load: if load > cores, reduce threads
    double load_factor = loadavg[0] / (double)num_cores;
    if (load_factor > 1.0) {
        // High load: reduce threads
        base_threads = (int)(base_threads / (load_factor * 0.5 + 0.5));
        if (base_threads < 1) base_threads = 1;
    } else {
        // Low load: can use more threads for I/O
        base_threads = (int)(base_threads * 1.5);
    }
    
    // Cap at reasonable limits
    if (base_threads > 16) {
        base_threads = 16;
    }
    if (base_threads < 1) {
        base_threads = 1;
    }
    
    runepkg_log_verbose("Calculated optimal threads: %d (cores: %ld, load: %.2f)\n", 
                       base_threads, num_cores, loadavg[0]);
    return base_threads;
}

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

void handle_install_stdin(void) {
    char line[PATH_MAX * 2];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *trimmed = runepkg_util_trim_whitespace(line);
        if (!trimmed || trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }

        char *token = strtok(trimmed, " \t");
        while (token) {
            // Accept any non-empty token as a potential .deb file path
            if (token[0] != '\0') {
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

// Struct for thread arguments in file installation
typedef struct {
    char *src;
    char *dst;
    int *error_count;
    pthread_mutex_t *mutex;
} FileInstallArgs;

// Function to install a single file (for threading)
void* install_single_file(void *arg) {
    FileInstallArgs *args = (FileInstallArgs*)arg;
    char *src = (char*)args->src;  // Cast away const for freeing
    char *dst = (char*)args->dst;
    int *error_count = args->error_count;
    pthread_mutex_t *mutex = args->mutex;

    struct stat st;
    if (lstat(src, &st) != 0) {
        runepkg_log_verbose("Install: failed to stat %s: %s\n", src, strerror(errno));
        pthread_mutex_lock(mutex);
        (*error_count)++;
        pthread_mutex_unlock(mutex);
        free(src);
        free(dst);
        return NULL;
    }

    if (S_ISDIR(st.st_mode)) {
        if (runepkg_util_create_dir_recursive(dst, 0755) != 0) {
            pthread_mutex_lock(mutex);
            (*error_count)++;
            pthread_mutex_unlock(mutex);
        }
    } else if (S_ISREG(st.st_mode)) {
        char *dst_copy = strdup(dst);
        if (dst_copy) {
            char *parent = dirname(dst_copy);
            if (parent) runepkg_util_create_dir_recursive(parent, 0755);
            free(dst_copy);
        }
        if (runepkg_util_copy_file(src, dst) != 0) {
            pthread_mutex_lock(mutex);
            (*error_count)++;
            pthread_mutex_unlock(mutex);
        }
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
            if (symlink(link_target, dst) != 0) {
                pthread_mutex_lock(mutex);
                (*error_count)++;
                pthread_mutex_unlock(mutex);
            }
        } else {
            runepkg_log_verbose("Install: failed to read symlink %s\n", src);
            pthread_mutex_lock(mutex);
            (*error_count)++;
            pthread_mutex_unlock(mutex);
        }
    }

    free(src);
    free(dst);
    return NULL;
}

int handle_install(const char *deb_file_path) {
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    runepkg_log_verbose("Installing package from: %s\n", deb_file_path);

    // Handle patterns
    if (strstr(deb_file_path, ".deb") == NULL) {
        char pattern[PATH_MAX];
        if (strstr(deb_file_path, "*") != NULL) {
            strncpy(pattern, deb_file_path, sizeof(pattern) - 1);
        } else {
            snprintf(pattern, sizeof(pattern), "%s*.deb", deb_file_path);
        }
        glob_t globbuf;
        int found = 0;
        if (glob(pattern, 0, NULL, &globbuf) == 0 && globbuf.gl_pathc > 0) {
            found = 1;
        } else if (strstr(deb_file_path, "/") == NULL) {
            // Try with debs/ prefix
            char new_pattern[PATH_MAX];
            if (strstr(deb_file_path, "*") != NULL) {
                snprintf(new_pattern, sizeof(new_pattern), "debs/%s", deb_file_path);
            } else {
                snprintf(new_pattern, sizeof(new_pattern), "debs/%s*.deb", deb_file_path);
            }
            globfree(&globbuf);
            if (glob(new_pattern, 0, NULL, &globbuf) == 0 && globbuf.gl_pathc > 0) {
                found = 1;
            }
        }
        if (found) {
            // Sort glob results by version and pick the latest
            if (globbuf.gl_pathc > 1) {
                // Sort by version descending
                for (size_t i = 0; i < globbuf.gl_pathc - 1; i++) {
                    for (size_t j = i + 1; j < globbuf.gl_pathc; j++) {
                        // Extract versions from filenames
                        char *name1 = basename(strdup(globbuf.gl_pathv[i]));
                        char *name2 = basename(strdup(globbuf.gl_pathv[j]));
                        char *ver1 = strstr(name1, "-");
                        char *ver2 = strstr(name2, "-");
                        if (ver1) ver1++;
                        if (ver2) ver2++;
                        char *end1 = strstr(ver1 ? ver1 : "", ".deb");
                        char *end2 = strstr(ver2 ? ver2 : "", ".deb");
                        if (end1) *end1 = '\0';
                        if (end2) *end2 = '\0';
                        if (runepkg_util_compare_versions(ver1 ? ver1 : "", ver2 ? ver2 : "") < 0) {
                            // Swap to sort descending
                            char *temp = globbuf.gl_pathv[i];
                            globbuf.gl_pathv[i] = globbuf.gl_pathv[j];
                            globbuf.gl_pathv[j] = temp;
                        }
                        free(name1);
                        free(name2);
                    }
                }
            }
            // Install the latest (first after sorting)
            handle_install(globbuf.gl_pathv[0]);
            globfree(&globbuf);
            return 0;
        }
        globfree(&globbuf);
        return -1;
    }

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
        return -1;
    }

    PkgInfo pkg_info;
    runepkg_pack_init_package_info(&pkg_info);

    int result = runepkg_pack_extract_and_collect_info(deb_file_path, g_control_dir, &pkg_info);

    if (result == 0) {
        // Check if already installed
        if (runepkg_hash_search(runepkg_main_hash_table, pkg_info.package_name)) {
            runepkg_log_verbose("Package %s already installed, skipping\n", pkg_info.package_name);
            runepkg_pack_free_package_info(&pkg_info);
            return 0;
        }

        // Mark as installing to prevent recursive installs
        PkgInfo dummy;
        runepkg_pack_init_package_info(&dummy);
        dummy.package_name = strdup(pkg_info.package_name);
        if (pkg_info.version) dummy.version = strdup(pkg_info.version);
        runepkg_hash_add_package(runepkg_main_hash_table, &dummy);
        runepkg_pack_free_package_info(&dummy);

        // Resolve dependencies
        Dependency **deps = parse_depends_with_constraints(pkg_info.depends);
        if (deps) {
            Dependency **unsatisfied = NULL;
            int num_unsatisfied = 0;
            for (int j = 0; deps[j]; j++) {
                PkgInfo *installed = runepkg_hash_search(runepkg_main_hash_table, deps[j]->package);
                int satisfied = 0;
                if (installed) {
                    if (deps[j]->constraint) {
                        satisfied = runepkg_util_check_version_constraint(installed->version, deps[j]->constraint);
                        if (satisfied == -1) {
                            printf("Warning: Unknown constraint '%s' for %s\n", deps[j]->constraint, deps[j]->package);
                            satisfied = 1;  // Assume satisfied for unknown
                        } else if (satisfied) {
                            runepkg_log_verbose("Dependency '%s %s' satisfied by installed version %s\n",
                                               deps[j]->package, deps[j]->constraint, installed->version);
                        }
                    } else {
                        satisfied = 1;  // No constraint, just installed
                        runepkg_log_verbose("Dependency '%s' satisfied by installed package\n", deps[j]->package);
                    }
                }
                if (!satisfied) {
                    // Find deb file in same dir
                    char *deb_copy = strdup(deb_file_path);
                    if (deb_copy) {
                        char *dir = dirname(deb_copy);
                        char pattern[PATH_MAX];
                        snprintf(pattern, sizeof(pattern), "%s/%s*.deb", dir, deps[j]->package);
                        glob_t globbuf;
                        if (glob(pattern, 0, NULL, &globbuf) == 0 && globbuf.gl_pathc > 0) {
                            runepkg_log_verbose("Installing dependency: %s from %s\n", deps[j]->package, globbuf.gl_pathv[0]);
                            handle_install(globbuf.gl_pathv[0]);
                        } else {
                            if (!g_force_mode) {
                                unsatisfied = realloc(unsatisfied, (num_unsatisfied + 1) * sizeof(Dependency*));
                                unsatisfied[num_unsatisfied] = malloc(sizeof(Dependency));
                                unsatisfied[num_unsatisfied]->package = strdup(deps[j]->package);
                                unsatisfied[num_unsatisfied]->constraint = deps[j]->constraint ? strdup(deps[j]->constraint) : NULL;
                                num_unsatisfied++;
                            } else {
                                runepkg_log_verbose("Skipping unsatisfied dependency %s %s (force mode)\n",
                                                    deps[j]->package, deps[j]->constraint ? deps[j]->constraint : "");
                            }
                        }
                        globfree(&globbuf);
                        free(deb_copy);
                    }
                }
            }
            if (num_unsatisfied > 0) {
                printf("Error: The following dependencies are not satisfied:\n");
                for(int k=0; k<num_unsatisfied; k++){
                    printf("  - %s%s%s\n", unsatisfied[k]->package, unsatisfied[k]->constraint ? " " : "", unsatisfied[k]->constraint ? unsatisfied[k]->constraint : "");
                }
                printf("Use -f or --force to install anyway.\n");
                // free unsatisfied
                for(int k=0; k<num_unsatisfied; k++){
                    free(unsatisfied[k]->package);
                    if(unsatisfied[k]->constraint) free(unsatisfied[k]->constraint);
                    free(unsatisfied[k]);
                }
                free(unsatisfied);
                // Free deps
                for (int j = 0; deps[j]; j++) {
                    free(deps[j]->package);
                    free(deps[j]->constraint);
                    free(deps[j]);
                }
                free(deps);
                runepkg_pack_free_package_info(&pkg_info);
                return -1;
            } else {
                // Free deps
                for (int j = 0; deps[j]; j++) {
                    free(deps[j]->package);
                    free(deps[j]->constraint);
                    free(deps[j]);
                }
                free(deps);
            }
        }

        if (g_verbose_mode) {
            runepkg_pack_print_package_info(&pkg_info);
        } else {
            printf("Selecting previously unselected package %s.\n",
                   pkg_info.package_name ? pkg_info.package_name : "(unknown)");
            printf("Unpacking %s (%s) ...\n",
                   pkg_info.package_name ? pkg_info.package_name : "(unknown)",
                   pkg_info.version ? pkg_info.version : "(unknown)");
        }

        if (pkg_info.package_name && pkg_info.version) {
            if (runepkg_storage_create_package_directory(pkg_info.package_name, pkg_info.version) == 0) {
                if (runepkg_storage_write_package_info(pkg_info.package_name, pkg_info.version, &pkg_info) == 0) {
                    if (g_verbose_mode) {
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

        // Rebuild autocomplete index after install
        runepkg_storage_build_autocomplete_index();

        // Update the text autocomplete list
        handle_update_pkglist();

        if (g_system_install_root && pkg_info.data_dir_path && pkg_info.file_count > 0 && pkg_info.file_list) {
            int install_errors = 0;
            pthread_mutex_t error_mutex = PTHREAD_MUTEX_INITIALIZER;
            const int MAX_POSSIBLE_THREADS = 32; // Absolute maximum
            int max_threads = calculate_optimal_threads();
            if (max_threads > MAX_POSSIBLE_THREADS) {
                max_threads = MAX_POSSIBLE_THREADS;
            }
            pthread_t threads[MAX_POSSIBLE_THREADS];
            memset(threads, 0, sizeof(threads));
            FileInstallArgs thread_args[MAX_POSSIBLE_THREADS];
            int active_threads = 0;

            // Process files in batches
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

                // Wait for a thread slot if needed
                if (active_threads >= max_threads) {
                    for (int t = 0; t < max_threads; t++) {
                        if (threads[t] != 0) {
                            pthread_join(threads[t], NULL);
                            threads[t] = 0;
                            active_threads--;
                            break;
                        }
                    }
                }

                // Find an available thread slot
                int thread_idx = -1;
                for (int t = 0; t < max_threads; t++) {
                    if (threads[t] == 0) {
                        thread_idx = t;
                        break;
                    }
                }

                if (thread_idx == -1) {
                    // Should not happen, but fallback to sequential
                    struct stat st;
                    if (lstat(src, &st) != 0) {
                        runepkg_log_verbose("Install: failed to stat %s: %s\n", src, strerror(errno));
                        install_errors++;
                    } else if (S_ISDIR(st.st_mode)) {
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
                    continue;
                }

                // Prepare thread arguments
                thread_args[thread_idx].src = src;
                thread_args[thread_idx].dst = dst;
                thread_args[thread_idx].error_count = &install_errors;
                thread_args[thread_idx].mutex = &error_mutex;

                // Create thread
                if (pthread_create(&threads[thread_idx], NULL, install_single_file, &thread_args[thread_idx]) == 0) {
                    active_threads++;
                } else {
                    // Fallback to sequential on thread creation failure
                    struct stat st;
                    if (lstat(src, &st) != 0) {
                        runepkg_log_verbose("Install: failed to stat %s: %s\n", src, strerror(errno));
                        install_errors++;
                    } else if (S_ISDIR(st.st_mode)) {
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
            }

            // Wait for remaining threads
            for (int t = 0; t < max_threads; t++) {
                if (threads[t] != 0) {
                    pthread_join(threads[t], NULL);
                }
            }

            pthread_mutex_destroy(&error_mutex);

            if (install_errors == 0 && g_verbose_mode) printf("Files installed to: %s\n", g_system_install_root);
            else if (install_errors > 0) printf("Install completed with %d file errors.\n", install_errors);
        }

    } else {
        runepkg_pack_free_package_info(&pkg_info);
        return -1;
    }

    if (pkg_info.package_name && installing_packages) {
        runepkg_hash_remove_package(installing_packages, pkg_info.package_name);
    }

    runepkg_pack_free_package_info(&pkg_info);

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double install_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
    runepkg_log_verbose("Total installation time: %.6f seconds\n", install_time);

    return 0;
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

