/******************************************************************************
 * Filename:    runepkg_storage.c
 * Author:      <michkochris@gmail.com>
 * Date:        started 01-03-2025
 * Description: Persistent storage management for runepkg package database
 *
 * Copyright (c) 2025 runepkg (Runar Linux) All rights reserved.
 * GPLV3
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>

#include "runepkg_storage.h"
#include "runepkg_config.h"
#include "runepkg_util.h"

// External global variables
extern char *g_runepkg_db_dir;
extern bool g_verbose_mode;

// --- Internal Function Declarations ---
static void runepkg_log_verbose(const char *format, ...);
static int runepkg_storage_create_directory_recursive(const char *path);

// --- Internal Functions ---

/**
 * @brief Logging function for verbose output
 */
static void runepkg_log_verbose(const char *format, ...) {
    if (!g_verbose_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("[STORAGE VERBOSE] ");
    vprintf(format, args);
    va_end(args);
}

/**
 * @brief Creates directory recursively (like mkdir -p)
 */
static int runepkg_storage_create_directory_recursive(const char *path) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    
    return 0;
}

// --- Public Storage Functions ---

/**
 * @brief Gets the full path to a package directory
 */
int runepkg_storage_get_package_path(const char *pkg_name, const char *pkg_version, 
                                    char *path_buffer) {
    if (!pkg_name || !pkg_version || !path_buffer) {
        return -1;
    }

    if (!g_runepkg_db_dir) {
        printf("Error: runepkg database directory not configured.\n");
        return -1;
    }

    snprintf(path_buffer, PATH_MAX, "%s/%s-%s", g_runepkg_db_dir, pkg_name, pkg_version);
    return 0;
}

/**
 * @brief Creates a package directory in the persistent storage
 */
int runepkg_storage_create_package_directory(const char *pkg_name, const char *pkg_version) {
    if (!pkg_name || !pkg_version) {
        return -1;
    }

    char pkg_dir_path[PATH_MAX];
    if (runepkg_storage_get_package_path(pkg_name, pkg_version, pkg_dir_path) != 0) {
        return -1;
    }

    runepkg_log_verbose("Creating package directory: %s\n", pkg_dir_path);

    // Create the package directory
    if (runepkg_storage_create_directory_recursive(pkg_dir_path) != 0) {
        printf("Error: Failed to create package directory: %s\n", pkg_dir_path);
        return -1;
    }

    runepkg_log_verbose("Package directory created successfully: %s\n", pkg_dir_path);
    return 0;
}

/**
 * @brief Writes package info to persistent storage
 */
