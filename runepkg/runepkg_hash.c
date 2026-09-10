/*****************************************************************************
 * Filename:    runepkg_hash.c
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Hash table implementation for runepkg package index
 * LICENSE:     GPL v3
 ***************************************************************************/

#include "runepkg_portable.h"
#include "runepkg_hash.h"
#include "runepkg_util.h"
#include "runepkg_pack.h"
#include "runepkg_defensive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Global Variables --- */
runepkg_hash_table_t *runepkg_main_hash_table = NULL;

/* --- Utility Functions --- */

/**
 * @brief Checks if a number is prime.
 */
static bool is_prime(size_t num) {
    size_t i;
    if (num <= 1) return false;
    if (num <= 3) return true;
    if (num % 2 == 0 || num % 3 == 0) return false;

    for (i = 5; i * i <= num; i += 6) {
        if (num % i == 0 || num % (i + 2) == 0)
            return false;
    }
    return true;
}

/**
 * @brief Finds the next prime number greater than or equal to num.
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
 */
static unsigned int hash_function(const char *name, size_t table_size) {
    const unsigned int FNV_PRIME_32 = 16777619U;
    const unsigned int FNV_OFFSET_BASIS_32 = 2166136261U;
    unsigned int hash = FNV_OFFSET_BASIS_32;
    const char *p;

    if (!name || table_size == 0) return 0;

    for (p = name; *p != '\0'; p++) {
        hash ^= (unsigned char)*p;
        hash *= FNV_PRIME_32;
    }
    return hash % table_size;
}

/* --- Memory Management Functions --- */

void runepkg_hash_free_package_info(PkgInfo *pkg_info) {
    if (!pkg_info) return;

    runepkg_util_free_and_null(&pkg_info->package_name);
    runepkg_util_free_and_null(&pkg_info->version);
    runepkg_util_free_and_null(&pkg_info->architecture);
    runepkg_util_free_and_null(&pkg_info->maintainer);
    runepkg_util_free_and_null(&pkg_info->description);
    runepkg_util_free_and_null(&pkg_info->depends);
    runepkg_util_free_and_null(&pkg_info->pre_depends);
    runepkg_util_free_and_null(&pkg_info->provides);
    runepkg_util_free_and_null(&pkg_info->build_depends);
    runepkg_util_free_and_null(&pkg_info->build_depends_indep);
    runepkg_util_free_and_null(&pkg_info->build_depends_arch);
    runepkg_util_free_and_null(&pkg_info->conflicts);
    runepkg_util_free_and_null(&pkg_info->breaks);
    runepkg_util_free_and_null(&pkg_info->recommends);
    runepkg_util_free_and_null(&pkg_info->suggests);
    runepkg_util_free_and_null(&pkg_info->installed_size);
    runepkg_util_free_and_null(&pkg_info->section);
    runepkg_util_free_and_null(&pkg_info->priority);
    runepkg_util_free_and_null(&pkg_info->homepage);
    runepkg_util_free_and_null(&pkg_info->filename);
    runepkg_util_free_and_null(&pkg_info->multi_arch);
    runepkg_util_free_and_null(&pkg_info->source_name);
    runepkg_util_free_and_null(&pkg_info->preinst);
    runepkg_util_free_and_null(&pkg_info->postinst);
    runepkg_util_free_and_null(&pkg_info->prerm);
    runepkg_util_free_and_null(&pkg_info->postrm);
    runepkg_util_free_and_null(&pkg_info->control_dir_path);
    runepkg_util_free_and_null(&pkg_info->data_dir_path);
    runepkg_util_free_and_null(&pkg_info->extraction_workspace_path);

    if (pkg_info->file_list) {
        int i;
        for (i = 0; i < pkg_info->file_count; i++) {
            runepkg_util_free_and_null(&pkg_info->file_list[i]);
        }
        free(pkg_info->file_list);
        pkg_info->file_list = NULL;
    }
    pkg_info->file_count = 0;
}

/* --- Hash Table Core Functions --- */

