/******************************************************************************
 * Filename:    upkg_hash.c
 * Author:      <michkochris@gmail.com>
 * Date:        started 01-02-2025
 * Description: Hash table implementation for upkg package management
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

#include "runepkg_hash.h"
#include "runepkg_util.h"
#include "runepkg_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// --- Global Variables ---
upkg_hash_table_t *upkg_main_hash_table = NULL;

// --- Utility Functions ---

/**
 * @brief Checks if a number is prime.
 * @param num The number to check.
 * @return True if prime, false otherwise.
 */
static bool is_prime(size_t num) {
    if (num <= 1) return false;
    if (num <= 3) return true;
    if (num % 2 == 0 || num % 3 == 0) return false;

    for (size_t i = 5; i * i <= num; i += 6) {
        if (num % i == 0 || num % (i + 2) == 0)
            return false;
    }
    return true;
}

/**
 * @brief Finds the next prime number greater than or equal to num.
 * @param num The starting number.
 * @return The next prime number.
 */
static size_t find_next_prime(size_t num) {
    if (num <= 2) return 2;
    if (num % 2 == 0) num++;

    while (!is_prime(num)) {
        num += 2;
    }
    return num;
}

/**
 * @brief Hash function using FNV-1a algorithm.
 * @param name The string to hash.
 * @param table_size The size of the hash table.
 * @return The hash value.
 */
static unsigned int hash_function(const char *name, size_t table_size) {
    if (!name || table_size == 0) return 0;

    const unsigned int FNV_PRIME_32 = 16777619U;
    const unsigned int FNV_OFFSET_BASIS_32 = 2166136261U;

    unsigned int hash = FNV_OFFSET_BASIS_32;
    for (const char *p = name; *p != '\0'; p++) {
        hash ^= (unsigned char)*p;
        hash *= FNV_PRIME_32;
    }
    return hash % table_size;
}

// --- Memory Management Functions ---

/**
 * @brief Frees all allocated memory in a hash package info structure.
 * @param pkg_info Pointer to the package info structure to free.
 */
void upkg_hash_free_package_info(upkg_hash_package_info_t *pkg_info) {
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

    if (pkg_info->file_list) {
        for (int i = 0; i < pkg_info->file_count; i++) {
            upkg_util_free_and_null(&pkg_info->file_list[i]);
        }
        upkg_util_free_and_null((char**)&pkg_info->file_list);
    }
    pkg_info->file_count = 0;
}

// --- Hash Table Core Functions ---

/**
 * @brief Creates and initializes a new hash table.
 * @param initial_size The desired initial size of the hash table.
 * @return A pointer to the new hash table, or NULL on failure.
 */
upkg_hash_table_t* upkg_hash_create_table(size_t initial_size) {
    upkg_hash_table_t *table = malloc(sizeof(upkg_hash_table_t));
    if (!table) {
        upkg_util_error("Failed to allocate memory for hash table structure.\n");
        return NULL;
    }

    if (initial_size < MIN_HASH_TABLE_SIZE) {
        initial_size = MIN_HASH_TABLE_SIZE;
    }
    initial_size = find_next_prime(initial_size);

    table->buckets = calloc(initial_size, sizeof(upkg_hash_node_t*));
    if (!table->buckets) {
        upkg_util_error("Failed to allocate memory for hash table buckets.\n");
        free(table);
        return NULL;
    }

    table->size = initial_size;
    table->count = 0;

    upkg_util_log_verbose("Hash table created with size %zu\n", table->size);
    return table;
}

/**
 * @brief Searches the hash table for a package.
 * @param table A pointer to the hash table.
 * @param name The name of the package to search for.
 * @return A pointer to the package info, or NULL if not found.
 */
upkg_hash_package_info_t* upkg_hash_search(upkg_hash_table_t *table, const char *name) {
    if (!table || !name || name[0] == '\0') return NULL;

    unsigned int index = hash_function(name, table->size);
    upkg_hash_node_t *current = table->buckets[index];

    while (current) {
        if (current->data.package_name && strcmp(current->data.package_name, name) == 0) {
            return &current->data;
        }
        current = current->next;
    }
    return NULL;
}

