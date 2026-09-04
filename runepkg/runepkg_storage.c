/******************************************************************************
 * Filename:    runepkg_storage.c
 * Author:      <michkochris@gmail.com>
 * Date:        started 01-03-2025
 * Description: Persistent storage management for runepkg package database
 *
 * Copyright (c) 2025 runepkg (Runar Linux) All rights reserved.
 * GPLV3
 ******************************************************************************/

#include "runepkg_portable.h"
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
#include <ctype.h>

#include "runepkg_storage.h"
#include "runepkg_config.h"
#include "runepkg_util.h"
#include "runepkg_pack.h"
#include "runepkg_defensive.h"

/* Compare function for qsort */
static int compare_packages(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

/* --- Public Storage Functions --- */

/**
 * @brief Gets the full path to a package directory
 */
int runepkg_storage_get_package_path(const char *pkg_name, const char *pkg_version, 
                                    char *path_buffer) {
    int ret;
    if (!pkg_name || !pkg_version || !path_buffer) {
        return -1;
    }

    if (!g_runepkg_db_dir) {
        printf("Error: runepkg database directory not configured.\n");
        return -1;
    }

    ret = snprintf(path_buffer, PATH_MAX, "%s/%s-%s", g_runepkg_db_dir, pkg_name, pkg_version);
    if (ret >= PATH_MAX) {
        return -1;
    }
    return 0;
}

/**
 * @brief Creates a package directory in the persistent storage
 */
int runepkg_storage_create_package_directory(const char *pkg_name, const char *pkg_version) {
    char pkg_dir_path[PATH_MAX];
    if (!pkg_name || !pkg_version) {
        return -1;
    }

    if (runepkg_storage_get_package_path(pkg_name, pkg_version, pkg_dir_path) != 0) {
        return -1;
    }

    runepkg_log_verbose("Creating package directory: %s\n", pkg_dir_path);

    /* Use the unified utility function to create the directory */
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
    char pkg_dir_path[PATH_MAX];
    char *binary_file_path;
    char tmp_file_path[PATH_MAX];
    FILE *bin_file;
    PkgHeader header;
    size_t slen;

    if (!pkg_name || !pkg_version || !pkg_info) {
        return -1;
    }

    if (runepkg_storage_get_package_path(pkg_name, pkg_version, pkg_dir_path) != 0) {
        return -1;
    }

    binary_file_path = runepkg_util_concat_path(pkg_dir_path, RUNEPKG_STORAGE_BINARY_FILE);
    runepkg_secure_snprintf(tmp_file_path, sizeof(tmp_file_path), "%s.tmp", binary_file_path);

    runepkg_log_verbose("Writing package info atomically via temp file: %s\n", tmp_file_path);

    bin_file = fopen(tmp_file_path, "wb");
    if (!bin_file) {
        runepkg_log_verbose("Failed to open binary file for writing: %s\n", tmp_file_path);
        free(binary_file_path);
        return -1;
    }

    /* Write PkgHeader for fast mmap access */
    header.magic = 0x52554E45;  /* "RUNE" */
    memset(header.pkgname, 0, sizeof(header.pkgname));
    memset(header.version, 0, sizeof(header.version));
    if (pkg_name) runepkg_util_safe_strncpy(header.pkgname, pkg_name, sizeof(header.pkgname));
    if (pkg_version) runepkg_util_safe_strncpy(header.version, pkg_version, sizeof(header.version));
    header.data_start = sizeof(PkgHeader);  /* Data starts after header */

    fwrite(&header, sizeof(PkgHeader), 1, bin_file);

    /* Helper macro to write a string and its length */
    #define WRITE_STRING(s) \
        slen = (s) ? strlen(s) + 1 : 0; \
        fwrite(&slen, sizeof(size_t), 1, bin_file); \
        if (slen > 0) fwrite(s, 1, slen, bin_file);

    WRITE_STRING(pkg_info->package_name);
    WRITE_STRING(pkg_info->version);
    WRITE_STRING(pkg_info->architecture);
    WRITE_STRING(pkg_info->maintainer);
    WRITE_STRING(pkg_info->description);
    WRITE_STRING(pkg_info->depends);
    WRITE_STRING(pkg_info->pre_depends);
    WRITE_STRING(pkg_info->provides);
    WRITE_STRING(pkg_info->installed_size);
    WRITE_STRING(pkg_info->section);
    WRITE_STRING(pkg_info->priority);
    WRITE_STRING(pkg_info->homepage);
    WRITE_STRING(pkg_info->filename);

    /* Write file_count */
    fwrite(&pkg_info->file_count, sizeof(int), 1, bin_file);

    /* Write file list directly into the binary file */
    if (pkg_info->file_list && pkg_info->file_count > 0) {
        int i;
        for (i = 0; i < pkg_info->file_count; i++) {
            WRITE_STRING(pkg_info->file_list[i]);
        }
    }

    fflush(bin_file);
    fsync(fileno(bin_file));
    fclose(bin_file);

    rename(tmp_file_path, binary_file_path);
    free(binary_file_path);
    runepkg_log_verbose("Package info committed atomically: %s\n", pkg_name);
    return 0;
}

/**
 * @brief Reads package info from persistent storage
 */
int runepkg_storage_read_package_info(const char *pkg_name, const char *pkg_version,
                                     PkgInfo *pkg_info) {
    char pkg_dir_path[PATH_MAX];
    char *binary_file_path;
    char *buffer = NULL;
    size_t file_size;
    const char *ptr;
    const char *end;
    int i;

    if (!pkg_name || !pkg_version || !pkg_info) {
        return -1;
    }

    if (runepkg_storage_get_package_path(pkg_name, pkg_version, pkg_dir_path) != 0) {
        return -1;
    }

    binary_file_path = runepkg_util_concat_path(pkg_dir_path, RUNEPKG_STORAGE_BINARY_FILE);

    runepkg_log_verbose("Reading package info from: %s\n", binary_file_path);

    runepkg_pack_init_package_info(pkg_info);

    buffer = runepkg_util_read_file_content(binary_file_path, &file_size);
    free(binary_file_path);

    if (!buffer) {
        runepkg_log_verbose("Failed to read binary file content.\n");
        return -1;
    }

    if (file_size < sizeof(PkgHeader)) {
        free(buffer);
        return -1;
    }

    /* Verify magic using integer comparison for endian safety */
    {
        uint32_t magic;
        memcpy(&magic, buffer, sizeof(uint32_t));
        if (magic != 0x52554E45) {
            free(buffer);
            return -1;
        }
    }

    ptr = buffer + sizeof(PkgHeader);
    end = buffer + file_size;

    /* Helper macro to read a string from the buffer */
    #define PARSE_STRING(s) \
        if (ptr + sizeof(size_t) > end) goto parse_error; \
        { \
            size_t slen; \
            memcpy(&slen, ptr, sizeof(size_t)); \
            ptr += sizeof(size_t); \
            if (slen > 0) { \
                if (ptr + slen > end) goto parse_error; \
                s = strdup(ptr); \
                ptr += slen; \
            } else { \
                s = NULL; \
            } \
        }

    PARSE_STRING(pkg_info->package_name);
    PARSE_STRING(pkg_info->version);
    PARSE_STRING(pkg_info->architecture);
    PARSE_STRING(pkg_info->maintainer);
    PARSE_STRING(pkg_info->description);
    PARSE_STRING(pkg_info->depends);
    PARSE_STRING(pkg_info->pre_depends);
    PARSE_STRING(pkg_info->provides);
    PARSE_STRING(pkg_info->installed_size);
    PARSE_STRING(pkg_info->section);
    PARSE_STRING(pkg_info->priority);
    PARSE_STRING(pkg_info->homepage);
    PARSE_STRING(pkg_info->filename);

    if (ptr + sizeof(int) > end) goto parse_error;
    memcpy(&pkg_info->file_count, ptr, sizeof(int));
    ptr += sizeof(int);

    /* Security check: enforce max file count */
    if (pkg_info->file_count < 0 || pkg_info->file_count > RUNEPKG_MAX_FILE_COUNT) {
        runepkg_util_error("Package metadata corrupted or limits exceeded: file count %d\n", pkg_info->file_count);
        goto parse_error;
    }
    
    if (pkg_info->file_count > 0) {
        pkg_info->file_list = malloc(pkg_info->file_count * sizeof(char *));
        if (!pkg_info->file_list) {
            goto parse_error;
        }

        for (i = 0; i < pkg_info->file_count; i++) {
            PARSE_STRING(pkg_info->file_list[i]);
        }
    }

    if (pkg_info->file_count == 0 || pkg_info->file_list == NULL) {
        runepkg_storage_load_host_file_list(pkg_info->package_name ? pkg_info->package_name : pkg_name, pkg_info);
    }

    free(buffer);
    runepkg_log_verbose("Package info read successfully from persistent storage\n");
    return 0;

parse_error:
    if (buffer) {
        free(buffer);
    }
    runepkg_pack_free_package_info(pkg_info);
    printf("Error: Failed to parse package info from binary buffer\n");
    return -1;
}

int runepkg_storage_load_host_file_list(const char *pkg_name, PkgInfo *pkg_info) {
    char list_path[PATH_MAX];
    FILE *fp;
    char line[PATH_MAX];
    char **files = NULL;
    int count = 0;
    int capacity = 64;

    if (!pkg_name || !pkg_info) return -1;

    /* 1. Check /var/lib/dpkg/info/<pkg_name>.list */
    snprintf(list_path, sizeof(list_path), "/var/lib/dpkg/info/%s.list", pkg_name);
    if (!runepkg_util_file_exists(list_path)) {
        /* 2. Check /var/lib/dpkg/info/<pkg_name>:amd64.list or similar */
        snprintf(list_path, sizeof(list_path), "/var/lib/dpkg/info/%s:amd64.list", pkg_name);
        if (!runepkg_util_file_exists(list_path)) {
            snprintf(list_path, sizeof(list_path), "/var/lib/dpkg/info/%s:arm64.list", pkg_name);
            if (!runepkg_util_file_exists(list_path)) {
                return -1;
            }
        }
    }

    fp = fopen(list_path, "r");
    if (!fp) return -1;

    files = malloc(capacity * sizeof(char *));
    if (!files) { fclose(fp); return -1; }

    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = runepkg_util_trim_whitespace(line);
        struct stat st;
        if (!trimmed || trimmed[0] == '\0') continue;

        /* Skip directories, only collect regular files and symlinks */
        if (lstat(trimmed, &st) == 0 && S_ISDIR(st.st_mode)) continue;

        if (count >= capacity) {
            int new_cap = capacity * 2;
            char **new_files = realloc(files, new_cap * sizeof(char *));
            if (!new_files) break;
            files = new_files;
            capacity = new_cap;
        }

        files[count] = strdup(trimmed);
        if (files[count]) count++;
    }

    fclose(fp);

    if (count > 0) {
        pkg_info->file_list = files;
        pkg_info->file_count = count;
        return 0;
    } else {
        free(files);
        return -1;
    }
}