runepkg_hash_table_t* runepkg_hash_create_table(size_t initial_size) {
    runepkg_hash_table_t *table;
    runepkg_error_t err = runepkg_validate_size(initial_size, 1000000, "hash table size");
    if (err != RUNEPKG_SUCCESS) {
        runepkg_util_error("Invalid hash table size: %s\n", runepkg_error_string(err));
        return NULL;
    }

    table = runepkg_secure_malloc(sizeof(runepkg_hash_table_t));
    if (!table) {
        runepkg_util_error("Failed to allocate memory for hash table structure.\n");
        return NULL;
    }

    if (initial_size < MIN_HASH_TABLE_SIZE) {
        initial_size = MIN_HASH_TABLE_SIZE;
    }
    initial_size = find_next_prime(initial_size);

    table->buckets = runepkg_secure_calloc(initial_size, sizeof(runepkg_hash_node_t*));
    if (!table->buckets) {
        runepkg_util_error("Failed to allocate memory for hash table buckets.\n");
        runepkg_secure_free((void**)&table, sizeof(runepkg_hash_table_t));
        return NULL;
    }

    table->provides_buckets = runepkg_secure_calloc(initial_size, sizeof(runepkg_provides_node_t*));
    if (!table->provides_buckets) {
        runepkg_secure_free((void**)&table->buckets, initial_size * sizeof(runepkg_hash_node_t*));
        runepkg_secure_free((void**)&table, sizeof(runepkg_hash_table_t));
        return NULL;
    }

    table->size = initial_size;
    table->count = 0;
    table->provides_size = initial_size;

    return table;
}

PkgInfo* runepkg_hash_search(runepkg_hash_table_t *table, const char *name) {
    unsigned int index;
    runepkg_hash_node_t *current;

    if (!table || !name || name[0] == '\0') return NULL;

    index = hash_function(name, table->size);
    current = table->buckets[index];

    while (current) {
        if (current->data.package_name && strcmp(current->data.package_name, name) == 0) {
            return &current->data;
        }
        current = current->next;
    }
    return NULL;
}

static void add_to_provides_map(runepkg_hash_table_t *table, runepkg_hash_node_t *node) {
    char *provides_copy;
    char *token;

    if (!node->data.provides) return;
    provides_copy = strdup(node->data.provides);
    token = strtok(provides_copy, ",");
    while (token) {
        char *trimmed = runepkg_util_trim_whitespace(token);
        size_t name_len = strcspn(trimmed, " (");
        char *vname = runepkg_secure_strndup(trimmed, name_len);
        unsigned int index = hash_function(vname, table->provides_size);
        runepkg_provides_node_t *pnode = runepkg_secure_malloc(sizeof(runepkg_provides_node_t));
        pnode->virtual_name = vname;
        pnode->provider = node;
        pnode->next = table->provides_buckets[index];
        table->provides_buckets[index] = pnode;
        token = strtok(NULL, ",");
    }
    free(provides_copy);
}

static void remove_from_provides_map(runepkg_hash_table_t *table, runepkg_hash_node_t *node) {
    char *provides_copy;
    char *token;

    if (!node->data.provides) return;
    provides_copy = strdup(node->data.provides);
    token = strtok(provides_copy, ",");
    while (token) {
        char *trimmed = runepkg_util_trim_whitespace(token);
        size_t name_len = strcspn(trimmed, " (");
        char *vname = runepkg_secure_strndup(trimmed, name_len);
        unsigned int index = hash_function(vname, table->provides_size);
        runepkg_provides_node_t *curr = table->provides_buckets[index];
        runepkg_provides_node_t *prev = NULL;
        while (curr) {
            if (strcmp(curr->virtual_name, vname) == 0 && curr->provider == node) {
                if (prev) prev->next = curr->next;
                else table->provides_buckets[index] = curr->next;
                free(curr->virtual_name); free(curr);
                break;
            }
            prev = curr; curr = curr->next;
        }
        free(vname); token = strtok(NULL, ",");
    }
    free(provides_copy);
}

static int resize_hash_table(runepkg_hash_table_t *table, size_t new_size);

static void cleanup_dummy_provides(runepkg_hash_table_t *table, const char *provider_name, const char *provides_str) {
    char *provides_copy;
    char *token;

    if (!provides_str || !provider_name) return;
    provides_copy = strdup(provides_str);
    token = strtok(provides_copy, ",");
    while (token) {
        char *trimmed;
        size_t name_len;
        char *vname;

        trimmed = runepkg_util_trim_whitespace(token);
        name_len = strcspn(trimmed, " (");
        vname = runepkg_secure_strndup(trimmed, name_len);

        if (vname && vname[0] != '\0') {
            PkgInfo *info;
            info = runepkg_hash_search(table, vname);
            /* Only remove if it's a dummy provided by this specific package */
            if (info && info->source_name && strcmp(info->source_name, provider_name) == 0) {
                runepkg_hash_remove_package(table, vname);
            }
        }
        if (vname) free(vname);
        token = strtok(NULL, ",");
    }
    free(provides_copy);
}

