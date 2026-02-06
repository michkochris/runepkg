/******************************************************************************
 * Filename:    runepkg_storage.h
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

#ifndef RUNEPKG_STORAGE_H
#define RUNEPKG_STORAGE_H

#include <stdio.h>
#include <stdlib.h>
#include "runepkg_pack.h"
#include "runepkg_hash.h"

// Define PATH_MAX if not defined
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// --- Storage Constants ---
#define RUNEPKG_STORAGE_BINARY_FILE "pkginfo.bin"

// --- Storage Functions ---

/**
 * @brief Creates a package directory in the persistent storage
 * @param pkg_name The package name
 * @param pkg_version The package version
 * @return 0 on success, -1 on failure
 */
int runepkg_storage_create_package_directory(const char *pkg_name, const char *pkg_version);

/**
 * @brief Writes package info to persistent storage
 * @param pkg_name The package name
 * @param pkg_version The package version
 * @param pkg_info The package info struct to store
 * @return 0 on success, -1 on failure
 */
int runepkg_storage_write_package_info(const char *pkg_name, const char *pkg_version, 
                                      const PkgInfo *pkg_info);

/**
 * @brief Reads package info from persistent storage
 * @param pkg_name The package name
 * @param pkg_version The package version
 * @param pkg_info Pointer to store the read package info
 * @return 0 on success, -1 on failure
 */
int runepkg_storage_read_package_info(const char *pkg_name, const char *pkg_version,
                                     PkgInfo *pkg_info);

/**
 * @brief Removes a package from persistent storage
 * @param pkg_name The package name
 * @param pkg_version The package version
 * @return 0 on success, -1 on failure
 */
int runepkg_storage_remove_package(const char *pkg_name, const char *pkg_version);

/**
 * @brief Checks if a package exists in persistent storage
 * @param pkg_name The package name
 * @param pkg_version The package version
 * @return 1 if exists, 0 if not, -1 on error
 */
int runepkg_storage_package_exists(const char *pkg_name, const char *pkg_version);

/**
 * @brief Lists all packages in persistent storage
 * @return 0 on success, -1 on failure
 */
int runepkg_storage_list_packages(const char *pattern);

/**
 * @brief Gets the full path to a package directory
 * @param pkg_name The package name
 * @param pkg_version The package version
 * @param path_buffer Buffer to store the path (should be PATH_MAX size)
 * @return 0 on success, -1 on failure
 */
int runepkg_storage_get_package_path(const char *pkg_name, const char *pkg_version, 
                                    char *path_buffer);

/**
 * @brief Prints package info from persistent storage
 * @param pkg_name The package name
 * @param pkg_version The package version
 * @return 0 on success, -1 on failure
 */
int runepkg_storage_print_package_info(const char *pkg_name, const char *pkg_version);

/**
 * @brief Builds the binary autocomplete index
 * @return 0 on success, -1 on failure
 */
int runepkg_storage_build_autocomplete_index(void);

#endif // RUNEPKG_STORAGE_H
