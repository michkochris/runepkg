/******************************************************************************
 * Filename:    upkg_pack.c
 * Author:      <michkochris@gmail.com>
 * Date:        started 01-02-2025
 * Description: Package extraction and information collection for upkg
 *
 * Copyright (c) 2025 upkg (ulinux) All rights reserved.
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

#include "runepkg_pack.h"
#include "runepkg_util.h"
#include "runepkg_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <libgen.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

// Define PATH_MAX if not defined
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// External global variable for verbose logging
extern bool g_verbose_mode;

// --- Package Information Management ---

/**
 * @brief Initializes a package info structure with NULL values.
 * @param pkg_info Pointer to the package info structure to initialize.
 */
void upkg_pack_init_package_info(upkg_package_info_t *pkg_info) {
    if (!pkg_info) return;
    
    pkg_info->package_name = NULL;
    pkg_info->version = NULL;
    pkg_info->architecture = NULL;
    pkg_info->maintainer = NULL;
    pkg_info->description = NULL;
    pkg_info->depends = NULL;
    pkg_info->installed_size = NULL;
    pkg_info->section = NULL;
    pkg_info->priority = NULL;
    pkg_info->homepage = NULL;
    pkg_info->filename = NULL;
    pkg_info->control_dir_path = NULL;
    pkg_info->data_dir_path = NULL;
    pkg_info->file_list = NULL;
    pkg_info->file_count = 0;
}

/**
 * @brief Frees all allocated memory in a package info structure.
 * @param pkg_info Pointer to the package info structure to free.
 */
void upkg_pack_free_package_info(upkg_package_info_t *pkg_info) {
    if (!pkg_info) return;
    
    upkg_util_free_and_null(&pkg_info->package_name);
    upkg_util_free_and_null(&pkg_info->version);
    upkg_util_free_and_null(&pkg_info->architecture);
    upkg_util_free_and_null(&pkg_info->maintainer);
    upkg_util_free_and_null(&pkg_info->description);
    upkg_util_free_and_null(&pkg_info->depends);
    upkg_util_free_and_null(&pkg_info->installed_size);
    upkg_util_free_and_null(&pkg_info->section);
    upkg_util_free_and_null(&pkg_info->priority);
    upkg_util_free_and_null(&pkg_info->homepage);
    upkg_util_free_and_null(&pkg_info->filename);
    upkg_util_free_and_null(&pkg_info->control_dir_path);
    upkg_util_free_and_null(&pkg_info->data_dir_path);
    
    // Free the file list array
    if (pkg_info->file_list) {
        for (int i = 0; i < pkg_info->file_count; i++) {
            upkg_util_free_and_null(&pkg_info->file_list[i]);
        }
        upkg_util_free_and_null((char**)&pkg_info->file_list);
    }
    pkg_info->file_count = 0;
}

/**
 * @brief Creates a unique extraction directory name based on package info.
 * @param base_dir The base directory where extraction should occur.
 * @param deb_filename The name of the .deb file.
 * @return A dynamically allocated string with the full extraction path, or NULL on error.
 */
char *upkg_pack_create_extraction_path(const char *base_dir, const char *deb_filename) {
    if (!base_dir || !deb_filename) {
        upkg_util_error("create_extraction_path: NULL base_dir or deb_filename.\n");
        return NULL;
    }
    
    // Get the base filename without path and extension
    char *deb_copy = strdup(deb_filename);
    if (!deb_copy) {
        upkg_util_error("Memory allocation failed for deb filename copy.\n");
        return NULL;
    }
    
    char *base_name = basename(deb_copy);
    
    // Remove .deb extension if present
    char *dot = strrchr(base_name, '.');
    if (dot && strcmp(dot, ".deb") == 0) {
        *dot = '\0';
    }
    
    // Create the full extraction path
    char *extraction_path = upkg_util_concat_path(base_dir, base_name);
    
    upkg_util_free_and_null(&deb_copy);
    return extraction_path;
}

// --- Control File Parsing ---

/**
 * @brief Parses a control file and extracts package information.
 * @param control_file_path The path to the control file.
 * @param pkg_info Pointer to package info structure to populate.
 * @return 0 on success, -1 on failure.
 */