/**
 * @brief Resizes the hash table.
 * @param table A pointer to the hash table.
 * @param new_size The new size for the table.
 * @return 0 on success, -1 on failure.
 */
static int resize_hash_table(upkg_hash_table_t *table, size_t new_size) {
    if (!table) return -1;

    if (new_size < MIN_HASH_TABLE_SIZE) {
        new_size = MIN_HASH_TABLE_SIZE;
    }
    new_size = find_next_prime(new_size);

    if (new_size == table->size) return 0;

    upkg_hash_node_t **new_buckets = calloc(new_size, sizeof(upkg_hash_node_t*));
    if (!new_buckets) {
        upkg_util_error("Failed to allocate memory for hash table resize.\n");
        return -1;
    }

    upkg_util_log_verbose("Resizing hash table from %zu to %zu buckets\n", table->size, new_size);

    // Rehash all existing nodes
    upkg_hash_node_t **old_buckets = table->buckets;
    size_t old_size = table->size;

    table->buckets = new_buckets;
    table->size = new_size;
    table->count = 0;

    for (size_t i = 0; i < old_size; i++) {
        upkg_hash_node_t *current = old_buckets[i];
        while (current) {
            upkg_hash_node_t *next = current->next;
            
            // Rehash and insert into new table
            unsigned int new_index = hash_function(current->data.package_name, new_size);
            current->next = table->buckets[new_index];
            table->buckets[new_index] = current;
            table->count++;
            
            current = next;
        }
    }

    free(old_buckets);
    return 0;
}

/**
 * @brief Adds a package to the hash table with deep copy.
 * @param table A pointer to the hash table.
 * @param pkg_info The package info to add.
 * @return 0 on success, -1 on failure.
 */