/**
 * @brief Checks if a package exists in persistent storage
 */
int runepkg_storage_package_exists(const char *pkg_name, const char *pkg_version) {
    char pkg_dir_path[PATH_MAX];
    char *binary_file_path;
    int exists;

    if (!pkg_name || !pkg_version) {
        return -1;
    }

    if (runepkg_storage_get_package_path(pkg_name, pkg_version, pkg_dir_path) != 0) {
        return -1;
    }

    binary_file_path = runepkg_util_concat_path(pkg_dir_path, RUNEPKG_STORAGE_BINARY_FILE);
    exists = runepkg_util_file_exists(binary_file_path) ? 1 : 0;
    free(binary_file_path);
    return exists;
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
    char pkg_dir_path[PATH_MAX];
    if (!pkg_name || !pkg_version) {
        return -1;
    }

    if (runepkg_storage_get_package_path(pkg_name, pkg_version, pkg_dir_path) != 0) {
        return -1;
    }

    runepkg_log_verbose("Removing package directory: %s\n", pkg_dir_path);
    if (!runepkg_util_file_exists(pkg_dir_path)) {
        runepkg_log_verbose("Package directory not present, nothing to remove: %s\n", pkg_dir_path);
        return 0;
    }

    if (runepkg_storage_remove_directory_tree(pkg_dir_path) != 0) {
        runepkg_log_verbose("Warning: Failed to remove package directory: %s\n", pkg_dir_path);
        return -1;
    }

    return 0;
}