int upkg_pack_parse_control_file(const char *control_file_path, upkg_package_info_t *pkg_info) {
    if (!control_file_path || !pkg_info) {
        upkg_util_error("parse_control_file: NULL control_file_path or pkg_info.\n");
        return -1;
    }
    
    upkg_util_log_verbose("Parsing control file: %s\n", control_file_path);
    
    if (!upkg_util_file_exists(control_file_path)) {
        upkg_util_error("Control file not found: %s\n", control_file_path);
        return -1;
    }
    
    // Parse each field from the control file
    pkg_info->package_name = upkg_util_get_config_value(control_file_path, "Package", ':');
    pkg_info->version = upkg_util_get_config_value(control_file_path, "Version", ':');
    pkg_info->architecture = upkg_util_get_config_value(control_file_path, "Architecture", ':');
    pkg_info->maintainer = upkg_util_get_config_value(control_file_path, "Maintainer", ':');
    pkg_info->description = upkg_util_get_config_value(control_file_path, "Description", ':');
    pkg_info->depends = upkg_util_get_config_value(control_file_path, "Depends", ':');
    pkg_info->installed_size = upkg_util_get_config_value(control_file_path, "Installed-Size", ':');
    pkg_info->section = upkg_util_get_config_value(control_file_path, "Section", ':');
    pkg_info->priority = upkg_util_get_config_value(control_file_path, "Priority", ':');
    pkg_info->homepage = upkg_util_get_config_value(control_file_path, "Homepage", ':');
    
    // Validate that we got at least the essential fields
    if (!pkg_info->package_name) {
        upkg_util_error("Failed to parse Package name from control file.\n");
        return -1;
    }
    
    if (!pkg_info->version) {
        upkg_util_error("Failed to parse Version from control file.\n");
        return -1;
    }
    
    if (!pkg_info->architecture) {
        upkg_util_error("Failed to parse Architecture from control file.\n");
        return -1;
    }
    
    upkg_util_log_verbose("Successfully parsed control file for package: %s %s (%s)\n", 
                         pkg_info->package_name, pkg_info->version, pkg_info->architecture);
    
    return 0;
}

// --- Main Package Processing Function ---

/**
 * @brief Extracts a .deb package and collects package information.
 * @param deb_path The full path to the .deb package file.
 * @param control_dir The directory where the package should be extracted.
 * @param pkg_info Pointer to package info structure to populate.
 * @return 0 on success, -1 on failure.
 */
