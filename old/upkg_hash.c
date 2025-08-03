/******************************************************************************
 * Filename:    upkghash.c
 * Author:      <michkochris@gmail.com>
 * Date:        started 12-31-2024
 * Description: upkg manages linux .deb pkg's
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
/*file description: file for adding pkginfo into hash table for storage...*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdbool.h> // For true/false
#include <math.h>    // For sqrt in is_prime
#include <stdarg.h>  // For va_list in logging functions

#include "upkg_lib.h"
#include "upkg_hash.h"
#include "upkg_struct.h"
#include "upkg_script.h"
#include "upkg_exec.h"
#include "upkg_highlight.h"

// --- Global Variables ---
bool g_verbose_mode = false;
HashTable *upkg_main_hash_table = NULL;

// --- Logging Functions Implementations ---

/**
 * @brief Prints a verbose message to stdout if g_verbose_mode is true.
 * @param format The format string for the message.
 * @param ... Variable arguments for the format string.
 */
void upkg_log_verbose(const char *format, ...) {
    if (g_verbose_mode) {
        va_list args;
        va_start(args, format);
        vfprintf(stdout, format, args);
        va_end(args);
    }
}

/**
 * @brief Prints a debug message to stderr. This is always on.
 * @param format The format string for the message.
 * @param ... Variable arguments for the format string.
 */