/* --- Directory removal (also used to purge temporary extraction trees) --- */
int runepkg_storage_remove_directory_tree(const char *path) {
    DIR *dir = opendir(path);
    struct dirent *entry;
    int ret = 0;

    if (!dir) {
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char *child;
        struct stat st;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        child = runepkg_util_concat_path(path, entry->d_name);

        if (lstat(child, &st) != 0) {
            free(child);
            ret = -1;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (runepkg_storage_remove_directory_tree(child) != 0) {
                ret = -1;
            }
        } else {
            if (unlink(child) != 0) {
                ret = -1;
            }
        }
        free(child);
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
    DIR *dir;
    struct dirent *entry;
    char **packages = NULL;
    int count = 0;
    int capacity = 0;
    size_t max_len = 0;
    int i;
    struct winsize w;
    int width = 80;
    int col_width;
    int cols;
    int rows;
    int r;
    size_t pattern_len = pattern ? strlen(pattern) : 0;

    if (!g_runepkg_db_dir) {
        printf("Error: runepkg database directory not configured.\n");
        return -1;
    }

    runepkg_log_verbose("Listing packages from: %s\n", g_runepkg_db_dir);

    dir = opendir(g_runepkg_db_dir);
    if (!dir) {
        printf("Error: Cannot open runepkg database directory: %s\n", g_runepkg_db_dir);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        bool is_dir;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "lists") == 0) continue;

        is_dir = (entry->d_type == DT_DIR);
        if (entry->d_type == DT_UNKNOWN) {
            char full[PATH_MAX + 256];
            struct stat st;
            snprintf(full, sizeof(full), "%.*s/%s", (int)(sizeof(full)-258), g_runepkg_db_dir, entry->d_name);
            if (stat(full, &st) == 0) {
                is_dir = S_ISDIR(st.st_mode);
            }
        }

        if (is_dir) {
            if (!pattern || strncmp(entry->d_name, pattern, pattern_len) == 0) {
                if (count >= capacity) {
                    char **new_packages;
                    capacity = (capacity == 0) ? 1024 : capacity * 2;
                    new_packages = realloc(packages, capacity * sizeof(char *));
                    if (!new_packages) {
                        perror("realloc failed in list_packages");
                        break;
                    }
                    packages = new_packages;
                }
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
        if (packages) free(packages);
        return 0;
    }

    qsort(packages, count, sizeof(char *), compare_packages);

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        width = w.ws_col;
    }

    col_width = (int)max_len + 2;
    cols = width / col_width;
    if (cols < 1) cols = 1;

    rows = (count + cols - 1) / cols;
    for (r = 0; r < rows; r++) {
        int c;
        for (c = 0; c < cols; c++) {
            int idx = r * cols + c;
            if (idx < count) {
                printf("%-*s", (int)col_width, packages[idx]);
            }
        }
        printf("\n");
    }

    for (i = 0; i < count; i++) {
        free(packages[i]);
    }
    free(packages);

    return count;
}