int upkg_pack_extract_and_collect_info(const char *deb_path, const char *control_dir, upkg_package_info_t *pkg_info) {
    if (!deb_path || !control_dir || !pkg_info) {
        upkg_util_error("extract_and_collect_info: NULL parameter provided.\n");
        return -1;
    }
    
    upkg_util_log_verbose("Starting package extraction and info collection for: %s\n", deb_path);
    
    // Initialize the package info structure
    upkg_pack_init_package_info(pkg_info);
    
    // Verify the .deb file exists
    if (!upkg_util_file_exists(deb_path)) {
        upkg_util_error(".deb file not found: %s\n", deb_path);
        return -1;
    }
    
    // Store the original filename
    pkg_info->filename = strdup(basename((char*)deb_path));
    if (!pkg_info->filename) {
        upkg_util_error("Failed to store package filename.\n");
        return -1;
    }
    
    // Create a unique extraction directory for this package
    char *package_extract_dir = upkg_pack_create_extraction_path(control_dir, deb_path);
    if (!package_extract_dir) {
        upkg_util_error("Failed to create extraction directory path.\n");
        upkg_pack_free_package_info(pkg_info);
        return -1;
    }
    
    upkg_util_log_verbose("Extracting to directory: %s\n", package_extract_dir);
    
    // Step 1: Extract the .deb package completely
    if (upkg_util_extract_deb_complete(deb_path, package_extract_dir) != 0) {
        upkg_util_error("Failed to extract .deb package.\n");
        upkg_util_free_and_null(&package_extract_dir);
        upkg_pack_free_package_info(pkg_info);
        return -1;
    }
    
    // Step 2: Set up paths for control and data directories
    pkg_info->control_dir_path = upkg_util_concat_path(package_extract_dir, "control");
    pkg_info->data_dir_path = upkg_util_concat_path(package_extract_dir, "data");
    
    if (!pkg_info->control_dir_path || !pkg_info->data_dir_path) {
        upkg_util_error("Failed to create control/data directory paths.\n");
        upkg_util_free_and_null(&package_extract_dir);
        upkg_pack_free_package_info(pkg_info);
        return -1;
    }
    
    // Step 3: Parse the control file
    char *control_file_path = upkg_util_concat_path(pkg_info->control_dir_path, "control");
    if (!control_file_path) {
        upkg_util_error("Failed to create control file path.\n");
        upkg_util_free_and_null(&package_extract_dir);
        upkg_pack_free_package_info(pkg_info);
        return -1;
    }
    
    if (upkg_pack_parse_control_file(control_file_path, pkg_info) != 0) {
        upkg_util_error("Failed to parse control file.\n");
        upkg_util_free_and_null(&control_file_path);
        upkg_util_free_and_null(&package_extract_dir);
        upkg_pack_free_package_info(pkg_info);
        return -1;
    }
    
    // Step 4: Collect file list from data directory
    if (upkg_pack_collect_file_list(pkg_info->data_dir_path, pkg_info) != 0) {
        upkg_util_error("Failed to collect package file list.\n");
        upkg_util_free_and_null(&control_file_path);
        upkg_util_free_and_null(&package_extract_dir);
        upkg_pack_free_package_info(pkg_info);
        return -1;
    }
    
    // Clean up temporary variables
    upkg_util_free_and_null(&control_file_path);
    upkg_util_free_and_null(&package_extract_dir);
    
    upkg_util_log_verbose("Package extraction and info collection completed successfully.\n");
    return 0;
}

// --- File List Collection ---

/**
 * @brief Recursively collects all files in a directory.
 * @param dir_path The directory path to scan.
 * @param base_path The base path to remove from file paths (for relative paths).
 * @param file_list Pointer to array of file paths.
 * @param file_count Pointer to current file count.
 * @param capacity Pointer to current array capacity.
 * @return 0 on success, -1 on failure.
 */
static int collect_files_recursive(const char *dir_path, const char *base_path, char ***file_list, int *file_count, int *capacity) {
    DIR *dp = opendir(dir_path);
    if (!dp) {
        upkg_util_log_verbose("Could not open directory: %s\n", dir_path);
        return 0; // Not necessarily an error, might be empty
    }
    
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        // Skip . and .. entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char *full_path = upkg_util_concat_path(dir_path, entry->d_name);
        if (!full_path) {
            closedir(dp);
            return -1;
        }
        
        struct stat st;
        if (stat(full_path, &st) != 0) {
            upkg_util_free_and_null(&full_path);
            continue;
        }
        
        if (S_ISDIR(st.st_mode)) {
            // Recursively process subdirectory
            if (collect_files_recursive(full_path, base_path, file_list, file_count, capacity) != 0) {
                upkg_util_free_and_null(&full_path);
                closedir(dp);
                return -1;
            }
        } else if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
            // Add regular file or symlink to the list
            
            // Create relative path by removing base_path prefix
            const char *relative_path = full_path;
            if (strncmp(full_path, base_path, strlen(base_path)) == 0) {
                relative_path = full_path + strlen(base_path);
                // Skip leading slash if present
                if (relative_path[0] == '/') {
                    relative_path++;
                }
            }
            
            // Ensure we have enough capacity
            if (*file_count >= *capacity) {
                *capacity = (*capacity == 0) ? 32 : (*capacity * 2);
                char **new_list = realloc(*file_list, sizeof(char*) * (*capacity));
                if (!new_list) {
                    upkg_util_error("Failed to reallocate memory for file list.\n");
                    upkg_util_free_and_null(&full_path);
                    closedir(dp);
                    return -1;
                }
                *file_list = new_list;
            }
            
            // Add the file to the list
            (*file_list)[*file_count] = strdup(relative_path);
            if (!(*file_list)[*file_count]) {
                upkg_util_error("Failed to duplicate file path string.\n");
                upkg_util_free_and_null(&full_path);
                closedir(dp);
                return -1;
            }
            (*file_count)++;
            
            upkg_util_log_verbose("Added file to list: %s\n", relative_path);
        }
        
        upkg_util_free_and_null(&full_path);
    }
    
    closedir(dp);
    return 0;
}