static void inject_dummy_provides(runepkg_hash_table_t *table, const PkgInfo *pkg_info) {
    char *provides_copy;
    char *token;

    if (!pkg_info || !pkg_info->provides) return;
    provides_copy = strdup(pkg_info->provides);
    token = strtok(provides_copy, ",");
    while (token) {
        char *trimmed = runepkg_util_trim_whitespace(token);
        size_t name_len = strcspn(trimmed, " (");
        char *vname = runepkg_secure_strndup(trimmed, name_len);

        /* Only inject if it doesn't exist as a real package or dummy already */
        if (vname && vname[0] != '\0' && !runepkg_hash_search(table, vname)) {
            PkgInfo dummy;
            char desc_buf[256];

            runepkg_pack_init_package_info(&dummy);
            dummy.package_name = strdup(vname);
            dummy.version = pkg_info->version ? strdup(pkg_info->version) : NULL;

            /* Record the actual provider so status/search can show it */
            dummy.source_name = strdup(pkg_info->package_name);

            snprintf(desc_buf, sizeof(desc_buf), "Virtual package provided by %s", pkg_info->package_name);
            dummy.description = strdup(desc_buf);

            dummy.provides = NULL; /* Avoid recursion */

            runepkg_hash_add_package(table, &dummy);
            runepkg_hash_free_package_info(&dummy);
        }
        if (vname) free(vname);
        token = strtok(NULL, ",");
    }
    free(provides_copy);
}

