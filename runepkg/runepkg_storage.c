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
#include "runepkg_pack.h" // Needed for PkgInfo and pack functions

// External global variables
extern char *g_runepkg_db_dir;
extern bool g_verbose_mode;

// --- Internal Function Declarations ---

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

    // Use the unified utility function to create the directory
    if (runepkg_util_create_dir_recursive(pkg_dir_path, 0755) != 0) {
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
                                      const PkgInfo *pkg_info) {
    if (!pkg_name || !pkg_version || !pkg_info) {
        return -1;
    }

    char pkg_dir_path[PATH_MAX];
    char binary_file_path[PATH_MAX];

    if (runepkg_storage_get_package_path(pkg_name, pkg_version, pkg_dir_path) != 0) {
        return -1;
    }

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

    runepkg_log_verbose("Writing package info to: %s\n", binary_file_path);

    FILE *bin_file = fopen(binary_file_path, "wb");
    if (!bin_file) {
        printf("Error: Failed to open binary file for writing: %s\n", binary_file_path);
        return -1;
    }

    // Write string lengths and strings to binary file
    size_t len;
    
    // Helper macro to write a string and its length
    #define WRITE_STRING(s) \
        len = (s) ? strlen(s) + 1 : 0; \
        fwrite(&len, sizeof(size_t), 1, bin_file); \
        if (len > 0) fwrite(s, 1, len, bin_file);

    WRITE_STRING(pkg_info->package_name);
    WRITE_STRING(pkg_info->version);
    WRITE_STRING(pkg_info->architecture);
    WRITE_STRING(pkg_info->maintainer);
    WRITE_STRING(pkg_info->description);
    WRITE_STRING(pkg_info->depends);
    WRITE_STRING(pkg_info->installed_size);
    WRITE_STRING(pkg_info->section);
    WRITE_STRING(pkg_info->priority);
    WRITE_STRING(pkg_info->homepage);
    WRITE_STRING(pkg_info->filename);

    // Write file_count
    fwrite(&pkg_info->file_count, sizeof(int), 1, bin_file);

    // --- NEW: Write file list directly into the binary file ---
    if (pkg_info->file_list && pkg_info->file_count > 0) {
        for (int i = 0; i < pkg_info->file_count; i++) {
            WRITE_STRING(pkg_info->file_list[i]);
        }
    }
    // --- END NEW CODE ---

    fclose(bin_file);

    runepkg_log_verbose("Package info written successfully to persistent storage\n");
    return 0;
}

/**
 * @brief Reads package info from persistent storage
 */
int runepkg_storage_read_package_info(const char *pkg_name, const char *pkg_version,
                                     PkgInfo *pkg_info) {
    if (!pkg_name || !pkg_version || !pkg_info) {
        return -1;
    }

    char pkg_dir_path[PATH_MAX];
    char binary_file_path[PATH_MAX];

    if (runepkg_storage_get_package_path(pkg_name, pkg_version, pkg_dir_path) != 0) {
        return -1;
    }

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

    runepkg_pack_init_package_info(pkg_info);

    FILE *bin_file = fopen(binary_file_path, "rb");
    if (!bin_file) {
        printf("Error: Failed to open binary file for reading: %s\n", binary_file_path);
        return -1;
    }

    // Read string lengths and strings from binary file
    size_t len;
    
    // Helper macro to read a string and its length
    #define READ_STRING(s) \
        if (fread(&len, sizeof(size_t), 1, bin_file) != 1) goto read_error; \
        if (len > 0) { \
            s = malloc(len); \
            if (!s || fread(s, 1, len, bin_file) != len) goto read_error; \
        } else { \
            s = NULL; \
        }

    READ_STRING(pkg_info->package_name);
    READ_STRING(pkg_info->version);
    READ_STRING(pkg_info->architecture);
    READ_STRING(pkg_info->maintainer);
    READ_STRING(pkg_info->description);
    READ_STRING(pkg_info->depends);
    READ_STRING(pkg_info->installed_size);
    READ_STRING(pkg_info->section);
    READ_STRING(pkg_info->priority);
    READ_STRING(pkg_info->homepage);
    READ_STRING(pkg_info->filename);

    if (fread(&pkg_info->file_count, sizeof(int), 1, bin_file) != 1) goto read_error;
    
    // --- NEW: Read file list directly from the binary file ---
    if (pkg_info->file_count > 0) {
        pkg_info->file_list = malloc(pkg_info->file_count * sizeof(char *));
        if (!pkg_info->file_list) {
            printf("Error: Memory allocation failed for file list.\n");
            goto read_error;
        }

        for (int i = 0; i < pkg_info->file_count; i++) {
            READ_STRING(pkg_info->file_list[i]);
        }
    }
    // --- END NEW CODE ---

    fclose(bin_file);
    runepkg_log_verbose("Package info read successfully from persistent storage\n");
    return 0;

read_error:
    if (bin_file) {
        fclose(bin_file);
    }
    runepkg_pack_free_package_info(pkg_info);
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

    return runepkg_util_file_exists(binary_file_path) ? 1 : 0;
}

/**
 * @brief Prints package info from persistent storage
 */
int runepkg_storage_print_package_info(const char *pkg_name, const char *pkg_version) {
    PkgInfo pkg_info;
    
    if (runepkg_storage_read_package_info(pkg_name, pkg_version, &pkg_info) != 0) {
        return -1;
    }

    printf("\n=== Package Info from Persistent Storage ===\n");
    runepkg_pack_print_package_info(&pkg_info);
    
    runepkg_pack_free_package_info(&pkg_info);
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
