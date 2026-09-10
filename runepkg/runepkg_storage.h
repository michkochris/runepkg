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

#ifdef __cplusplus
extern "C" {
#endif

#include "runepkg_portable.h"
#include <stdio.h>
#include <stdlib.h>
#include "runepkg_pack.h"
#include "runepkg_hash.h"

/* Binary index header used by the self-completing binary (mmap'd index) */
typedef struct {
    uint32_t magic;      /* 0x52554E45 ("RUNE") */
    uint32_t version;    /* Index format version */
    uint32_t entry_count;
    uint32_t strings_size; /* Size of string blob */
} AutocompleteHeader;

/* Define PATH_MAX if not defined */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* --- Storage Constants --- */
#define RUNEPKG_STORAGE_BINARY_FILE "pkginfo.bin"

/* --- Storage Functions --- */

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
 * @brief Resolves installed file list from dpkg info files for host-synced packages
 * @param pkg_name The package name
 * @param pkg_info Pointer to PkgInfo struct to populate with file_list and file_count
 * @return 0 on success, -1 on failure
 */
int runepkg_storage_load_host_file_list(const char *pkg_name, PkgInfo *pkg_info);

/**
 * @brief Removes a package from persistent storage
 * @param pkg_name The package name
 * @param pkg_version The package version
 * @return 0 on success, -1 on failure
 */
int runepkg_storage_remove_package(const char *pkg_name, const char *pkg_version);

/**
 * @brief Purges any host virtual dummy package records provided by pkg_name
 * @param pkg_name The provider package name
 * @return 0 on success, -1 on failure
 */
int runepkg_storage_remove_provided_dummies(const char *pkg_name);

/**
 * @brief Finds an installed package that provides virtual_pkg and populates out_info
 * @param virtual_pkg The virtual package name to search for
 * @param out_info Pointer to PkgInfo to receive virtual package details
 * @return 0 on success (found provider), -1 if not found
 */
int runepkg_storage_find_provider(const char *virtual_pkg, PkgInfo *out_info);

/**
 * @brief Recursively delete a directory tree (files and subdirs).
 * @return 0 on success, -1 on failure
 */
int runepkg_storage_remove_directory_tree(const char *path);

/**
 * @brief Checks if a package exists in persistent storage
 * @param pkg_name The package name
 * @param pkg_version The package version
 * @return 1 if exists, 0 if not, -1 on error
 */
int runepkg_storage_package_exists(const char *pkg_name, const char *pkg_version);

/**
 * @brief Removes old version directories for a package from persistent storage
 * @param pkg_name The package name
 * @param new_version The new package version to keep
 * @return Number of old version directories removed
 */
int runepkg_storage_remove_old_versions(const char *pkg_name, const char *new_version);

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

/* Relation types for Conflicts, Breaks, Provides */
#define RUNEPKG_RELATION_CONFLICTS 1
#define RUNEPKG_RELATION_BREAKS    2
#define RUNEPKG_RELATION_PROVIDES  3

/* Binary header for conflicts-breaks.bin */
typedef struct {
    uint32_t magic;         /* 0x52554E45 ("RUNE") */
    uint32_t version;       /* Format version (1) */
    uint32_t entry_count;   /* Number of relation entries */
    uint32_t strings_size;  /* Size of string table blob */
} ConflictsBreaksHeader;

/* Binary entry for conflicts-breaks.bin */
typedef struct {
    uint32_t src_pkg_offset;   /* String offset for source package name */
    uint32_t src_ver_offset;   /* String offset for source package version */
    uint32_t target_pkg_offset;/* String offset for target package/virtual name */
    uint32_t constraint_offset;/* String offset for version constraint */
    uint32_t relation_type;    /* RUNEPKG_RELATION_CONFLICTS, BREAKS, PROVIDES */
} ConflictsBreaksEntry;

#define RUNEPKG_STORAGE_CONFLICTS_BINARY_FILE "conflicts-breaks.bin"
#define RUNEPKG_STORAGE_CONFLICTS_TEXT_FILE   "conflicts-breaks.txt"

/**
 * @brief Builds the binary conflicts, breaks, and provides index (conflicts-breaks.bin / .txt)
 * @return 0 on success, -1 on failure
 */
int runepkg_storage_build_conflicts_breaks_index(void);

/**
 * @brief Checks if installing pkg_name (with pkg_version) violates any conflicts or breaks
 * @param pkg_name The package name to test
 * @param pkg_version The package version to test
 * @param conflict_target Buffer to receive conflicting package name if conflict found (optional)
 * @param target_size Size of conflict_target buffer
 * @return 1 if conflict/breaks found, 0 if safe, -1 on error
 */
int runepkg_storage_check_conflict(const char *pkg_name, const char *pkg_version, char *conflict_target, size_t target_size);

#ifdef __cplusplus
}
#endif

#endif /* RUNEPKG_STORAGE_H */