/**
 * @brief Helper to scan a directory for subdirectories or specific files and add to a string array.
 * Note: This function no longer performs deduplication, which should be done by the caller.
 */
static int scan_and_add_entries(const char *dir_path, char ***entries, int *count, int *capacity, bool subdirs_only, const char *suffix_filter, bool add_absolute) {
    DIR *dir;
    struct dirent *entry;
    if (!dir_path) return 0;
    dir = opendir(dir_path);
    if (!dir) return 0;

    while ((entry = readdir(dir)) != NULL) {
        bool is_dir;
        bool is_reg;
        char *to_add = NULL;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "lists") == 0) continue;

        is_dir = (entry->d_type == DT_DIR);
        is_reg = (entry->d_type == DT_REG);

        if (entry->d_type == DT_UNKNOWN) {
            char full[PATH_MAX + 256];
            struct stat st;
            snprintf(full, sizeof(full), "%.*s/%s", (int)(sizeof(full)-258), dir_path, entry->d_name);
            if (stat(full, &st) == 0) {
                is_dir = S_ISDIR(st.st_mode);
                is_reg = S_ISREG(st.st_mode);
            }
        }

        if (subdirs_only && !is_dir) continue;

        if (suffix_filter && is_reg) {
            size_t nlen = strlen(entry->d_name);
            size_t slen = strlen(suffix_filter);
            if (nlen < slen || strcmp(entry->d_name + nlen - slen, suffix_filter) != 0) continue;
        } else if (suffix_filter && !is_dir) {
            continue;
        }

        if (add_absolute) {
            char *full_path = runepkg_util_concat_path(dir_path, entry->d_name);
            if (full_path) {
                if (is_dir) {
                    to_add = malloc(strlen(full_path) + 2);
                    if (to_add) snprintf(to_add, strlen(full_path) + 2, "%s/", full_path);
                    free(full_path);
                } else {
                    to_add = full_path;
                }
            }
        } else {
            to_add = strdup(entry->d_name);
        }

        if (to_add) {
            if (*count >= *capacity) {
                char **temp;
                *capacity = (*capacity == 0) ? 1024 : *capacity * 2;
                temp = realloc(*entries, *capacity * sizeof(char *));
                if (!temp) {
                    free(to_add);
                    closedir(dir);
                    return -1;
                }
                *entries = temp;
            }
            (*entries)[(*count)++] = to_add;
        }

        /* SPECIAL: For database directories, also add the package name WITHOUT the version */
        if (!add_absolute && is_dir) {
             const char *p_sep = runepkg_util_find_version_separator(entry->d_name);
             if (p_sep) {
                 size_t name_only_len = (size_t)(p_sep - entry->d_name);
                 char *name_only = malloc(name_only_len + 1);
                 if (name_only) {
                     runepkg_util_safe_strncpy(name_only, entry->d_name, name_only_len + 1);
                     name_only[name_only_len] = '\0';
                     if (*count >= *capacity) {
                         char **temp;
                         *capacity = (*capacity == 0) ? 1024 : *capacity * 2;
                         temp = realloc(*entries, *capacity * sizeof(char *));
                         if (temp) {
                             *entries = temp;
                             (*entries)[(*count)++] = name_only;
                         } else free(name_only);
                     } else {
                         (*entries)[(*count)++] = name_only;
                     }
                 }
             }
        }
    }
    closedir(dir);
    return 0;
}