int runepkg_hash_add_package(runepkg_hash_table_t *table, const PkgInfo *pkg_info) {
    runepkg_hash_node_t *new_node;
    unsigned int index;

    if (!table || !pkg_info || !pkg_info->package_name) {
        runepkg_util_error("Invalid parameters for hash table add operation.\n");
        return -1;
    }

    index = hash_function(pkg_info->package_name, table->size);
    {
        runepkg_hash_node_t *curr = table->buckets[index];
        while (curr) {
            if (curr->data.package_name && strcmp(curr->data.package_name, pkg_info->package_name) == 0) {
                runepkg_util_log_verbose("Package '%s' already exists in hash table, updating.\n", pkg_info->package_name);

                remove_from_provides_map(table, curr);
                runepkg_hash_free_package_info(&curr->data);

                curr->data.package_name = pkg_info->package_name ? runepkg_secure_strdup(pkg_info->package_name) : NULL;
                curr->data.version = pkg_info->version ? runepkg_secure_strdup(pkg_info->version) : NULL;
                curr->data.architecture = pkg_info->architecture ? runepkg_secure_strdup(pkg_info->architecture) : NULL;
                curr->data.maintainer = pkg_info->maintainer ? runepkg_secure_strdup(pkg_info->maintainer) : NULL;
                curr->data.description = pkg_info->description ? runepkg_secure_strdup(pkg_info->description) : NULL;
                curr->data.depends = pkg_info->depends ? runepkg_secure_strdup(pkg_info->depends) : NULL;
                curr->data.pre_depends = pkg_info->pre_depends ? runepkg_secure_strdup(pkg_info->pre_depends) : NULL;
                curr->data.provides = pkg_info->provides ? runepkg_secure_strdup(pkg_info->provides) : NULL;
                curr->data.build_depends = pkg_info->build_depends ? runepkg_secure_strdup(pkg_info->build_depends) : NULL;
                curr->data.build_depends_indep = pkg_info->build_depends_indep ? runepkg_secure_strdup(pkg_info->build_depends_indep) : NULL;
                curr->data.build_depends_arch = pkg_info->build_depends_arch ? runepkg_secure_strdup(pkg_info->build_depends_arch) : NULL;
                curr->data.conflicts = pkg_info->conflicts ? runepkg_secure_strdup(pkg_info->conflicts) : NULL;
                curr->data.breaks = pkg_info->breaks ? runepkg_secure_strdup(pkg_info->breaks) : NULL;
                curr->data.recommends = pkg_info->recommends ? runepkg_secure_strdup(pkg_info->recommends) : NULL;
                curr->data.suggests = pkg_info->suggests ? runepkg_secure_strdup(pkg_info->suggests) : NULL;
                curr->data.installed_size = pkg_info->installed_size ? runepkg_secure_strdup(pkg_info->installed_size) : NULL;
                curr->data.section = pkg_info->section ? runepkg_secure_strdup(pkg_info->section) : NULL;
                curr->data.priority = pkg_info->priority ? runepkg_secure_strdup(pkg_info->priority) : NULL;
                curr->data.homepage = pkg_info->homepage ? runepkg_secure_strdup(pkg_info->homepage) : NULL;
                curr->data.filename = pkg_info->filename ? runepkg_secure_strdup(pkg_info->filename) : NULL;
                curr->data.multi_arch = pkg_info->multi_arch ? runepkg_secure_strdup(pkg_info->multi_arch) : NULL;
                curr->data.source_name = pkg_info->source_name ? runepkg_secure_strdup(pkg_info->source_name) : NULL;
                curr->data.preinst = pkg_info->preinst ? runepkg_secure_strdup(pkg_info->preinst) : NULL;
                curr->data.postinst = pkg_info->postinst ? runepkg_secure_strdup(pkg_info->postinst) : NULL;
                curr->data.prerm = pkg_info->prerm ? runepkg_secure_strdup(pkg_info->prerm) : NULL;
                curr->data.postrm = pkg_info->postrm ? runepkg_secure_strdup(pkg_info->postrm) : NULL;
                curr->data.md5_verified = pkg_info->md5_verified;
                curr->data.control_dir_path = pkg_info->control_dir_path ? runepkg_secure_strdup(pkg_info->control_dir_path) : NULL;
                curr->data.data_dir_path = pkg_info->data_dir_path ? runepkg_secure_strdup(pkg_info->data_dir_path) : NULL;
                curr->data.extraction_workspace_path = pkg_info->extraction_workspace_path ? runepkg_secure_strdup(pkg_info->extraction_workspace_path) : NULL;

                if (pkg_info->file_list && pkg_info->file_count > 0) {
                    runepkg_error_t err = runepkg_validate_file_count(pkg_info->file_count);
                    if (err != RUNEPKG_SUCCESS) {
                        runepkg_util_error("Invalid file count: %s\n", runepkg_error_string(err));
                        curr->data.file_list = NULL;
                        curr->data.file_count = 0;
                    } else {
                        curr->data.file_list = runepkg_secure_malloc(pkg_info->file_count * sizeof(char*));
                        if (curr->data.file_list) {
                            int i;
                            curr->data.file_count = pkg_info->file_count;
                            for (i = 0; i < pkg_info->file_count; i++) {
                                curr->data.file_list[i] = pkg_info->file_list[i] ? runepkg_secure_strdup(pkg_info->file_list[i]) : NULL;
                            }
                        } else {
                            curr->data.file_count = 0;
                        }
                    }
                } else {
                    curr->data.file_list = NULL;
                    curr->data.file_count = 0;
                }

                add_to_provides_map(table, curr);
                /* Aggressively inject dummies for provided virtual packages */
                if (pkg_info->provides) inject_dummy_provides(table, pkg_info);
                return 0;
            }
            curr = curr->next;
        }
    }

    if ((double)(table->count + 1) / table->size > GROW_LOAD_FACTOR_THRESHOLD) {
        if (resize_hash_table(table, table->size * 2) != 0) {
            runepkg_util_error("Failed to resize hash table during add operation.\n");
            return -1;
        }
    }

    new_node = runepkg_secure_malloc(sizeof(runepkg_hash_node_t));
    if (!new_node) {
        runepkg_util_error("Failed to allocate memory for new hash table node.\n");
        return -1;
    }

    memset(&new_node->data, 0, sizeof(PkgInfo));

    new_node->data.package_name = pkg_info->package_name ? runepkg_secure_strdup(pkg_info->package_name) : NULL;
    new_node->data.version = pkg_info->version ? runepkg_secure_strdup(pkg_info->version) : NULL;
    new_node->data.architecture = pkg_info->architecture ? runepkg_secure_strdup(pkg_info->architecture) : NULL;
    new_node->data.maintainer = pkg_info->maintainer ? runepkg_secure_strdup(pkg_info->maintainer) : NULL;
    new_node->data.description = pkg_info->description ? runepkg_secure_strdup(pkg_info->description) : NULL;
    new_node->data.depends = pkg_info->depends ? runepkg_secure_strdup(pkg_info->depends) : NULL;
    new_node->data.pre_depends = pkg_info->pre_depends ? runepkg_secure_strdup(pkg_info->pre_depends) : NULL;
    new_node->data.provides = pkg_info->provides ? runepkg_secure_strdup(pkg_info->provides) : NULL;
    new_node->data.build_depends = pkg_info->build_depends ? runepkg_secure_strdup(pkg_info->build_depends) : NULL;
    new_node->data.build_depends_indep = pkg_info->build_depends_indep ? runepkg_secure_strdup(pkg_info->build_depends_indep) : NULL;
    new_node->data.build_depends_arch = pkg_info->build_depends_arch ? runepkg_secure_strdup(pkg_info->build_depends_arch) : NULL;
    new_node->data.conflicts = pkg_info->conflicts ? runepkg_secure_strdup(pkg_info->conflicts) : NULL;
    new_node->data.breaks = pkg_info->breaks ? runepkg_secure_strdup(pkg_info->breaks) : NULL;
    new_node->data.recommends = pkg_info->recommends ? runepkg_secure_strdup(pkg_info->recommends) : NULL;
    new_node->data.suggests = pkg_info->suggests ? runepkg_secure_strdup(pkg_info->suggests) : NULL;
    new_node->data.installed_size = pkg_info->installed_size ? runepkg_secure_strdup(pkg_info->installed_size) : NULL;
    new_node->data.section = pkg_info->section ? runepkg_secure_strdup(pkg_info->section) : NULL;
    new_node->data.priority = pkg_info->priority ? runepkg_secure_strdup(pkg_info->priority) : NULL;
    new_node->data.homepage = pkg_info->homepage ? runepkg_secure_strdup(pkg_info->homepage) : NULL;
    new_node->data.filename = pkg_info->filename ? runepkg_secure_strdup(pkg_info->filename) : NULL;
    new_node->data.multi_arch = pkg_info->multi_arch ? runepkg_secure_strdup(pkg_info->multi_arch) : NULL;
    new_node->data.source_name = pkg_info->source_name ? runepkg_secure_strdup(pkg_info->source_name) : NULL;
    new_node->data.preinst = pkg_info->preinst ? runepkg_secure_strdup(pkg_info->preinst) : NULL;
    new_node->data.postinst = pkg_info->postinst ? runepkg_secure_strdup(pkg_info->postinst) : NULL;
    new_node->data.prerm = pkg_info->prerm ? runepkg_secure_strdup(pkg_info->prerm) : NULL;
    new_node->data.postrm = pkg_info->postrm ? runepkg_secure_strdup(pkg_info->postrm) : NULL;
    new_node->data.md5_verified = pkg_info->md5_verified;
    new_node->data.control_dir_path = pkg_info->control_dir_path ? runepkg_secure_strdup(pkg_info->control_dir_path) : NULL;
    new_node->data.data_dir_path = pkg_info->data_dir_path ? runepkg_secure_strdup(pkg_info->data_dir_path) : NULL;
    new_node->data.extraction_workspace_path = pkg_info->extraction_workspace_path ? runepkg_secure_strdup(pkg_info->extraction_workspace_path) : NULL;

    if (pkg_info->file_list && pkg_info->file_count > 0) {
        runepkg_error_t err = runepkg_validate_file_count(pkg_info->file_count);
        if (err != RUNEPKG_SUCCESS) {
            runepkg_util_error("Invalid file count: %s\n", runepkg_error_string(err));
            new_node->data.file_list = NULL;
            new_node->data.file_count = 0;
        } else {
            new_node->data.file_list = runepkg_secure_malloc(pkg_info->file_count * sizeof(char*));
            if (new_node->data.file_list) {
                int i;
                new_node->data.file_count = pkg_info->file_count;
                for (i = 0; i < pkg_info->file_count; i++) {
                    new_node->data.file_list[i] = pkg_info->file_list[i] ? runepkg_secure_strdup(pkg_info->file_list[i]) : NULL;
                }
            } else {
                new_node->data.file_count = 0;
            }
        }
    } else {
        new_node->data.file_list = NULL;
        new_node->data.file_count = 0;
    }

    index = hash_function(pkg_info->package_name, table->size);
    new_node->next = table->buckets[index];
    table->buckets[index] = new_node;
    table->count++;

    add_to_provides_map(table, new_node);

    /* Aggressively inject dummies for provided virtual packages */
    if (pkg_info->provides) inject_dummy_provides(table, pkg_info);

    runepkg_util_log_verbose("Package '%s' added to hash table.\n", pkg_info->package_name);
    return 0;
}

