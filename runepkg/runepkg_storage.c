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
#include <sys/ioctl.h>
#include <fnmatch.h>

#include "runepkg_storage.h"
#include "runepkg_config.h"
#include "runepkg_util.h"
#include "runepkg_pack.h"
static int runepkg_storage_remove_dir_recursive(const char *path);

// Binary index structures (from self-completing-binary.txt)
typedef struct {
    uint32_t magic;      // 0x52554E45 ("RUNE")
    uint32_t version;    // Index format version
    uint32_t entry_count;
    uint32_t strings_size; // Size of string blob
} AutocompleteHeader;

// Compare function for qsort
static int compare_packages(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
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
        runepkg_log_verbose("Failed to open binary file for writing: %s\n", binary_file_path);
        return -1;
    }

    // Write PkgHeader for fast mmap access
    PkgHeader header;
    header.magic = 0x52554E45;  // "RUNE"
    memset(header.pkgname, 0, sizeof(header.pkgname));
    memset(header.version, 0, sizeof(header.version));
    if (pkg_name) strncpy(header.pkgname, pkg_name, sizeof(header.pkgname) - 1);
    if (pkg_version) strncpy(header.version, pkg_version, sizeof(header.version) - 1);
    header.data_start = sizeof(PkgHeader);  // Data starts after header

    fwrite(&header, sizeof(PkgHeader), 1, bin_file);

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
        runepkg_log_verbose("Failed to open binary file for reading: %s\n", binary_file_path);
        return -1;
    }

    // Skip the PkgHeader
    if (fseek(bin_file, sizeof(PkgHeader), SEEK_SET) != 0) {
        fclose(bin_file);
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

    if (runepkg_storage_remove_dir_recursive(pkg_dir_path) != 0) {
        printf("Warning: Failed to remove package directory: %s\n", pkg_dir_path);
        return -1;
    }

    return 0;
}

// --- Internal Helpers ---
static int runepkg_storage_remove_dir_recursive(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        return -1;
    }

    struct dirent *entry;
    int ret = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char child[PATH_MAX];
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);

        struct stat st;
        if (lstat(child, &st) != 0) {
            ret = -1;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (runepkg_storage_remove_dir_recursive(child) != 0) {
                ret = -1;
            }
        } else {
            if (unlink(child) != 0) {
                ret = -1;
            }
        }
    }

    closedir(dir);
    if (rmdir(path) != 0) {
        ret = -1;
    }

    return ret;
}

/**
 * @brief Lists all packages in persistent storage
 */
int runepkg_storage_list_packages(const char *pattern) {
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
    char *packages[1024];
    int count = 0;
    size_t max_len = 0;
    
    while ((entry = readdir(dir)) != NULL && count < 1024) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            if (!pattern || strncmp(entry->d_name, pattern, strlen(pattern)) == 0) {
                packages[count] = strdup(entry->d_name);
                if (packages[count]) {
                    size_t len = strlen(packages[count]);
                    if (len > max_len) max_len = len;
                    count++;
                }
            }
        }
    }

    closedir(dir);

    if (count == 0) {
        return 0; // No packages
    }

    // Sort packages alphabetically
    qsort(packages, count, sizeof(char *), compare_packages);

    // Get terminal width
    struct winsize w;
    int width = 80; // default
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        width = w.ws_col;
    }

    // Calculate number of columns
    int col_width = max_len + 2; // +2 for spacing
    int cols = width / col_width;
    if (cols < 1) cols = 1;

    // Print in columns
    int rows = (count + cols - 1) / cols; // ceil(count / cols)
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = r * cols + c;
            if (idx < count) {
                printf("%-*s", (int)col_width, packages[idx]);
            }
        }
        printf("\n");
    }

    // Free memory
    for (int i = 0; i < count; i++) {
        free(packages[i]);
    }

    return count;
}

/**
 * @brief Builds the binary autocomplete index (runepkg_autocomplete.bin)
 */
int runepkg_storage_build_autocomplete_index(void) {
    if (!g_runepkg_db_dir) {
        runepkg_log_verbose("Error: runepkg database directory not configured.\n");
        return -1;
    }

    runepkg_log_verbose("Building autocomplete index from: %s\n", g_runepkg_db_dir);

    DIR *dir = opendir(g_runepkg_db_dir);
    if (!dir) {
        runepkg_log_verbose("Error: Cannot open runepkg database directory: %s\n", g_runepkg_db_dir);
        return -1;
    }

    char **packages = NULL;
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            packages = realloc(packages, (count + 1) * sizeof(char *));
            if (!packages) {
                runepkg_log_verbose("Error: Memory allocation failed.\n");
                closedir(dir);
                return -1;
            }
            packages[count] = strdup(entry->d_name);
            if (!packages[count]) {
                runepkg_log_verbose("Error: String duplication failed.\n");
                closedir(dir);
                for (int i = 0; i < count; i++) free(packages[i]);
                free(packages);
                return -1;
            }
            count++;
        }
    }
    closedir(dir);

    if (count == 0) {
        runepkg_log_verbose("No packages found, skipping index build.\n");
        return 0;
    }

    // Sort packages alphabetically
    qsort(packages, count, sizeof(char *), compare_packages);

    // Calculate sizes
    size_t strings_size = 0;
    for (int i = 0; i < count; i++) {
        strings_size += strlen(packages[i]) + 1; // +1 for null terminator
    }

    // Build the file path
    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/runepkg_autocomplete.bin", g_runepkg_db_dir);

    FILE *fp = fopen(index_path, "wb");
    if (!fp) {
        runepkg_log_verbose("Error: Cannot create index file: %s\n", index_path);
        for (int i = 0; i < count; i++) free(packages[i]);
        free(packages);
        return -1;
    }

    // Write header
    AutocompleteHeader hdr = {
        .magic = 0x52554E45, // "RUNE"
        .version = 1,
        .entry_count = (uint32_t)count,
        .strings_size = (uint32_t)strings_size
    };
    fwrite(&hdr, sizeof(hdr), 1, fp);

    // Write offset table (relative to start of string blob)
    uint32_t offset = 0;
    for (int i = 0; i < count; i++) {
        fwrite(&offset, sizeof(uint32_t), 1, fp);
        offset += strlen(packages[i]) + 1;
    }

    // Write string blob
    for (int i = 0; i < count; i++) {
        fwrite(packages[i], strlen(packages[i]) + 1, 1, fp);
    }

    fclose(fp);

    // Make the index readable by all users
    if (chmod(index_path, 0644) != 0) {
        runepkg_log_verbose("Warning: Failed to set permissions on autocomplete index\n");
    }

    // Free memory
    for (int i = 0; i < count; i++) free(packages[i]);
    free(packages);

    runepkg_log_verbose("Autocomplete index built: %d entries, %s\n", count, index_path);
    return 0;
}
