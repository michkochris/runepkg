/*****************************************************************************
 * Filename:    runepkg_pack.c
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Package extraction and information collection for runepkg
 * LICENSE:     GPL v3
 * THIS IS FREE SOFTWARE; YOU CAN REDISTRIBUTE IT AND/OR MODIFY IT UNDER
 * THE TERMS OF THE GNU GENERAL PUBLIC LICENSE AS PUBLISHED BY THE FREE
 * SOFTWARE FOUNDATION; EITHER VERSION 3 OF THE LICENSE, OR (AT YOUR OPTION)
 * ANY LATER VERSION.
 * THIS PROGRAM IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. SEE THE
 * GNU GENERAL PUBLIC LICENSE FOR MORE DETAILS.
 ***************************************************************************/

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
 * @brief Initializes a unified PkgInfo structure with NULL values.
 * @param pkg_info Pointer to the PkgInfo structure to initialize.
 */
void runepkg_pack_init_package_info(PkgInfo *pkg_info) {
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
 * @brief Frees all allocated memory in a unified PkgInfo structure.
 * @param pkg_info Pointer to the PkgInfo structure to free.
 */
void runepkg_pack_free_package_info(PkgInfo *pkg_info) {
    if (!pkg_info) return;
    
    runepkg_util_free_and_null(&pkg_info->package_name);
    runepkg_util_free_and_null(&pkg_info->version);
    runepkg_util_free_and_null(&pkg_info->architecture);
    runepkg_util_free_and_null(&pkg_info->maintainer);
    runepkg_util_free_and_null(&pkg_info->description);
    runepkg_util_free_and_null(&pkg_info->depends);
    runepkg_util_free_and_null(&pkg_info->installed_size);
    runepkg_util_free_and_null(&pkg_info->section);
    runepkg_util_free_and_null(&pkg_info->priority);
    runepkg_util_free_and_null(&pkg_info->homepage);
    runepkg_util_free_and_null(&pkg_info->filename);
    runepkg_util_free_and_null(&pkg_info->control_dir_path);
    runepkg_util_free_and_null(&pkg_info->data_dir_path);
    
    if (pkg_info->file_list) {
        for (int i = 0; i < pkg_info->file_count; i++) {
            runepkg_util_free_and_null(&pkg_info->file_list[i]);
        }
        free(pkg_info->file_list);
        pkg_info->file_list = NULL;
    }
    pkg_info->file_count = 0;
}

/**
 * @brief Creates a unique extraction directory name based on package info.
 * @param base_dir The base directory where extraction should occur.
 * @param deb_filename The name of the .deb file.
 * @return A dynamically allocated string with the full extraction path, or NULL on error.
 */
char *runepkg_pack_create_extraction_path(const char *base_dir, const char *deb_filename) {
    if (!base_dir || !deb_filename) {
        runepkg_util_error("create_extraction_path: NULL base_dir or deb_filename.\n");
        return NULL;
    }
    
    char *deb_copy = strdup(deb_filename);
    if (!deb_copy) {
        runepkg_util_error("Memory allocation failed for deb filename copy.\n");
        return NULL;
    }
    
    char *base_name = basename(deb_copy);
    
    char *dot = strrchr(base_name, '.');
    if (dot && strcmp(dot, ".deb") == 0) {
        *dot = '\0';
    }
    
    char *extraction_path = runepkg_util_concat_path(base_dir, base_name);
    
    runepkg_util_free_and_null(&deb_copy);
    return extraction_path;
}

// --- Control File Parsing ---

/**
 * @brief Parses a control file and extracts package information.
 * @param control_file_path The path to the control file.
 * @param pkg_info Pointer to package info structure to populate.
 * @return 0 on success, -1 on failure.
 */
int runepkg_pack_parse_control_file(const char *control_file_path, PkgInfo *pkg_info) {
    if (!control_file_path || !pkg_info) {
        runepkg_util_error("parse_control_file: NULL control_file_path or pkg_info.\n");
        return -1;
    }
    
    runepkg_util_log_verbose("Parsing control file: %s\n", control_file_path);
    
    if (!runepkg_util_file_exists(control_file_path)) {
        runepkg_util_error("Control file not found: %s\n", control_file_path);
        return -1;
    }
    
    pkg_info->package_name = runepkg_util_get_config_value(control_file_path, "Package", ':');
    pkg_info->version = runepkg_util_get_config_value(control_file_path, "Version", ':');
    pkg_info->architecture = runepkg_util_get_config_value(control_file_path, "Architecture", ':');
    pkg_info->maintainer = runepkg_util_get_config_value(control_file_path, "Maintainer", ':');
    pkg_info->description = runepkg_util_get_config_value(control_file_path, "Description", ':');
    pkg_info->depends = runepkg_util_get_config_value(control_file_path, "Depends", ':');
    pkg_info->installed_size = runepkg_util_get_config_value(control_file_path, "Installed-Size", ':');
    pkg_info->section = runepkg_util_get_config_value(control_file_path, "Section", ':');
    pkg_info->priority = runepkg_util_get_config_value(control_file_path, "Priority", ':');
    pkg_info->homepage = runepkg_util_get_config_value(control_file_path, "Homepage", ':');
    
    if (!pkg_info->package_name) {
        runepkg_util_error("Failed to parse Package name from control file.\n");
        return -1;
    }
    
    if (!pkg_info->version) {
        runepkg_util_error("Failed to parse Version from control file.\n");
        return -1;
    }
    
    if (!pkg_info->architecture) {
        runepkg_util_error("Failed to parse Architecture from control file.\n");
        return -1;
    }
    
    runepkg_util_log_verbose("Successfully parsed control file for package: %s %s (%s)\n", 
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
int runepkg_pack_extract_and_collect_info(const char *deb_path, const char *control_dir, PkgInfo *pkg_info) {
    if (!deb_path || !control_dir || !pkg_info) {
        runepkg_util_error("extract_and_collect_info: NULL parameter provided.\n");
        return -1;
    }
    
    runepkg_util_log_verbose("Starting package extraction and info collection for: %s\n", deb_path);
    
    runepkg_pack_init_package_info(pkg_info);
    
    if (!runepkg_util_file_exists(deb_path)) {
        runepkg_log_verbose(".deb file not found: %s\n", deb_path);
        return -1;
    }
    
    char *deb_copy_for_basename = strdup(deb_path);
    if (!deb_copy_for_basename) {
        runepkg_util_error("Memory allocation failed for deb path copy.\n");
        return -1;
    }
    pkg_info->filename = strdup(basename(deb_copy_for_basename));
    runepkg_util_free_and_null(&deb_copy_for_basename);
    
    if (!pkg_info->filename) {
        runepkg_util_error("Failed to store package filename.\n");
        return -1;
    }
    
    char *package_extract_dir = runepkg_pack_create_extraction_path(control_dir, deb_path);
    if (!package_extract_dir) {
        runepkg_util_error("Failed to create extraction directory path.\n");
        runepkg_pack_free_package_info(pkg_info);
        return -1;
    }
    
    runepkg_util_log_verbose("Extracting to directory: %s\n", package_extract_dir);
    
    if (runepkg_util_extract_deb_complete(deb_path, package_extract_dir) != 0) {
        runepkg_util_error("Failed to extract .deb package.\n");
        runepkg_util_free_and_null(&package_extract_dir);
        runepkg_pack_free_package_info(pkg_info);
        return -1;
    }
    
    pkg_info->control_dir_path = runepkg_util_concat_path(package_extract_dir, "control");
    pkg_info->data_dir_path = runepkg_util_concat_path(package_extract_dir, "data");
    
    if (!pkg_info->control_dir_path || !pkg_info->data_dir_path) {
        runepkg_util_error("Failed to create control/data directory paths.\n");
        runepkg_util_free_and_null(&package_extract_dir);
        runepkg_pack_free_package_info(pkg_info);
        return -1;
    }
    
    char *control_file_path = runepkg_util_concat_path(pkg_info->control_dir_path, "control");
    if (!control_file_path) {
        runepkg_util_error("Failed to create control file path.\n");
        runepkg_util_free_and_null(&package_extract_dir);
        runepkg_pack_free_package_info(pkg_info);
        return -1;
    }
    
    if (runepkg_pack_parse_control_file(control_file_path, pkg_info) != 0) {
        runepkg_util_error("Failed to parse control file.\n");
        runepkg_util_free_and_null(&control_file_path);
        runepkg_util_free_and_null(&package_extract_dir);
        runepkg_pack_free_package_info(pkg_info);
        return -1;
    }
    
    if (runepkg_pack_collect_file_list(pkg_info->data_dir_path, pkg_info) != 0) {
        runepkg_util_error("Failed to collect package file list.\n");
        runepkg_util_free_and_null(&control_file_path);
        runepkg_util_free_and_null(&package_extract_dir);
        runepkg_pack_free_package_info(pkg_info);
        return -1;
    }
    
    runepkg_util_free_and_null(&control_file_path);
    runepkg_util_free_and_null(&package_extract_dir);
    
    runepkg_util_log_verbose("Package extraction and info collection completed successfully.\n");
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
        runepkg_util_log_verbose("Could not open directory: %s\n", dir_path);
        return 0;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char *full_path = runepkg_util_concat_path(dir_path, entry->d_name);
        if (!full_path) {
            closedir(dp);
            return -1;
        }
        
        struct stat st;
        if (stat(full_path, &st) != 0) {
            runepkg_util_free_and_null(&full_path);
            continue;
        }
        
        if (S_ISDIR(st.st_mode)) {
            if (collect_files_recursive(full_path, base_path, file_list, file_count, capacity) != 0) {
                runepkg_util_free_and_null(&full_path);
                closedir(dp);
                return -1;
            }
        } else if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
            const char *relative_path = full_path;
            if (strncmp(full_path, base_path, strlen(base_path)) == 0) {
                relative_path = full_path + strlen(base_path);
                if (relative_path[0] == '/') {
                    relative_path++;
                }
            }
            
            if (*file_count >= *capacity) {
                *capacity = (*capacity == 0) ? 32 : (*capacity * 2);
                char **new_list = realloc(*file_list, sizeof(char*) * (*capacity));
                if (!new_list) {
                    runepkg_util_error("Failed to reallocate memory for file list.\n");
                    runepkg_util_free_and_null(&full_path);
                    closedir(dp);
                    return -1;
                }
                *file_list = new_list;
            }
            
            (*file_list)[*file_count] = strdup(relative_path);
            if (!(*file_list)[*file_count]) {
                runepkg_util_error("Failed to duplicate file path string.\n");
                runepkg_util_free_and_null(&full_path);
                closedir(dp);
                return -1;
            }
            (*file_count)++;
            
            runepkg_util_log_verbose("Added file to list: %s\n", relative_path);
        }
        
        runepkg_util_free_and_null(&full_path);
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
int runepkg_pack_collect_file_list(const char *data_dir_path, PkgInfo *pkg_info) {
    if (!data_dir_path || !pkg_info) {
        runepkg_util_error("collect_file_list: NULL data_dir_path or pkg_info.\n");
        return -1;
    }
    
    runepkg_util_log_verbose("Collecting file list from: %s\n", data_dir_path);
    
    if (!runepkg_util_file_exists(data_dir_path)) {
        runepkg_util_log_verbose("Data directory does not exist or is empty: %s\n", data_dir_path);
        pkg_info->file_list = NULL;
        pkg_info->file_count = 0;
        return 0;
    }
    
    int capacity = 0;
    pkg_info->file_count = 0;
    pkg_info->file_list = NULL;
    
    if (collect_files_recursive(data_dir_path, data_dir_path, &pkg_info->file_list, &pkg_info->file_count, &capacity) != 0) {
        runepkg_util_error("Failed to collect files from data directory.\n");
        return -1;
    }
    
    runepkg_util_log_verbose("Collected %d files from package data directory.\n", pkg_info->file_count);
    return 0;
}

// --- Display Functions ---

/**
 * @brief Prints package information in a readable format.
 * @param pkg_info Pointer to the package info structure to display.
 */
void runepkg_pack_print_package_info(const PkgInfo *pkg_info) {
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