int runepkg_storage_write_package_info(const char *pkg_name, const char *pkg_version, 
                                      const upkg_package_info_t *pkg_info) {
    if (!pkg_name || !pkg_version || !pkg_info) {
        return -1;
    }

    char pkg_dir_path[PATH_MAX];
    char binary_file_path[PATH_MAX];
    char files_list_path[PATH_MAX];

    if (runepkg_storage_get_package_path(pkg_name, pkg_version, pkg_dir_path) != 0) {
        return -1;
    }

    // Check path lengths to prevent truncation
    size_t pkg_dir_len = strlen(pkg_dir_path);
    if (pkg_dir_len + strlen(RUNEPKG_STORAGE_BINARY_FILE) + 2 > sizeof(binary_file_path)) {
        printf("Error: Package directory path too long for binary file\n");
        return -1;
    }
    if (pkg_dir_len + strlen(RUNEPKG_STORAGE_FILES_LIST) + 2 > sizeof(files_list_path)) {
        printf("Error: Package directory path too long for files list\n");
        return -1;
    }

    int ret1 = snprintf(binary_file_path, sizeof(binary_file_path), "%s/%s", 
                        pkg_dir_path, RUNEPKG_STORAGE_BINARY_FILE);
    int ret2 = snprintf(files_list_path, sizeof(files_list_path), "%s/%s", 
                        pkg_dir_path, RUNEPKG_STORAGE_FILES_LIST);
    
    if (ret1 >= (int)sizeof(binary_file_path) || ret2 >= (int)sizeof(files_list_path)) {
        printf("Error: Path truncation occurred during formatting\n");
        return -1;
    }

    runepkg_log_verbose("Writing package info to: %s\n", binary_file_path);

    // Open binary file for writing
    FILE *bin_file = fopen(binary_file_path, "wb");
    if (!bin_file) {
        printf("Error: Failed to open binary file for writing: %s\n", binary_file_path);
        return -1;
    }

    // Write string lengths and strings to binary file
    size_t len;
    
    // Write package_name
    len = pkg_info->package_name ? strlen(pkg_info->package_name) + 1 : 0;
    fwrite(&len, sizeof(size_t), 1, bin_file);
    if (len > 0) fwrite(pkg_info->package_name, 1, len, bin_file);

    // Write version
    len = pkg_info->version ? strlen(pkg_info->version) + 1 : 0;
    fwrite(&len, sizeof(size_t), 1, bin_file);
    if (len > 0) fwrite(pkg_info->version, 1, len, bin_file);

    // Write architecture
    len = pkg_info->architecture ? strlen(pkg_info->architecture) + 1 : 0;
    fwrite(&len, sizeof(size_t), 1, bin_file);
    if (len > 0) fwrite(pkg_info->architecture, 1, len, bin_file);

    // Write maintainer
    len = pkg_info->maintainer ? strlen(pkg_info->maintainer) + 1 : 0;
    fwrite(&len, sizeof(size_t), 1, bin_file);
    if (len > 0) fwrite(pkg_info->maintainer, 1, len, bin_file);

    // Write description
    len = pkg_info->description ? strlen(pkg_info->description) + 1 : 0;
    fwrite(&len, sizeof(size_t), 1, bin_file);
    if (len > 0) fwrite(pkg_info->description, 1, len, bin_file);

    // Write depends
    len = pkg_info->depends ? strlen(pkg_info->depends) + 1 : 0;
    fwrite(&len, sizeof(size_t), 1, bin_file);
    if (len > 0) fwrite(pkg_info->depends, 1, len, bin_file);

    // Write installed_size
    len = pkg_info->installed_size ? strlen(pkg_info->installed_size) + 1 : 0;
    fwrite(&len, sizeof(size_t), 1, bin_file);
    if (len > 0) fwrite(pkg_info->installed_size, 1, len, bin_file);

    // Write section
    len = pkg_info->section ? strlen(pkg_info->section) + 1 : 0;
    fwrite(&len, sizeof(size_t), 1, bin_file);
    if (len > 0) fwrite(pkg_info->section, 1, len, bin_file);

    // Write priority
    len = pkg_info->priority ? strlen(pkg_info->priority) + 1 : 0;
    fwrite(&len, sizeof(size_t), 1, bin_file);
    if (len > 0) fwrite(pkg_info->priority, 1, len, bin_file);

    // Write homepage
    len = pkg_info->homepage ? strlen(pkg_info->homepage) + 1 : 0;
    fwrite(&len, sizeof(size_t), 1, bin_file);
    if (len > 0) fwrite(pkg_info->homepage, 1, len, bin_file);

    // Write filename
    len = pkg_info->filename ? strlen(pkg_info->filename) + 1 : 0;
    fwrite(&len, sizeof(size_t), 1, bin_file);
    if (len > 0) fwrite(pkg_info->filename, 1, len, bin_file);

    // Write file_count
    fwrite(&pkg_info->file_count, sizeof(int), 1, bin_file);

    fclose(bin_file);

    // Write file list to separate text file
    if (pkg_info->file_list && pkg_info->file_count > 0) {
        FILE *files_file = fopen(files_list_path, "w");
        if (!files_file) {
            printf("Warning: Failed to open files list for writing: %s\n", files_list_path);
            return 0; // Don't fail completely if binary write succeeded
        }

        for (int i = 0; i < pkg_info->file_count; i++) {
            if (pkg_info->file_list[i]) {
                fprintf(files_file, "%s\n", pkg_info->file_list[i]);
            }
        }

        fclose(files_file);
        runepkg_log_verbose("Files list written to: %s\n", files_list_path);
    }

    runepkg_log_verbose("Package info written successfully to persistent storage\n");
    return 0;
}

/**
 * @brief Reads package info from persistent storage
 */
