/******************************************************************************
 * Filename:    upkg_pack.h
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

#ifndef UPKG_PACK_H
#define UPKG_PACK_H

#include <stdio.h>
#include <stdlib.h>

// Define PATH_MAX if not defined
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// --- Package Information Structure ---

/**
 * @brief Structure to hold package information extracted from control file
 */
typedef struct {
    char *package_name;
    char *version;
    char *architecture;
    char *maintainer;
    char *description;
    char *depends;
    char *installed_size;
    char *section;
    char *priority;
    char *homepage;
    char *filename;          // Original .deb filename
    char *control_dir_path;  // Path where control files are extracted
    char *data_dir_path;     // Path where data files are extracted
    char **file_list;        // Array of file paths contained in the package
    int file_count;          // Number of files in the package
} upkg_package_info_t;

// --- Function Prototypes ---

/**
 * @brief Initializes a package info structure with NULL values.
 * @param pkg_info Pointer to the package info structure to initialize.
 */
void upkg_pack_init_package_info(upkg_package_info_t *pkg_info);

/**
 * @brief Frees all allocated memory in a package info structure.
 * @param pkg_info Pointer to the package info structure to free.
 */
void upkg_pack_free_package_info(upkg_package_info_t *pkg_info);

/**
 * @brief Extracts a .deb package and collects package information.
 * 
 * This function performs the complete workflow:
 * 1. Extracts the .deb file to the control directory
 * 2. Parses the control file to collect package metadata
 * 3. Populates the package info structure
 *
 * @param deb_path The full path to the .deb package file.
 * @param control_dir The directory where the package should be extracted (from config).
 * @param pkg_info Pointer to package info structure to populate.
 * @return 0 on success, -1 on failure.
 */
int upkg_pack_extract_and_collect_info(const char *deb_path, const char *control_dir, upkg_package_info_t *pkg_info);

/**
 * @brief Parses a control file and extracts package information.
 * @param control_file_path The path to the control file.
 * @param pkg_info Pointer to package info structure to populate.
 * @return 0 on success, -1 on failure.
 */
int upkg_pack_parse_control_file(const char *control_file_path, upkg_package_info_t *pkg_info);

/**
 * @brief Prints package information in a readable format.
 * @param pkg_info Pointer to the package info structure to display.
 */
void upkg_pack_print_package_info(const upkg_package_info_t *pkg_info);

/**
 * @brief Creates a unique extraction directory name based on package info.
 * @param base_dir The base directory where extraction should occur.
 * @param deb_filename The name of the .deb file.
 * @return A dynamically allocated string with the full extraction path, or NULL on error.
 * The caller is responsible for freeing the returned string.
 */
char *upkg_pack_create_extraction_path(const char *base_dir, const char *deb_filename);

/**
 * @brief Collects a list of all files contained in the package data directory.
 * @param data_dir_path The path to the extracted data directory.
 * @param pkg_info Pointer to package info structure to populate with file list.
 * @return 0 on success, -1 on failure.
 */
int upkg_pack_collect_file_list(const char *data_dir_path, upkg_package_info_t *pkg_info);

#endif // UPKG_PACK_H