/**
 * @brief Builds the binary autocomplete index (runepkg_autocomplete.bin)
 */
int runepkg_storage_build_autocomplete_index(void) {
    char **packages = NULL;
    int count = 0;
    int capacity = 0;
    size_t strings_size = 0;
    int i;
    char index_path[PATH_MAX];
    FILE *fp;
    AutocompleteHeader hdr;
    uint32_t offset = 0;
    uint32_t *offset_table = NULL;
    char *string_blob = NULL;
    char *ptr;

    if (!g_runepkg_db_dir) {
        runepkg_log_verbose("Error: runepkg database directory not configured.\n");
        return -1;
    }

    runepkg_log_verbose("Building consolidated autopool index...\n");

    /* 1. Scan installed packages */
    if (scan_and_add_entries(g_runepkg_db_dir, &packages, &count, &capacity, true, NULL, false) != 0) {
        runepkg_log_verbose("Error: Failed to scan database directory.\n");
        goto error_cleanup;
    }

    /* 1b. Scan persistent host-dpkg packages */
    {
        char *host_db_path = runepkg_util_concat_path(g_runepkg_db_dir, "host");
        if (host_db_path) {
            if (runepkg_util_is_directory(host_db_path)) {
                if (scan_and_add_entries(host_db_path, &packages, &count, &capacity, true, NULL, false) != 0) {
                    runepkg_log_verbose("Warning: Failed to scan host database directory.\n");
                }
            }
            free(host_db_path);
        }
    }

    /* 2. Scan build directory */
    if (g_build_dir) {
        if (scan_and_add_entries(g_build_dir, &packages, &count, &capacity, true, NULL, true) != 0) {
            runepkg_log_verbose("Error: Failed to scan build directory for subdirs.\n");
            goto error_cleanup;
        }
        if (scan_and_add_entries(g_build_dir, &packages, &count, &capacity, false, ".dsc", true) != 0) {
            runepkg_log_verbose("Error: Failed to scan build directory for .dsc files.\n");
            goto error_cleanup;
        }
    }

    /* 3. Scan debs directory */
    if (g_debs_dir) {
        if (scan_and_add_entries(g_debs_dir, &packages, &count, &capacity, false, ".deb", true) != 0) {
            runepkg_log_verbose("Error: Failed to scan debs directory for .deb files.\n");
            goto error_cleanup;
        }
    }

    /* 4. Scan download directory */
    if (g_download_dir) {
        if (scan_and_add_entries(g_download_dir, &packages, &count, &capacity, false, ".deb", true) != 0) {
            runepkg_log_verbose("Error: Failed to scan download directory for .deb files.\n");
            goto error_cleanup;
        }
    }

    /* 5. Scan repository indexes (if they exist) */
    {
        const char *repo_bins[2] = {"repo_index.bin", "repo_src_index.bin"};
        int j;
        typedef struct {
            char name[64];
            uint32_t file_id;
            uint32_t offset;
        } RepoIndexEntry;

        for (j = 0; j < 2; j++) {
            char path[PATH_MAX];
            FILE *rf;
            snprintf(path, sizeof(path), "%s/%s", g_runepkg_db_dir, repo_bins[j]);
            rf = fopen(path, "rb");
            if (rf) {
                uint32_t rcount = 0;
                if (fread(&rcount, sizeof(rcount), 1, rf) == 1 && rcount > 0) {
                    RepoIndexEntry *entries = malloc(rcount * sizeof(RepoIndexEntry));
                    if (entries) {
                        if (fread(entries, sizeof(RepoIndexEntry), rcount, rf) == rcount) {
                            uint32_t k;
                            for (k = 0; k < rcount; k++) {
                                if (count >= capacity) {
                                    char **temp;
                                    capacity = (capacity == 0) ? 1024 : capacity * 2;
                                    temp = realloc(packages, capacity * sizeof(char *));
                                    if (!temp) {
                                        free(entries); fclose(rf); goto error_cleanup;
                                    }
                                    packages = temp;
                                }
                                packages[count++] = strdup(entries[k].name);
                            }
                        }
                        free(entries);
                    }
                }
                fclose(rf);
            }
        }
    }

    if (count == 0) {
        runepkg_log_verbose("No packages or build directories found, skipping index build.\n");
        if (packages) free(packages);
        return 0;
    }

    /* O(n log n) Sort and Dedup */
    qsort(packages, count, sizeof(char *), compare_packages);

    {
        int unique_count = 0;
        for (i = 0; i < count; i++) {
            if (i == 0 || strcmp(packages[i], packages[i-1]) != 0) {
                packages[unique_count++] = packages[i];
            } else {
                free(packages[i]);
            }
        }
        count = unique_count;
    }

    for (i = 0; i < count; i++) {
        strings_size += strlen(packages[i]) + 1;
    }

    snprintf(index_path, sizeof(index_path), "%s/runepkg_autocomplete.bin", g_runepkg_db_dir);

    fp = fopen(index_path, "wb");
    if (!fp) {
        runepkg_log_verbose("Error: Cannot create index file: %s\n", index_path);
        goto error_cleanup;
    }

    /* Write header */
    hdr.magic = 0x52554E45; /* "RUNE" */
    hdr.version = 1;
    hdr.entry_count = (uint32_t)count;
    hdr.strings_size = (uint32_t)strings_size;
    fwrite(&hdr, sizeof(hdr), 1, fp);

    /* Build and write offset table and string blob in batches */
    offset_table = malloc(count * sizeof(uint32_t));
    string_blob = malloc(strings_size);
    if (!offset_table || !string_blob) {
        if (fp) fclose(fp);
        goto error_cleanup;
    }

    ptr = string_blob;
    for (i = 0; i < count; i++) {
        size_t slen = strlen(packages[i]) + 1;
        offset_table[i] = offset;
        memcpy(ptr, packages[i], slen);
        ptr += slen;
        offset += (uint32_t)slen;
    }

    fwrite(offset_table, sizeof(uint32_t), count, fp);
    fwrite(string_blob, 1, strings_size, fp);

    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    free(offset_table);
    free(string_blob);

    if (chmod(index_path, 0644) != 0) {
        runepkg_log_verbose("Warning: Failed to set permissions on autocomplete index\n");
    }

    for (i = 0; i < count; i++) free(packages[i]);
    free(packages);

    runepkg_log_verbose("Autocomplete index built: %d entries, %s\n", count, index_path);
    return 0;

error_cleanup:
    if (packages) {
        for (i = 0; i < count; i++) free(packages[i]);
        free(packages);
    }
    if (offset_table) free(offset_table);
    if (string_blob) free(string_blob);
    return -1;
}
