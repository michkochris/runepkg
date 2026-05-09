/******************************************************************************
 * Filename:    runepkg_hash.h
 * Author:      <michkochris@gmail.com>
 * Date:        started 01-02-2025
 * Description: Hash table for runepkg (runar linux) package management
 *
 * Copyright (c) 2025 runepkg (runar linux) All rights reserved.
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

#ifndef RUNEPKG_HASH_H
#define RUNEPKG_HASH_H

#include <stddef.h>
#include <stdbool.h>

// --- Hash Table Configuration ---
#define INITIAL_HASH_TABLE_SIZE 2
#define GROW_LOAD_FACTOR_THRESHOLD 0.75
#define SHRINK_LOAD_FACTOR_THRESHOLD 0.25
#define MIN_HASH_TABLE_SIZE 2
#define MAX_SUGGESTIONS 10

// --- Unified PkgInfo Structure ---
// This is the unified struct that will be used across the application.
// It's the single source of truth for all package data.
typedef struct PkgInfo {
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
    char *filename;
    char **file_list;
    int file_count;
    char *control_dir_path; // Re-added: Path to the extracted 'control' directory.
    char *data_dir_path;    // Re-added: Path to the extracted 'data' directory.
} PkgInfo;

// --- Hash Table Node Structure ---
typedef struct runepkg_hash_node {
    PkgInfo data; // Use the new PkgInfo struct
    struct runepkg_hash_node *next;
} runepkg_hash_node_t;

// --- Hash Table Structure ---
typedef struct runepkg_hash_table {
    runepkg_hash_node_t **buckets;
    size_t size;
    size_t count;
} runepkg_hash_table_t;

// --- Global Variables ---
extern bool g_verbose_mode;
extern runepkg_hash_table_t *runepkg_main_hash_table;

// --- Function Prototypes ---

/**
 * @brief Creates and initializes a new hash table.
 * @param initial_size The desired initial size of the hash table.
 * @return A pointer to the new hash table, or NULL on failure.
 */
runepkg_hash_table_t* runepkg_hash_create_table(size_t initial_size);

/**
 * @brief Searches the hash table for a package.
 * @param table A pointer to the hash table.
 * @param name The name of the package to search for.
 * @return A pointer to the package info, or NULL if not found.
 */
PkgInfo* runepkg_hash_search(runepkg_hash_table_t *table, const char *name);

/**
 * @brief Adds a package to the hash table with deep copy.
 * @param table A pointer to the hash table.
 * @param pkg_info The package info to add.
 * @return 0 on success, -1 on failure.
 */
int runepkg_hash_add_package(runepkg_hash_table_t *table, const PkgInfo *pkg_info);

/**
 * @brief Removes a package from the hash table.
 * @param table A pointer to the hash table.
 * @param name The name of the package to remove.
 */
void runepkg_hash_remove_package(runepkg_hash_table_t *table, const char *name);

/**
 * @brief Destroys the hash table and frees all memory.
 * @param table A pointer to the hash table to destroy.
 */
void runepkg_hash_destroy_table(runepkg_hash_table_t *table);

/**
 * @brief Prints package information from the hash table.
 * @param pkg_info A pointer to the package info to print.
 */
void runepkg_hash_print_package_info(const PkgInfo *pkg_info);

/**
 * @brief Lists all packages in the hash table.
 * @param table A pointer to the hash table.
 */
void runepkg_hash_list_packages(runepkg_hash_table_t *table);

/**
 * @brief Frees all allocated memory in a hash package info structure.
 * @param pkg_info Pointer to the package info structure to free.
 */
void runepkg_hash_free_package_info(PkgInfo *pkg_info);

#endif // RUNEPKG_HASH_H