static int resize_hash_table(runepkg_hash_table_t *table, size_t new_size) {
    runepkg_hash_node_t **new_buckets;
    runepkg_provides_node_t **new_provides;
    runepkg_hash_node_t **old_buckets;
    runepkg_provides_node_t **old_provides;
    size_t old_size;
    size_t i;

    if (!table) return -1;

    if (new_size < MIN_HASH_TABLE_SIZE) {
        new_size = MIN_HASH_TABLE_SIZE;
    }
    new_size = find_next_prime(new_size);

    if (new_size == table->size) return 0;

    new_buckets = runepkg_secure_calloc(new_size, sizeof(runepkg_hash_node_t*));
    if (!new_buckets) {
        runepkg_util_error("Failed to allocate memory for hash table resize.\n");
        return -1;
    }

    new_provides = runepkg_secure_calloc(new_size, sizeof(runepkg_provides_node_t*));
    if (!new_provides) {
        free(new_buckets);
        runepkg_util_error("Failed to allocate memory for provides map resize.\n");
        return -1;
    }

    runepkg_util_log_verbose("Resizing hash table from %lu to %lu buckets\n", (unsigned long)table->size, (unsigned long)new_size);

    old_buckets = table->buckets;
    old_provides = table->provides_buckets;
    old_size = table->size;

    table->buckets = new_buckets;
    table->provides_buckets = new_provides;
    table->size = new_size;
    table->provides_size = new_size;
    table->count = 0;

    for (i = 0; i < old_size; i++) {
        runepkg_hash_node_t *current = old_buckets[i];
        while (current) {
            runepkg_hash_node_t *next = current->next;
            unsigned int new_index = hash_function(current->data.package_name, new_size);
            current->next = table->buckets[new_index];
            table->buckets[new_index] = current;
            table->count++;

            add_to_provides_map(table, current);
            current = next;
        }

        {
            runepkg_provides_node_t *pcurr = old_provides[i];
            while (pcurr) {
                runepkg_provides_node_t *pnext = pcurr->next;
                free(pcurr->virtual_name);
                free(pcurr);
                pcurr = pnext;
            }
        }
    }

    free(old_buckets);
    free(old_provides);
    return 0;
}