int runepkg_storage_read_package_info(const char *pkg_name, const char *pkg_version,
                                     upkg_package_info_t *pkg_info) {
    if (!pkg_name || !pkg_version || !pkg_info) {
        return -1;
    }

    char pkg_dir_path[PATH_MAX];
    char binary_file_path[PATH_MAX];

    if (runepkg_storage_get_package_path(pkg_name, pkg_version, pkg_dir_path) != 0) {
        return -1;
    }

    // Check path length to prevent truncation
    size_t pkg_dir_len = strlen(pkg_dir_path);
    if (pkg_dir_len + strlen(RUNEPKG_STORAGE_BINARY_FILE) + 2 > sizeof(binary_file_path)) {
        printf("Error: Package directory path too long for binary file\n");
        return -1;
    }

    int ret = snprintf(binary_file_path, sizeof(binary_file_path), "%s/%s", 
                       pkg_dir_path, RUNEPKG_STORAGE_BINARY_FILE);
    
    if (ret >= (int)sizeof(binary_file_path)) {
        printf("Error: Path truncation occurred during formatting\n");
        return -1;
    }

    runepkg_log_verbose("Reading package info from: %s\n", binary_file_path);

    // Initialize the package info structure
    upkg_pack_init_package_info(pkg_info);

    FILE *bin_file = fopen(binary_file_path, "rb");
    if (!bin_file) {
        printf("Error: Failed to open binary file for reading: %s\n", binary_file_path);
        return -1;
    }

    // Read string lengths and strings from binary file
    size_t len;
    
    // Read package_name
    if (fread(&len, sizeof(size_t), 1, bin_file) != 1) goto read_error;
    if (len > 0) {
        pkg_info->package_name = malloc(len);
        if (!pkg_info->package_name || fread(pkg_info->package_name, 1, len, bin_file) != len) 
            goto read_error;
    }

    // Read version
    if (fread(&len, sizeof(size_t), 1, bin_file) != 1) goto read_error;
    if (len > 0) {
        pkg_info->version = malloc(len);
        if (!pkg_info->version || fread(pkg_info->version, 1, len, bin_file) != len) 
            goto read_error;
    }

    // Read architecture
    if (fread(&len, sizeof(size_t), 1, bin_file) != 1) goto read_error;
    if (len > 0) {
        pkg_info->architecture = malloc(len);
        if (!pkg_info->architecture || fread(pkg_info->architecture, 1, len, bin_file) != len) 
            goto read_error;
    }

    // Read maintainer
    if (fread(&len, sizeof(size_t), 1, bin_file) != 1) goto read_error;
    if (len > 0) {
        pkg_info->maintainer = malloc(len);
        if (!pkg_info->maintainer || fread(pkg_info->maintainer, 1, len, bin_file) != len) 
            goto read_error;
    }

    // Read description
    if (fread(&len, sizeof(size_t), 1, bin_file) != 1) goto read_error;
    if (len > 0) {
        pkg_info->description = malloc(len);
        if (!pkg_info->description || fread(pkg_info->description, 1, len, bin_file) != len) 
            goto read_error;
    }

    // Read depends
    if (fread(&len, sizeof(size_t), 1, bin_file) != 1) goto read_error;
    if (len > 0) {
        pkg_info->depends = malloc(len);
        if (!pkg_info->depends || fread(pkg_info->depends, 1, len, bin_file) != len) 
            goto read_error;
    }

    // Read installed_size
    if (fread(&len, sizeof(size_t), 1, bin_file) != 1) goto read_error;
    if (len > 0) {
        pkg_info->installed_size = malloc(len);
        if (!pkg_info->installed_size || fread(pkg_info->installed_size, 1, len, bin_file) != len) 
            goto read_error;
    }

    // Read section
    if (fread(&len, sizeof(size_t), 1, bin_file) != 1) goto read_error;
    if (len > 0) {
        pkg_info->section = malloc(len);
        if (!pkg_info->section || fread(pkg_info->section, 1, len, bin_file) != len) 
            goto read_error;
    }

    // Read priority
    if (fread(&len, sizeof(size_t), 1, bin_file) != 1) goto read_error;
    if (len > 0) {
        pkg_info->priority = malloc(len);
        if (!pkg_info->priority || fread(pkg_info->priority, 1, len, bin_file) != len) 
            goto read_error;
    }

    // Read homepage
    if (fread(&len, sizeof(size_t), 1, bin_file) != 1) goto read_error;
    if (len > 0) {
        pkg_info->homepage = malloc(len);
        if (!pkg_info->homepage || fread(pkg_info->homepage, 1, len, bin_file) != len) 
            goto read_error;
    }

    // Read filename
    if (fread(&len, sizeof(size_t), 1, bin_file) != 1) goto read_error;
    if (len > 0) {
        pkg_info->filename = malloc(len);
        if (!pkg_info->filename || fread(pkg_info->filename, 1, len, bin_file) != len) 
            goto read_error;
    }

    // Read file_count
    if (fread(&pkg_info->file_count, sizeof(int), 1, bin_file) != 1) goto read_error;

    fclose(bin_file);

    // TODO: Read file list from separate text file if needed

    runepkg_log_verbose("Package info read successfully from persistent storage\n");
    return 0;

read_error:
    fclose(bin_file);
    upkg_pack_free_package_info(pkg_info);
    printf("Error: Failed to read package info from binary file\n");
    return -1;
}