int upkg_hash_add_package(upkg_hash_table_t *table, const upkg_hash_package_info_t *pkg_info) {
    if (!table || !pkg_info || !pkg_info->package_name) {
        upkg_util_error("Invalid parameters for hash table add operation.\n");
        return -1;
    }

    // Check if package already exists
    upkg_hash_package_info_t *existing = upkg_hash_search(table, pkg_info->package_name);
    if (existing) {
        upkg_util_log_verbose("Package '%s' already exists in hash table, updating.\n", pkg_info->package_name);
        upkg_hash_free_package_info(existing);
        
        // Update existing entry with new data
        existing->package_name = pkg_info->package_name ? strdup(pkg_info->package_name) : NULL;
        existing->version = pkg_info->version ? strdup(pkg_info->version) : NULL;
        existing->architecture = pkg_info->architecture ? strdup(pkg_info->architecture) : NULL;
        existing->maintainer = pkg_info->maintainer ? strdup(pkg_info->maintainer) : NULL;
        existing->description = pkg_info->description ? strdup(pkg_info->description) : NULL;
        existing->depends = pkg_info->depends ? strdup(pkg_info->depends) : NULL;
        existing->installed_size = pkg_info->installed_size ? strdup(pkg_info->installed_size) : NULL;
        existing->section = pkg_info->section ? strdup(pkg_info->section) : NULL;
        existing->priority = pkg_info->priority ? strdup(pkg_info->priority) : NULL;
        existing->homepage = pkg_info->homepage ? strdup(pkg_info->homepage) : NULL;
        existing->filename = pkg_info->filename ? strdup(pkg_info->filename) : NULL;
        
        // Copy file list
        if (pkg_info->file_list && pkg_info->file_count > 0) {
            existing->file_list = malloc(pkg_info->file_count * sizeof(char*));
            if (existing->file_list) {
                existing->file_count = pkg_info->file_count;
                for (int i = 0; i < pkg_info->file_count; i++) {
                    existing->file_list[i] = pkg_info->file_list[i] ? strdup(pkg_info->file_list[i]) : NULL;
                }
            } else {
                existing->file_count = 0;
            }
        } else {
            existing->file_list = NULL;
            existing->file_count = 0;
        }
        
        return 0;
    }

    // Check load factor and resize if needed
    if ((double)(table->count + 1) / table->size > GROW_LOAD_FACTOR_THRESHOLD) {
        if (resize_hash_table(table, table->size * 2) != 0) {
            upkg_util_error("Failed to resize hash table during add operation.\n");
            return -1;
        }
    }

    // Create new node
    upkg_hash_node_t *new_node = malloc(sizeof(upkg_hash_node_t));
    if (!new_node) {
        upkg_util_error("Failed to allocate memory for new hash table node.\n");
        return -1;
    }

    // Initialize the node data
    memset(&new_node->data, 0, sizeof(upkg_hash_package_info_t));

    // Deep copy all string fields
    new_node->data.package_name = pkg_info->package_name ? strdup(pkg_info->package_name) : NULL;
    new_node->data.version = pkg_info->version ? strdup(pkg_info->version) : NULL;
    new_node->data.architecture = pkg_info->architecture ? strdup(pkg_info->architecture) : NULL;
    new_node->data.maintainer = pkg_info->maintainer ? strdup(pkg_info->maintainer) : NULL;
    new_node->data.description = pkg_info->description ? strdup(pkg_info->description) : NULL;
    new_node->data.depends = pkg_info->depends ? strdup(pkg_info->depends) : NULL;
    new_node->data.installed_size = pkg_info->installed_size ? strdup(pkg_info->installed_size) : NULL;
    new_node->data.section = pkg_info->section ? strdup(pkg_info->section) : NULL;
    new_node->data.priority = pkg_info->priority ? strdup(pkg_info->priority) : NULL;
    new_node->data.homepage = pkg_info->homepage ? strdup(pkg_info->homepage) : NULL;
    new_node->data.filename = pkg_info->filename ? strdup(pkg_info->filename) : NULL;

    // Deep copy file list
    if (pkg_info->file_list && pkg_info->file_count > 0) {
        new_node->data.file_list = malloc(pkg_info->file_count * sizeof(char*));
        if (new_node->data.file_list) {
            new_node->data.file_count = pkg_info->file_count;
            for (int i = 0; i < pkg_info->file_count; i++) {
                new_node->data.file_list[i] = pkg_info->file_list[i] ? strdup(pkg_info->file_list[i]) : NULL;
            }
        } else {
            new_node->data.file_count = 0;
        }
    } else {
        new_node->data.file_list = NULL;
        new_node->data.file_count = 0;
    }

    // Insert into hash table
    unsigned int index = hash_function(pkg_info->package_name, table->size);
    new_node->next = table->buckets[index];
    table->buckets[index] = new_node;
    table->count++;

    upkg_util_log_verbose("Package '%s' added to hash table.\n", pkg_info->package_name);
    return 0;
}

/**
 * @brief Removes a package from the hash table.
 * @param table A pointer to the hash table.
 * @param name The name of the package to remove.
 */
void upkg_hash_remove_package(upkg_hash_table_t *table, const char *name) {
    if (!table || !name || name[0] == '\0') return;

    unsigned int index = hash_function(name, table->size);
    upkg_hash_node_t *current = table->buckets[index];
    upkg_hash_node_t *prev = NULL;

    while (current && strcmp(current->data.package_name, name) != 0) {
        prev = current;
        current = current->next;
    }

    if (current) {
        if (prev) {
            prev->next = current->next;
        } else {
            table->buckets[index] = current->next;
        }

        upkg_hash_free_package_info(&current->data);
        free(current);
        table->count--;

        upkg_util_log_verbose("Package '%s' removed from hash table.\n", name);

        // Check if we should shrink the table
        if (table->count > MIN_HASH_TABLE_SIZE && 
            (double)table->count / table->size < SHRINK_LOAD_FACTOR_THRESHOLD) {
            resize_hash_table(table, table->size / 2);
        }
    }
}

/**
 * @brief Destroys the hash table and frees all memory.
 * @param table A pointer to the hash table to destroy.
 */