void runepkg_hash_remove_package(runepkg_hash_table_t *table, const char *name) {
    unsigned int index;
    runepkg_hash_node_t *current;
    runepkg_hash_node_t *prev = NULL;
    char *saved_provides = NULL;
    char *saved_name = NULL;

    if (!table || !name || name[0] == '\0') return;

    index = hash_function(name, table->size);
    current = table->buckets[index];

    while (current && strcmp(current->data.package_name, name) != 0) {
        prev = current;
        current = current->next;
    }

    if (current) {
        saved_provides = current->data.provides ? strdup(current->data.provides) : NULL;
        saved_name = strdup(current->data.package_name);

        if (prev) {
            prev->next = current->next;
        } else {
            table->buckets[index] = current->next;
        }

        remove_from_provides_map(table, current);

        runepkg_hash_free_package_info(&current->data);
        free(current);
        table->count--;

        /* Clean up any dummy packages that were created for this package's Provides */
        if (saved_provides) {
            cleanup_dummy_provides(table, saved_name, saved_provides);
            free(saved_provides);
        }
        free(saved_name);

        runepkg_util_log_verbose("Package '%s' removed from hash table.\n", name);

        if (table->count > MIN_HASH_TABLE_SIZE && 
            (double)table->count / table->size < SHRINK_LOAD_FACTOR_THRESHOLD) {
            resize_hash_table(table, table->size / 2);
        }
    }
}

void runepkg_hash_destroy_table(runepkg_hash_table_t *table) {
    if (!table) return;

    runepkg_hash_clear_table(table);
    free(table->buckets);
    free(table->provides_buckets);
    free(table);
}