/**
 * @brief Collects a list of all files contained in the package data directory.
 * @param data_dir_path The path to the extracted data directory.
 * @param pkg_info Pointer to package info structure to populate with file list.
 * @return 0 on success, -1 on failure.
 */
int upkg_pack_collect_file_list(const char *data_dir_path, upkg_package_info_t *pkg_info) {
    if (!data_dir_path || !pkg_info) {
        upkg_util_error("collect_file_list: NULL data_dir_path or pkg_info.\n");
        return -1;
    }
    
    upkg_util_log_verbose("Collecting file list from: %s\n", data_dir_path);
    
    // Check if data directory exists
    if (!upkg_util_file_exists(data_dir_path)) {
        upkg_util_log_verbose("Data directory does not exist or is empty: %s\n", data_dir_path);
        // This is not necessarily an error - some packages might not have data files
        pkg_info->file_list = NULL;
        pkg_info->file_count = 0;
        return 0;
    }
    
    // Initialize file list
    int capacity = 0;
    pkg_info->file_count = 0;
    pkg_info->file_list = NULL;
    
    // Collect files recursively
    if (collect_files_recursive(data_dir_path, data_dir_path, &pkg_info->file_list, &pkg_info->file_count, &capacity) != 0) {
        upkg_util_error("Failed to collect files from data directory.\n");
        return -1;
    }
    
    upkg_util_log_verbose("Collected %d files from package data directory.\n", pkg_info->file_count);
    return 0;
}

// --- Display Functions ---

/**
 * @brief Prints package information in a readable format.
 * @param pkg_info Pointer to the package info structure to display.
 */
void upkg_pack_print_package_info(const upkg_package_info_t *pkg_info) {
    if (!pkg_info) {
        printf("No package information available.\n");
        return;
    }
    
    printf("Package Information:\n");
    printf("===================\n");
    
    if (pkg_info->package_name) {
        printf("Package:      %s\n", pkg_info->package_name);
    }
    if (pkg_info->version) {
        printf("Version:      %s\n", pkg_info->version);
    }
    if (pkg_info->architecture) {
        printf("Architecture: %s\n", pkg_info->architecture);
    }
    if (pkg_info->maintainer) {
        printf("Maintainer:   %s\n", pkg_info->maintainer);
    }
    if (pkg_info->section) {
        printf("Section:      %s\n", pkg_info->section);
    }
    if (pkg_info->priority) {
        printf("Priority:     %s\n", pkg_info->priority);
    }
    if (pkg_info->installed_size) {
        printf("Installed-Size: %s\n", pkg_info->installed_size);
    }
    if (pkg_info->depends) {
        printf("Depends:      %s\n", pkg_info->depends);
    }
    if (pkg_info->homepage) {
        printf("Homepage:     %s\n", pkg_info->homepage);
    }
    if (pkg_info->description) {
        printf("Description:  %s\n", pkg_info->description);
    }
    
    printf("\nExtraction Paths:\n");
    if (pkg_info->filename) {
        printf("Filename:     %s\n", pkg_info->filename);
    }
    if (pkg_info->control_dir_path) {
        printf("Control Dir:  %s\n", pkg_info->control_dir_path);
    }
    if (pkg_info->data_dir_path) {
        printf("Data Dir:     %s\n", pkg_info->data_dir_path);
    }
    
    // Print file list
    printf("\nPackage Contents (%d files):\n", pkg_info->file_count);
    if (pkg_info->file_count > 0 && pkg_info->file_list) {
        printf("========================\n");
        for (int i = 0; i < pkg_info->file_count; i++) {
            printf("  %s\n", pkg_info->file_list[i]);
        }
    } else {
        printf("  (No files or empty package)\n");
    }
    printf("\n");
}