/**
 * @brief Checks if a package exists in persistent storage
 */
int runepkg_storage_package_exists(const char *pkg_name, const char *pkg_version) {
    if (!pkg_name || !pkg_version) {
        return -1;
    }

    char pkg_dir_path[PATH_MAX];
    char binary_file_path[PATH_MAX];

    if (runepkg_storage_get_package_path(pkg_name, pkg_version, pkg_dir_path) != 0) {
        return -1;
    }

    // Check path length to prevent truncation
    size_t pkg_dir_len = strlen(pkg_dir_path);
    if (pkg_dir_len + strlen(RUNEPKG_STORAGE_BINARY_FILE) + 2 > sizeof(binary_file_path)) {
        printf("Error: Package directory path too long for binary file\n");
        return -1;
    }

    int ret = snprintf(binary_file_path, sizeof(binary_file_path), "%s/%s", 
                       pkg_dir_path, RUNEPKG_STORAGE_BINARY_FILE);
    
    if (ret >= (int)sizeof(binary_file_path)) {
        printf("Error: Path truncation occurred during formatting\n");
        return -1;
    }

    return upkg_util_file_exists(binary_file_path) ? 1 : 0;
}

/**
 * @brief Prints package info from persistent storage
 */
int runepkg_storage_print_package_info(const char *pkg_name, const char *pkg_version) {
    upkg_package_info_t pkg_info;
    
    if (runepkg_storage_read_package_info(pkg_name, pkg_version, &pkg_info) != 0) {
        return -1;
    }

    printf("\n=== Package Info from Persistent Storage ===\n");
    upkg_pack_print_package_info(&pkg_info);
    
    upkg_pack_free_package_info(&pkg_info);
    return 0;
}

/**
 * @brief Removes a package from persistent storage
 */
int runepkg_storage_remove_package(const char *pkg_name, const char *pkg_version) {
    if (!pkg_name || !pkg_version) {
        return -1;
    }

    char pkg_dir_path[PATH_MAX];
    if (runepkg_storage_get_package_path(pkg_name, pkg_version, pkg_dir_path) != 0) {
        return -1;
    }

    runepkg_log_verbose("Removing package directory: %s\n", pkg_dir_path);

    // TODO: Implement recursive directory removal
    // For now, just print what would be removed
    printf("Would remove package directory: %s\n", pkg_dir_path);
    
    return 0;
}

/**
 * @brief Lists all packages in persistent storage
 */
int runepkg_storage_list_packages(void) {
    if (!g_runepkg_db_dir) {
        printf("Error: runepkg database directory not configured.\n");
        return -1;
    }

    runepkg_log_verbose("Listing packages from: %s\n", g_runepkg_db_dir);

    DIR *dir = opendir(g_runepkg_db_dir);
    if (!dir) {
        printf("Error: Cannot open runepkg database directory: %s\n", g_runepkg_db_dir);
        return -1;
    }

    struct dirent *entry;
    printf("\n=== Installed Packages (Persistent Storage) ===\n");
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            printf("  %s\n", entry->d_name);
        }
    }

    closedir(dir);
    return 0;
}