void runepkg_hash_clear_table(runepkg_hash_table_t *table) {
    size_t i;
    if (!table) return;

    for (i = 0; i < table->size; i++) {
        runepkg_hash_node_t *current = table->buckets[i];
        while (current) {
            runepkg_hash_node_t *temp = current;
            current = current->next;
            runepkg_hash_free_package_info(&temp->data);
            free(temp);
        }
        table->buckets[i] = NULL;
    }

    for (i = 0; i < table->provides_size; i++) {
        runepkg_provides_node_t *pcurr = table->provides_buckets[i];
        while (pcurr) {
            runepkg_provides_node_t *pnext = pcurr->next;
            free(pcurr->virtual_name);
            free(pcurr);
            pcurr = pnext;
        }
        table->provides_buckets[i] = NULL;
    }

    table->count = 0;
}

int is_package_provided_by_table(runepkg_hash_table_t *table, const char *pkg_name) {
    unsigned int index;
    runepkg_provides_node_t *curr;
    const char *dash;

    if (!table || !pkg_name) return 0;

    /* First, try exact match in provides map */
    index = hash_function(pkg_name, table->provides_size);
    curr = table->provides_buckets[index];
    while (curr) {
        if (strcmp(curr->virtual_name, pkg_name) == 0) return 1;
        curr = curr->next;
    }

    /* Aggressive Version Sifting:
     * If pkg_name is name-version (e.g. perlapi-5.42.2), sift through all buckets
     * looking for a provider of the prefix 'name-'.
     */
    dash = strrchr(pkg_name, '-');
    if (dash && dash != pkg_name) {
        size_t prefix_len = dash - pkg_name + 1; /* include the dash */
        size_t i;
        for (i = 0; i < table->provides_size; i++) {
            curr = table->provides_buckets[i];
            while (curr) {
                if (strncmp(curr->virtual_name, pkg_name, prefix_len) == 0) {
                    runepkg_util_log_verbose("[hash] Aggressive sifting: satisfied %s via %s\n",
                                             pkg_name, curr->virtual_name);
                    return 1;
                }
                curr = curr->next;
            }
        }
    }

    return 0;
}

/* --- Display Functions --- */

void runepkg_hash_print_package_info(const PkgInfo *pkg_info) {
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
    if (pkg_info->pre_depends) {
        printf("Pre-Depends:  %s\n", pkg_info->pre_depends);
    }
    if (pkg_info->provides) {
        printf("Provides:     %s\n", pkg_info->provides);
    }
    if (pkg_info->build_depends) {
        printf("Build-Depends: %s\n", pkg_info->build_depends);
    }
    if (pkg_info->build_depends_indep) {
        printf("Build-Depends-Indep: %s\n", pkg_info->build_depends_indep);
    }
    if (pkg_info->build_depends_arch) {
        printf("Build-Depends-Arch: %s\n", pkg_info->build_depends_arch);
    }
    if (pkg_info->conflicts) {
        printf("Conflicts:    %s\n", pkg_info->conflicts);
    }
    if (pkg_info->breaks) {
        printf("Breaks:       %s\n", pkg_info->breaks);
    }
    if (pkg_info->recommends) {
        printf("Recommends:   %s\n", pkg_info->recommends);
    }
    if (pkg_info->suggests) {
        printf("Suggests:     %s\n", pkg_info->suggests);
    }
    if (pkg_info->homepage) {
        printf("Homepage:     %s\n", pkg_info->homepage);
    }
    if (pkg_info->source_name) {
        printf("Provided-By:  %s\n", pkg_info->source_name);
    }
    if (pkg_info->description) {
        printf("Description:  %s\n", pkg_info->description);
    }
    if (pkg_info->filename) {
        printf("Filename:     %s\n", pkg_info->filename);
    }

    printf("\nHash Table File List (%d files):\n", pkg_info->file_count);
    if (pkg_info->file_count > 0 && pkg_info->file_list) {
        int i;
        printf("================================\n");
        for (i = 0; i < pkg_info->file_count; i++) {
            if (pkg_info->file_list[i]) {
                printf("  %s\n", pkg_info->file_list[i]);
            }
        }
    } else {
        printf("  (No files or empty package)\n");
    }
    printf("\n");
}

void runepkg_hash_list_packages(runepkg_hash_table_t *table) {
    int count = 0;
    size_t i;
    if (!table) {
        printf("Hash table is NULL.\n");
        return;
    }

    printf("Packages in Hash Table:\n");
    printf("======================\n");
    
    for (i = 0; i < table->size; i++) {
        runepkg_hash_node_t *current = table->buckets[i];
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