void upkg_log_debug(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

// --- Prime Number Utility Functions ---

/**
 * @brief Checks if a given number is prime.
 * @param num The number to check.
 * @return True if num is prime, false otherwise.
 */
static bool is_prime(size_t num) {
    if (num <= 1) return false;
    if (num <= 3) return true;
    if (num % 2 == 0 || num % 3 == 0) return false;

    for (size_t i = 5; i * i <= num; i = i + 6) {
        if (num % i == 0 || num % (i + 2) == 0)
            return false;
    }
    return true;
}

/**
 * @brief Finds the next prime number greater than or equal to 'num'.
 * @param num The starting number to search from.
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

// --- Core Hash Table Functions Implementations ---

// FNV-1a (Fowler-Noll-Vo hash, variant 1a) for better distribution
static unsigned int hashFunction(const char *name, size_t table_size) {
    if (name == NULL || table_size == 0) {
        return 0;
    }

    const unsigned int FNV_PRIME_32 = 16777619U;
    const unsigned int FNV_OFFSET_BASIS_32 = 2166136261U;

    unsigned int hash = FNV_OFFSET_BASIS_32;

    for (const char *p = name; *p != '\0'; p++) {
        hash ^= (unsigned char)*p;
        hash *= FNV_PRIME_32;
    }
    return hash % table_size;
}

/**
 * @brief Creates and initializes a new hash table.
 * @param initial_size The desired initial size of the hash table.
 * @return A pointer to the new HashTable, or NULL on failure.
 */
HashTable* create_hash_table(size_t initial_size) {
    HashTable *new_table = (HashTable *)malloc(sizeof(HashTable));
    if (!new_table) {
        perror("Failed to allocate memory for HashTable struct");
        return NULL;
    }

    if (initial_size < MIN_HASH_TABLE_SIZE) {
        initial_size = MIN_HASH_TABLE_SIZE;
    }

    initial_size = find_next_prime(initial_size);

    new_table->buckets = (Node **)calloc(initial_size, sizeof(Node *));
    if (!new_table->buckets) {
        perror("Failed to allocate memory for hash table buckets");
        free(new_table);
        return NULL;
    }

    new_table->size = initial_size;
    new_table->count = 0;

    upkg_log_verbose("Hash table created with initial size %zu. Growth threshold %.2f\n",
            new_table->size, GROW_LOAD_FACTOR_THRESHOLD);
    return new_table;
}

/**
 * @brief Searches the hash table for a package and returns a pointer to its Pkginfo struct.
 * @param table A pointer to the HashTable.
 * @param name The name of the package to search for.
 * @return A pointer to the Pkginfo struct within the table, or NULL if not found.
 * IMPORTANT: The returned Pkginfo* points to data *inside* the hash table.
 * Do NOT free this pointer directly. Its memory is managed by the hash table.
 */
Pkginfo *search(HashTable *table, const char *name) {
    if (!table || name == NULL || name[0] == '\0') {
        return NULL;
    }
    unsigned int index = hashFunction(name, table->size);
    Node* current = table->buckets[index];
    while (current != NULL) {
        if (current->data.pkgname[0] != '\0' &&
            strcmp(current->data.pkgname, name) == 0) {
            return &(current->data);
        }
        current = current->next;
    }
    return NULL;
}

/**
 * @brief Resizes the hash table to a new_size, rehashing all entries.
 * @param table A pointer to the HashTable.
 * @param new_size The new size for the hash table.
 * @return 0 on success, -1 on failure.
 */
static int resize_hash_table(HashTable *table, size_t new_size) {
    if (!table) return -1;

    size_t old_size = table->size;
    size_t old_count = table->count;

    if (new_size < MIN_HASH_TABLE_SIZE) {
        new_size = MIN_HASH_TABLE_SIZE;
    }
    new_size = find_next_prime(new_size);

    if (new_size == old_size || new_size == 0) {
        return 0;
    }

    Node **new_buckets = (Node **)calloc(new_size, sizeof(Node *));
    if (!new_buckets) {
        perror("Failed to allocate memory for new hash table buckets during resize");
        return -1;
    }

    upkg_log_verbose("Hash table resizing from %zu buckets to %zu buckets. Re-copying %zu packages...\n",
            old_size, new_size, old_count);

    Node **old_buckets = table->buckets;
    table->buckets = new_buckets;
    table->size = new_size;
    table->count = 0;

    size_t copied_count = 0;
    int progress_interval_percent = 10;
    size_t next_progress_threshold = 0;
    if (old_count > 0) {
        next_progress_threshold = (old_count * progress_interval_percent) / 100;
        if (next_progress_threshold == 0) next_progress_threshold = 1;
    }

    for (size_t i = 0; i < old_size; i++) {
        Node *current = old_buckets[i];
        while (current != NULL) {
            Node *next_node = current->next;

            unsigned int new_index = hashFunction(current->data.pkgname, table->size);

            current->next = table->buckets[new_index];
            table->buckets[new_index] = current;
            table->count++;
            copied_count++;

            if (old_count > 0 && copied_count >= next_progress_threshold) {
                upkg_log_verbose("%zu of %zu packages re-copied (%.1f%%)...\n",
                        copied_count, old_count, ((double)copied_count / old_count) * 100.0);
                next_progress_threshold += (old_count * progress_interval_percent) / 100;
                if (next_progress_threshold == copied_count) next_progress_threshold++;
                if (next_progress_threshold > old_count) next_progress_threshold = old_count;
            }

            current = next_node;
        }
    }

    if (old_count > 0 && copied_count == old_count) {
        upkg_log_verbose("%zu of %zu packages re-copied (100.0%%). Complete.\n", old_count, old_count);
    } else if (old_count > 0 && copied_count < old_count) {
        upkg_log_verbose("Re-copying finished. Final count: %zu of %zu.\n", copied_count, old_count);
    }

    free(old_buckets);

    upkg_log_verbose("Hash table resizing complete. New size: %zu, Final count: %zu\n",
            table->size, table->count);
    return 0;
}


/**
 * @brief Adds a package to the hash table by performing a deep copy of all fields.
 *
 * This is the primary function for adding packages to the in-memory database.
 * It handles resizing the hash table if the load factor is exceeded.
 *
 * @param table A pointer to the HashTable.
 * @param source_info A pointer to the temporary Pkginfo struct to be copied.
 * @return 0 on success, -1 on failure. On success, the dynamic members of the source_info are freed.
 */
int add_package_deep_copy(HashTable *table, Pkginfo *source_info) {
    if (!table || source_info == NULL || source_info->pkgname[0] == '\0') {
        upkg_log_debug("Error: add_package_deep_copy received invalid table or source_info.\n");
        if (source_info) free_pkginfo_members(source_info);
        return -1;
    }

    Pkginfo *existing_pkg = search(table, source_info->pkgname);
    if (existing_pkg != NULL) {
        upkg_log_verbose("Warning: Package '%s' already exists in hash table. Updating info.\n", source_info->pkgname);
        free_pkginfo_members(existing_pkg);

        strncpy(existing_pkg->pkgname, source_info->pkgname, PKGNAME_SIZE - 1); existing_pkg->pkgname[PKGNAME_SIZE - 1] = '\0';
        strncpy(existing_pkg->version, source_info->version, VERSION_SIZE - 1); existing_pkg->version[VERSION_SIZE - 1] = '\0';
        strncpy(existing_pkg->arch, source_info->arch, ARCH_SIZE - 1); existing_pkg->arch[ARCH_SIZE - 1] = '\0';
        strncpy(existing_pkg->maintainer, source_info->maintainer, MAINTAINER_SIZE - 1); existing_pkg->maintainer[MAINTAINER_SIZE - 1] = '\0';
        strncpy(existing_pkg->homepage, source_info->homepage, HOMEPAGE_SIZE - 1); existing_pkg->homepage[HOMEPAGE_SIZE - 1] = '\0';
        strncpy(existing_pkg->sources, source_info->sources, SOURCES_SIZE - 1); existing_pkg->sources[SOURCES_SIZE - 1] = '\0';
        strncpy(existing_pkg->section, source_info->section, SECTION_SIZE - 1); existing_pkg->section[SECTION_SIZE - 1] = '\0';
        strncpy(existing_pkg->priority, source_info->priority, PRIORITY_SIZE - 1); existing_pkg->priority[PRIORITY_SIZE - 1] = '\0';
        strncpy(existing_pkg->depends, source_info->depends, DEPENDS_SIZE - 1); existing_pkg->depends[DEPENDS_SIZE - 1] = '\0';
        strncpy(existing_pkg->comment, source_info->comment, COMMENT_SIZE - 1); existing_pkg->comment[COMMENT_SIZE - 1] = '\0';
        strncpy(existing_pkg->description, source_info->description, DESCRIPTION_SIZE - 1); existing_pkg->description[DESCRIPTION_SIZE - 1] = '\0';

        existing_pkg->preinst = (source_info->preinst && source_info->preinst_len > 0) ? strdup(source_info->preinst) : NULL;
        existing_pkg->preinst_len = source_info->preinst_len;
        existing_pkg->postinst = (source_info->postinst && source_info->postinst_len > 0) ? strdup(source_info->postinst) : NULL;
        existing_pkg->postinst_len = source_info->postinst_len;
        existing_pkg->prerm = (source_info->prerm && source_info->prerm_len > 0) ? strdup(source_info->prerm) : NULL;
        existing_pkg->prerm_len = source_info->prerm_len;
        existing_pkg->postrm = (source_info->postrm && source_info->postrm_len > 0) ? strdup(source_info->postrm) : NULL;
        existing_pkg->postrm_len = source_info->postrm_len;
        existing_pkg->buildscript = (source_info->buildscript && source_info->buildscript_len > 0) ? strdup(source_info->buildscript) : NULL;
        existing_pkg->buildscript_len = source_info->buildscript_len;

        if (source_info->file_list && source_info->file_count > 0) {
            existing_pkg->file_count = source_info->file_count;
            existing_pkg->file_list = (char**)malloc(existing_pkg->file_count * sizeof(char*));
            if (!existing_pkg->file_list) {
                upkg_log_debug("Error: Memory allocation for existing package file_list failed during update for '%s'.\n", source_info->pkgname);
                for (int j = 0; j < existing_pkg->file_count; ++j) free_and_null(&existing_pkg->file_list[j]);
                free_and_null((char**)&existing_pkg->file_list);
                existing_pkg->file_count = 0;
                free_pkginfo_members(source_info);
                return -1;
            }
            for (int i = 0; i < existing_pkg->file_count; ++i) {
                if (source_info->file_list[i]) {
                    existing_pkg->file_list[i] = strdup(source_info->file_list[i]);
                    if (!existing_pkg->file_list[i]) {
                        upkg_log_debug("Error: strdup failed for file_list entry %d during update for '%s'.\n", i, source_info->pkgname);
                        for (int j = 0; j < i; ++j) free_and_null(&existing_pkg->file_list[j]);
                        free_and_null((char**)&existing_pkg->file_list);
                        existing_pkg->file_count = 0;
                        free_pkginfo_members(source_info);
                        return -1;
                    }
                } else {
                    existing_pkg->file_list[i] = NULL;
                }
            }
        } else {
            existing_pkg->file_list = NULL;
            existing_pkg->file_count = 0;
        }

        free_pkginfo_members(source_info);
        return 0;
    }

    if (((double)(table->count + 1) / table->size) > GROW_LOAD_FACTOR_THRESHOLD) {
        upkg_log_verbose("Load factor %.2f exceeded (current: %.2f). Resizing.\n",
                GROW_LOAD_FACTOR_THRESHOLD, (double)table->count / table->size);
        if (resize_hash_table(table, table->size * 2) != 0) {
            upkg_log_debug("Error: Failed to resize hash table during add_package_deep_copy for '%s'.\n", source_info->pkgname);
            free_pkginfo_members(source_info);
            return -1;
        }
    }

    unsigned int index = hashFunction(source_info->pkgname, table->size);

    Node* newNode = (Node*)malloc(sizeof(Node));
    if (!newNode) {
        upkg_log_debug("Error: add_package_deep_copy: Memory allocation failed for new Node.\n");
        free_pkginfo_members(source_info);
        return -1;
    }
    memset(&(newNode->data), 0, sizeof(Pkginfo));

    strncpy(newNode->data.pkgname, source_info->pkgname, PKGNAME_SIZE - 1);
    newNode->data.pkgname[PKGNAME_SIZE - 1] = '\0';
    strncpy(newNode->data.version, source_info->version, VERSION_SIZE - 1);
    newNode->data.version[VERSION_SIZE - 1] = '\0';
    strncpy(newNode->data.arch, source_info->arch, ARCH_SIZE - 1);
    newNode->data.arch[ARCH_SIZE - 1] = '\0';
    strncpy(newNode->data.maintainer, source_info->maintainer, MAINTAINER_SIZE - 1);
    newNode->data.maintainer[MAINTAINER_SIZE - 1] = '\0';
    strncpy(newNode->data.homepage, source_info->homepage, HOMEPAGE_SIZE - 1);
    newNode->data.homepage[HOMEPAGE_SIZE - 1] = '\0';
    strncpy(newNode->data.sources, source_info->sources, SOURCES_SIZE - 1);
    newNode->data.sources[SOURCES_SIZE - 1] = '\0';
    strncpy(newNode->data.section, source_info->section, SECTION_SIZE - 1);
    newNode->data.section[SECTION_SIZE - 1] = '\0';
    strncpy(newNode->data.priority, source_info->priority, PRIORITY_SIZE - 1);
    newNode->data.priority[PRIORITY_SIZE - 1] = '\0';
    strncpy(newNode->data.depends, source_info->depends, DEPENDS_SIZE - 1);
    newNode->data.depends[DEPENDS_SIZE - 1] = '\0';
    strncpy(newNode->data.comment, source_info->comment, COMMENT_SIZE - 1);
    newNode->data.comment[COMMENT_SIZE - 1] = '\0';
    strncpy(newNode->data.description, source_info->description, DESCRIPTION_SIZE - 1);
    newNode->data.description[DESCRIPTION_SIZE - 1] = '\0';

    newNode->data.preinst = (source_info->preinst && source_info->preinst_len > 0) ? strdup(source_info->preinst) : NULL;
    newNode->data.preinst_len = source_info->preinst_len;
    newNode->data.postinst = (source_info->postinst && source_info->postinst_len > 0) ? strdup(source_info->postinst) : NULL;
    newNode->data.postinst_len = source_info->postinst_len;
    newNode->data.prerm = (source_info->prerm && source_info->prerm_len > 0) ? strdup(source_info->prerm) : NULL;
    newNode->data.prerm_len = source_info->prerm_len;
    newNode->data.postrm = (source_info->postrm && source_info->postrm_len > 0) ? strdup(source_info->postrm) : NULL;
    newNode->data.postrm_len = source_info->postrm_len;
    newNode->data.buildscript = (source_info->buildscript && source_info->buildscript_len > 0) ? strdup(source_info->buildscript) : NULL;
    newNode->data.buildscript_len = source_info->buildscript_len;

    bool script_copy_failed = false;
    if ((source_info->preinst && source_info->preinst_len > 0 && !newNode->data.preinst) ||
        (source_info->postinst && source_info->postinst_len > 0 && !newNode->data.postinst) ||
        (source_info->prerm && source_info->prerm_len > 0 && !newNode->data.prerm) ||
        (source_info->postrm && source_info->postrm_len > 0 && !newNode->data.postrm) ||
        (source_info->buildscript && source_info->buildscript_len > 0 && !newNode->data.buildscript)) {
        script_copy_failed = true;
    }

    if (script_copy_failed) {
        upkg_log_debug("Error: add_package_deep_copy: strdup failed for a script for '%s'.\n", source_info->pkgname);
        free_pkginfo_members(&(newNode->data));
        free(newNode);
        free_pkginfo_members(source_info);
        return -1;
    }

    if (source_info->file_list && source_info->file_count > 0) {
        newNode->data.file_count = source_info->file_count;
        newNode->data.file_list = (char**)malloc(newNode->data.file_count * sizeof(char*));
        if (!newNode->data.file_list) {
            upkg_log_debug("Error: add_package_deep_copy: Memory allocation failed for file_list array for '%s'.\n", source_info->pkgname);
            free_pkginfo_members(&(newNode->data));
            free(newNode);
            free_pkginfo_members(source_info);
            return -1;
        }
        for (int i = 0; i < newNode->data.file_count; ++i) {
            if (source_info->file_list[i]) {
                newNode->data.file_list[i] = strdup(source_info->file_list[i]);
                if (!newNode->data.file_list[i]) {
                    upkg_log_debug("Error: add_package_deep_copy: strdup failed for file_list entry %d for '%s'.\n", i, source_info->pkgname);
                    for (int j = 0; j < i; ++j) free_and_null(&newNode->data.file_list[j]);
                    free_and_null((char**)&newNode->data.file_list);
                    newNode->data.file_count = 0;
                    free_pkginfo_members(&(newNode->data));
                    free(newNode);
                    free_pkginfo_members(source_info);
                    return -1;
                }
            } else {
                newNode->data.file_list[i] = NULL;
            }
        }
    } else {
        newNode->data.file_list = NULL;
        newNode->data.file_count = 0;
    }

    newNode->next = table->buckets[index];
    table->buckets[index] = newNode;
    table->count++;

    upkg_log_verbose("Package '%s' successfully added to hash table (deep copy).\n", source_info->pkgname);
    free_pkginfo_members(source_info);

    return 0;
}

/**
 * @brief Removes a package from the hash table.
 *
 * This function also handles shrinking the hash table if the load factor
 * drops below the shrink threshold.
 *
 * @param table A pointer to the HashTable.
 * @param name The name of the package to remove.
 */
void removepkg(HashTable *table, const char *name) {
    if (!table || name == NULL || name[0] == '\0') {
        return;
    }
    unsigned int index = hashFunction(name, table->size);
    Node* current = table->buckets[index];
    Node* prev = NULL;
    while (current != NULL && strcmp(current->data.pkgname, name) != 0) {
        prev = current;
        current = current->next;
    }
    if (current != NULL) {
        if (prev == NULL) {
            table->buckets[index] = current->next;
        } else {
            prev->next = current->next;
        }
        free_pkginfo_members(&(current->data));
        free(current);
        table->count--;
        upkg_log_verbose("Package '%s' removed and its memory freed.\n", name);

        if (table->count > MIN_HASH_TABLE_SIZE && (double)table->count / table->size < SHRINK_LOAD_FACTOR_THRESHOLD) {
            upkg_log_verbose("Load factor %.2f below threshold (current: %.2f). Shrinking.\n",
                    SHRINK_LOAD_FACTOR_THRESHOLD, (double)table->count / table->size);
            resize_hash_table(table, table->size / 2);
        }

    } else {
        upkg_log_debug("Package '%s' not found for removal.\n", name);
    }
}

/**
 * @brief Destroys the hash table and frees all associated memory.
 * @param table A pointer to the HashTable to be destroyed.
 */
void destroy_hash_table(HashTable *table) {
    if (!table) return;

    for (size_t i = 0; i < table->size; ++i) {
        Node *current = table->buckets[i];
        while (current != NULL) {
            Node *temp = current;
            current = current->next;
            free_pkginfo_members(&(temp->data));
            free(temp);
        }
        table->buckets[i] = NULL;
    }
    free(table->buckets);
    free(table);
    upkg_main_hash_table = NULL;
    upkg_log_verbose("Hash table and all package data freed.\n");
}


// --- Utility/Display Functions Implementations ---

/**
 * @brief Prints the names of all packages in the hash table separated by spaces.
 * @param table A pointer to the HashTable.
 */
void glob(HashTable *table) {
    if (!table) return;
    for (size_t i = 0; i < table->size; i++) {
        Node *current = table->buckets[i];
        while (current != NULL) {
            if (current->data.pkgname[0] != '\0') {
                printf("%s ", current->data.pkgname);
            }
            current = current->next;
        }
    }
    printf("\n");
}

/**
 * @brief Prints the names of all packages in the hash table, each on a new line.
 * @param table A pointer to the HashTable.
 */
void list(HashTable *table) {
    if (!table) return;
    for (size_t i = 0; i < table->size; i++) {
        Node *current = table->buckets[i];
        while (current != NULL) {
            if (current->data.pkgname[0] != '\0') {
                printf("%s\n", current->data.pkgname);
            }
            current = current->next;
        }
    }
}

/**
 * @brief Prints a detailed view of the hash table's internal state.
 * @param table A pointer to the HashTable.
 */
void print_hash_table(HashTable *table) {
    if (!table) {
        printf("Hash table is NULL.\n");
        return;
    }
    printf("--- Hash Table Status ---\n");
    printf("Size: %zu, Count: %zu, Load Factor: %.2f (Grow Threshold: %.2f, Shrink Threshold: %.2f)\n",
           table->size, table->count, (double)table->count / table->size, GROW_LOAD_FACTOR_THRESHOLD, SHRINK_LOAD_FACTOR_THRESHOLD);
    printf("-------------------------\n");

    for (size_t i = 0; i < table->size; i++) {
        printf("Index %zu: ", i);
        Node *current = table->buckets[i];
        while (current != NULL) {
            if (current->data.pkgname[0] != '\0') {
                printf("(%s, ver:%s, arch:%s) -> ", current->data.pkgname, current->data.version, current->data.arch);
            }
            current = current->next;
        }
        printf("NULL\n");
    }
    printf("-------------------------\n");
}

/**
 * @brief Generates an array of package name suggestions based on a prefix.
 * @param table A pointer to the HashTable.
 * @param name The prefix to match.
 * @return A dynamically allocated, NULL-terminated array of matching package names.
 * The caller is responsible for freeing the array and its contents.
 */
char **suggestions(HashTable *table, const char *name) {
    if (!table || name == NULL) return NULL;
    char **suggestions_list = (char **)malloc((MAX_SUGGESTIONS + 1) * sizeof(char *));
    if (!suggestions_list) return NULL;
    int count = 0;
    size_t name_len = strlen(name);

    for (size_t i = 0; i < table->size; i++) {
        Node *current = table->buckets[i];
        while (current != NULL && count < MAX_SUGGESTIONS) {
            if (current->data.pkgname[0] != '\0' &&
                strncmp(name, current->data.pkgname, name_len) == 0) {
                suggestions_list[count++] = strdup(current->data.pkgname);
            }
            current = current->next;
        }
        if (count >= MAX_SUGGESTIONS) break;
    }
    suggestions_list[count] = NULL;
    return suggestions_list;
}

/**
 * @brief Prints a formatted list of package name suggestions.
 * @param table A pointer to the HashTable.
 * @param prefix The prefix to use for suggestions.
 */
void print_suggestions(HashTable *table, const char *prefix) {
    if (!table || prefix == NULL || prefix[0] == '\0') return;
    char **suggs = suggestions(table, prefix);
    if (suggs) {
        if (suggs[0] == NULL) {
            printf("No suggestions found for '%s'.\n", prefix);
        } else {
            printf("Did you mean:\n");
            for (int i = 0; suggs[i] != NULL; ++i) {
                printf("  - %s\n", suggs[i]);
                free_and_null(&suggs[i]);
            }
        }
        free_and_null((char**)&suggs);
    } else {
        fprintf(stderr, "Error: Could not generate suggestions.\n");
    }
}

/**
 * @brief Searches for a package and prints its detailed status.
 *
 * If the package is not found, it prints suggestions for similar package names.
 *
 * @param table A pointer to the HashTable.
 * @param name The name of the package to query.
 */
void status_search(HashTable *table, const char *name) {
    if (!table || name == NULL) {
        printf("Invalid package name or table.\n");
        return;
    }
    Pkginfo *found = search(table, name);

    if (found != NULL && strlen(found->pkgname) > 0) {
        printf("\nPackage: %s\n", found->pkgname);
        if (strlen(found->version) > 0) {
            printf("Version: %s\n", found->version);
        }
        if (strlen(found->arch) > 0) {
            printf("Architecture: %s\n", found->arch);
        }
        if (strlen(found->maintainer) > 0) {
            printf("Maintainer: %s\n", found->maintainer);
        }
        if (strlen(found->homepage) > 0) {
            printf("Homepage: %s\n", found->homepage);
        }
        if (strlen(found->sources) > 0) {
            printf("Source: %s\n", found->sources);
        }
        if (strlen(found->section) > 0) {
            printf("Section: %s\n", found->section);
        }
        if (strlen(found->priority) > 0) {
            printf("Priority: %s\n", found->priority);
        }
        if (strlen(found->depends) > 0) {
            printf("Depends: %s\n", found->depends);
        }
        if (strlen(found->comment) > 0) {
            printf("Comment: %s\n", found->comment);
        }
        if (strlen(found->description) > 0) {
            printf("Description: %s\n", found->description);
        }

        if (found->preinst) {
            printf("Pre-install script length: %zu\n", found->preinst_len);
        }
        if (found->postinst) {
            printf("Post-install script length: %zu\n", found->postinst_len);
        }
        if (found->file_list && found->file_count > 0) {
            printf("Contains %d files.\n", found->file_count);
        }

    } else {
        printf("Package '%s' is not installed.\n", name);
        print_suggestions(table, name);
    }
}