void upkg_hash_destroy_table(upkg_hash_table_t *table) {
    if (!table) return;

    for (size_t i = 0; i < table->size; i++) {
        upkg_hash_node_t *current = table->buckets[i];
        while (current) {
            upkg_hash_node_t *temp = current;
            current = current->next;
            upkg_hash_free_package_info(&temp->data);
            free(temp);
        }
    }

    free(table->buckets);
    free(table);
    upkg_util_log_verbose("Hash table destroyed and memory freed.\n");
}

// --- Display Functions ---

/**
 * @brief Prints package information from the hash table.
 * @param pkg_info A pointer to the package info to print.
 */
void upkg_hash_print_package_info(const upkg_hash_package_info_t *pkg_info) {
    if (!pkg_info) {
        printf("No package information available in hash table.\n");
        return;
    }

    printf("Hash Table Package Information:\n");
    printf("==============================\n");

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
    if (pkg_info->filename) {
        printf("Filename:     %s\n", pkg_info->filename);
    }

    printf("\nHash Table File List (%d files):\n", pkg_info->file_count);
    if (pkg_info->file_count > 0 && pkg_info->file_list) {
        printf("================================\n");
        for (int i = 0; i < pkg_info->file_count; i++) {
            if (pkg_info->file_list[i]) {
                printf("  %s\n", pkg_info->file_list[i]);
            }
        }
    } else {
        printf("  (No files or empty package)\n");
    }
    printf("\n");
}

/**
 * @brief Lists all packages in the hash table.
 * @param table A pointer to the hash table.
 */
void upkg_hash_list_packages(upkg_hash_table_t *table) {
    if (!table) {
        printf("Hash table is NULL.\n");
        return;
    }

    printf("Packages in Hash Table:\n");
    printf("======================\n");
    
    int count = 0;
    for (size_t i = 0; i < table->size; i++) {
        upkg_hash_node_t *current = table->buckets[i];
        while (current) {
            if (current->data.package_name) {
                printf("%s\n", current->data.package_name);
                count++;
            }
            current = current->next;
        }
    }
    
    printf("\nTotal packages: %d\n", count);
}

/**
 * @brief Converts upkg_package_info_t to upkg_hash_package_info_t.
 * @param src Source package info from upkg_pack.
 * @param dst Destination package info for hash table.
 * @return 0 on success, -1 on failure.
 */
int upkg_hash_convert_package_info(const void *src, upkg_hash_package_info_t *dst) {
    if (!src || !dst) return -1;

    const upkg_package_info_t *pkg_info = (const upkg_package_info_t*)src;
    
    // Initialize destination
    memset(dst, 0, sizeof(upkg_hash_package_info_t));

    // Copy all fields
    dst->package_name = pkg_info->package_name ? strdup(pkg_info->package_name) : NULL;
    dst->version = pkg_info->version ? strdup(pkg_info->version) : NULL;
    dst->architecture = pkg_info->architecture ? strdup(pkg_info->architecture) : NULL;
    dst->maintainer = pkg_info->maintainer ? strdup(pkg_info->maintainer) : NULL;
    dst->description = pkg_info->description ? strdup(pkg_info->description) : NULL;
    dst->depends = pkg_info->depends ? strdup(pkg_info->depends) : NULL;
    dst->installed_size = pkg_info->installed_size ? strdup(pkg_info->installed_size) : NULL;
    dst->section = pkg_info->section ? strdup(pkg_info->section) : NULL;
    dst->priority = pkg_info->priority ? strdup(pkg_info->priority) : NULL;
    dst->homepage = pkg_info->homepage ? strdup(pkg_info->homepage) : NULL;
    dst->filename = pkg_info->filename ? strdup(pkg_info->filename) : NULL;

    // Copy file list
    if (pkg_info->file_list && pkg_info->file_count > 0) {
        dst->file_list = malloc(pkg_info->file_count * sizeof(char*));
        if (dst->file_list) {
            dst->file_count = pkg_info->file_count;
            for (int i = 0; i < pkg_info->file_count; i++) {
                dst->file_list[i] = pkg_info->file_list[i] ? strdup(pkg_info->file_list[i]) : NULL;
            }
        } else {
            dst->file_count = 0;
            return -1;
        }
    } else {
        dst->file_list = NULL;
        dst->file_count = 0;
    }

    return 0;
}
