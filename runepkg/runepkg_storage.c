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
    if (!pkg_name || !path_buffer) {
        return -1;
    }

    if (!g_runepkg_db_dir) {
        printf("Error: runepkg database directory not configured.\n");
        return -1;
    }

    if (pkg_version && pkg_version[0] != '\0') {
        snprintf(path_buffer, PATH_MAX, "%s/%s-%s", g_runepkg_db_dir, pkg_name, pkg_version);
        if (runepkg_util_file_exists(path_buffer)) return 0;

        snprintf(path_buffer, PATH_MAX, "%s/host/%s-%s", g_runepkg_db_dir, pkg_name, pkg_version);
        if (runepkg_util_file_exists(path_buffer)) return 0;

        /* When pkg_version is specified, do NOT return a different version's directory! */
        snprintf(path_buffer, PATH_MAX, "%s/%s-%s", g_runepkg_db_dir, pkg_name, pkg_version);
        return 0;
    }

    /* Only if pkg_version is NULL / empty, check unversioned paths or scan for installed version */
    snprintf(path_buffer, PATH_MAX, "%s/%s", g_runepkg_db_dir, pkg_name);
    if (runepkg_util_file_exists(path_buffer)) return 0;

    snprintf(path_buffer, PATH_MAX, "%s/host/%s", g_runepkg_db_dir, pkg_name);
    if (runepkg_util_file_exists(path_buffer)) return 0;

    {
        const char *db_dirs[2];
        int d;
        db_dirs[0] = g_runepkg_db_dir;
        db_dirs[1] = runepkg_util_concat_path(g_runepkg_db_dir, "host");

        for (d = 0; d < 2; d++) {
            const char *cdir = db_dirs[d];
            DIR *dir;
            struct dirent *entry;

            if (!cdir || !runepkg_util_is_directory(cdir)) {
                if (d == 1 && db_dirs[1]) free((void*)db_dirs[1]);
                continue;
            }

            dir = opendir(cdir);
            if (dir) {
                while ((entry = readdir(dir)) != NULL) {
                    char entry_name[256];
                    const char *sep;

                    if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) continue;
                    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
                        strcmp(entry->d_name, "lists") == 0 || strcmp(entry->d_name, "host") == 0) continue;

                    sep = runepkg_util_find_version_separator(entry->d_name);
                    memset(entry_name, 0, sizeof(entry_name));

                    if (sep) {
                        size_t nlen = (size_t)(sep - entry->d_name);
                        if (nlen >= sizeof(entry_name)) nlen = sizeof(entry_name) - 1;
                        memcpy(entry_name, entry->d_name, nlen);
                        entry_name[nlen] = '\0';
                    } else {
                        runepkg_secure_strcpy(entry_name, sizeof(entry_name), entry->d_name);
                    }

                    if (strcmp(entry_name, pkg_name) == 0 || strcmp(entry->d_name, pkg_name) == 0) {
                        snprintf(path_buffer, PATH_MAX, "%s/%s", cdir, entry->d_name);
                        closedir(dir);
                        if (d == 1 && db_dirs[1]) free((void*)db_dirs[1]);
                        return 0;
                    }
                }
                closedir(dir);
            }
            if (d == 1 && db_dirs[1]) free((void*)db_dirs[1]);
        }
    }

    snprintf(path_buffer, PATH_MAX, "%s/%s", g_runepkg_db_dir, pkg_name);
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
    WRITE_STRING(pkg_info->build_depends);
    WRITE_STRING(pkg_info->build_depends_indep);
    WRITE_STRING(pkg_info->build_depends_arch);
    WRITE_STRING(pkg_info->conflicts);
    WRITE_STRING(pkg_info->replaces);
    WRITE_STRING(pkg_info->breaks);
    WRITE_STRING(pkg_info->recommends);
    WRITE_STRING(pkg_info->suggests);
    WRITE_STRING(pkg_info->installed_size);
    WRITE_STRING(pkg_info->section);
    WRITE_STRING(pkg_info->priority);
    WRITE_STRING(pkg_info->homepage);
    WRITE_STRING(pkg_info->filename);
    WRITE_STRING(pkg_info->multi_arch);
    WRITE_STRING(pkg_info->source_name);

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

    if (!pkg_name || !pkg_info) {
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
                s = runepkg_secure_strdup(ptr); \
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
    PARSE_STRING(pkg_info->build_depends);
    PARSE_STRING(pkg_info->build_depends_indep);
    PARSE_STRING(pkg_info->build_depends_arch);
    PARSE_STRING(pkg_info->conflicts);
    PARSE_STRING(pkg_info->replaces);
    PARSE_STRING(pkg_info->breaks);
    PARSE_STRING(pkg_info->recommends);
    PARSE_STRING(pkg_info->suggests);
    PARSE_STRING(pkg_info->installed_size);
    PARSE_STRING(pkg_info->section);
    PARSE_STRING(pkg_info->priority);
    PARSE_STRING(pkg_info->homepage);
    PARSE_STRING(pkg_info->filename);
    PARSE_STRING(pkg_info->multi_arch);
    PARSE_STRING(pkg_info->source_name);

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

    if (!pkg_name) {
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

int runepkg_storage_find_provider(const char *virtual_pkg, PkgInfo *out_info) {
    const char *db_dirs[2];
    int d;

    if (!virtual_pkg || !out_info || !g_runepkg_db_dir) return -1;

    db_dirs[0] = g_runepkg_db_dir;
    db_dirs[1] = runepkg_util_concat_path(g_runepkg_db_dir, "host");

    for (d = 0; d < 2; d++) {
        const char *cdir = db_dirs[d];
        DIR *dir;
        struct dirent *entry;

        if (!cdir || !runepkg_util_is_directory(cdir)) {
            if (d == 1 && db_dirs[1]) free((void*)db_dirs[1]);
            continue;
        }

        dir = opendir(cdir);
        if (dir) {
            while ((entry = readdir(dir)) != NULL) {
                char entry_name[256];
                char entry_ver[128];
                const char *sep;
                PkgInfo info;

                if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) continue;
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
                    strcmp(entry->d_name, "lists") == 0 || strcmp(entry->d_name, "host") == 0) continue;

                sep = runepkg_util_find_version_separator(entry->d_name);
                memset(entry_name, 0, sizeof(entry_name));
                memset(entry_ver, 0, sizeof(entry_ver));

                if (sep) {
                    size_t nlen = (size_t)(sep - entry->d_name);
                    if (nlen >= sizeof(entry_name)) nlen = sizeof(entry_name) - 1;
                    memcpy(entry_name, entry->d_name, nlen);
                    entry_name[nlen] = '\0';
                    runepkg_secure_strcpy(entry_ver, sizeof(entry_ver), sep + 1);
                } else {
                    runepkg_secure_strcpy(entry_name, sizeof(entry_name), entry->d_name);
                }

                if (runepkg_storage_read_package_info(entry_name, entry_ver, &info) == 0) {
                    if (info.provides && info.provides[0] != '\0') {
                        char *pcopy = strdup(info.provides);
                        if (pcopy) {
                            char *token, *saveptr = NULL;
                            token = strtok_r(pcopy, ",", &saveptr);
                            while (token) {
                                char *vname = runepkg_util_trim_whitespace(token);
                                if (vname) {
                                    char *paren = strchr(vname, '(');
                                    if (paren) *paren = '\0';
                                    vname = runepkg_util_trim_whitespace(vname);
                                    if (vname && strcmp(vname, virtual_pkg) == 0) {
                                        char desc_buf[512];
                                        runepkg_pack_init_package_info(out_info);
                                        out_info->package_name = strdup(virtual_pkg);
                                        out_info->version = info.version ? strdup(info.version) : strdup("1.0");
                                        out_info->architecture = info.architecture ? strdup(info.architecture) : strdup("all");
                                        out_info->maintainer = info.maintainer ? strdup(info.maintainer) : NULL;

                                        snprintf(desc_buf, sizeof(desc_buf), "Virtual package provided by installed package %s (%s)",
                                                 info.package_name ? info.package_name : "host",
                                                 info.version ? info.version : "1.0");
                                        out_info->description = strdup(desc_buf);
                                        out_info->depends = info.depends ? strdup(info.depends) : NULL;
                                        out_info->provides = strdup(virtual_pkg);
                                        out_info->conflicts = info.conflicts ? strdup(info.conflicts) : NULL;
                                        out_info->replaces = info.replaces ? strdup(info.replaces) : NULL;
                                        out_info->breaks = info.breaks ? strdup(info.breaks) : NULL;
                                        out_info->source_name = info.package_name ? strdup(info.package_name) : NULL;
                                        out_info->section = strdup("virtual");

                                        free(pcopy);
                                        runepkg_pack_free_package_info(&info);
                                        closedir(dir);
                                        if (d == 1 && db_dirs[1]) free((void*)db_dirs[1]);
                                        return 0;
                                    }
                                }
                                token = strtok_r(NULL, ",", &saveptr);
                            }
                            free(pcopy);
                        }
                    }
                    runepkg_pack_free_package_info(&info);
                }
            }
            closedir(dir);
        }
        if (d == 1 && db_dirs[1]) free((void*)db_dirs[1]);
    }

    return -1;
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
int runepkg_storage_remove_provided_dummies(const char *pkg_name) {
    const char *db_dirs[2];
    int d;

    if (!pkg_name || !g_runepkg_db_dir) return 0;

    db_dirs[0] = g_runepkg_db_dir;
    db_dirs[1] = runepkg_util_concat_path(g_runepkg_db_dir, "host");

    for (d = 0; d < 2; d++) {
        const char *cdir = db_dirs[d];
        DIR *dir;
        struct dirent *entry;

        if (!cdir || !runepkg_util_is_directory(cdir)) {
            if (d == 1 && db_dirs[1]) free((void*)db_dirs[1]);
            continue;
        }

        dir = opendir(cdir);
        if (dir) {
            while ((entry = readdir(dir)) != NULL) {
                char dummy_pkg[256];
                char dummy_ver[128];
                const char *sep;
                PkgInfo info;

                if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) continue;
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
                    strcmp(entry->d_name, "lists") == 0 || strcmp(entry->d_name, "host") == 0) continue;

                sep = runepkg_util_find_version_separator(entry->d_name);
                if (!sep) continue;

                memset(dummy_pkg, 0, sizeof(dummy_pkg));
                memset(dummy_ver, 0, sizeof(dummy_ver));

                {
                    size_t name_len = (size_t)(sep - entry->d_name);
                    if (name_len >= sizeof(dummy_pkg)) name_len = sizeof(dummy_pkg) - 1;
                    memcpy(dummy_pkg, entry->d_name, name_len);
                    dummy_pkg[name_len] = '\0';
                }
                runepkg_secure_strcpy(dummy_ver, sizeof(dummy_ver), sep + 1);

                if (runepkg_storage_read_package_info(dummy_pkg, dummy_ver, &info) == 0) {
                    bool is_dummy = false;
                    if ((info.section && strcmp(info.section, "virtual") == 0) ||
                        (info.description && strstr(info.description, "Virtual package provided by"))) {
                        is_dummy = true;
                    }

                    if (is_dummy) {
                        bool matches_provider = false;
                        if (info.source_name && strcmp(info.source_name, pkg_name) == 0) {
                            matches_provider = true;
                        } else if (info.description && strstr(info.description, pkg_name) != NULL) {
                            matches_provider = true;
                        }

                        if (matches_provider) {
                            char *dpath = runepkg_util_concat_path(cdir, entry->d_name);
                            if (dpath) {
                                runepkg_log_verbose("[storage] Purging virtual dummy package '%s' provided by removed package '%s'\n", dummy_pkg, pkg_name);
                                runepkg_storage_remove_directory_tree(dpath);
                                free(dpath);
                            }
                        }
                    }
                    runepkg_pack_free_package_info(&info);
                }
            }
            closedir(dir);
        }
        if (d == 1 && db_dirs[1]) free((void*)db_dirs[1]);
    }
    return 0;
}

int runepkg_storage_remove_old_versions(const char *pkg_name, const char *new_version) {
    const char *db_dirs[2];
    int d;
    int removed_count = 0;

    if (!pkg_name || !g_runepkg_db_dir) return 0;

    db_dirs[0] = g_runepkg_db_dir;
    db_dirs[1] = runepkg_util_concat_path(g_runepkg_db_dir, "host");

    for (d = 0; d < 2; d++) {
        const char *cdir = db_dirs[d];
        DIR *dir;
        struct dirent *entry;

        if (!cdir || !runepkg_util_is_directory(cdir)) {
            if (d == 1 && db_dirs[1]) free((void*)db_dirs[1]);
            continue;
        }

        dir = opendir(cdir);
        if (dir) {
            while ((entry = readdir(dir)) != NULL) {
                char entry_pkg[256];
                char entry_ver[128];
                const char *sep;

                if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) continue;
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
                    strcmp(entry->d_name, "lists") == 0 || strcmp(entry->d_name, "host") == 0) continue;

                sep = runepkg_util_find_version_separator(entry->d_name);
                if (!sep) continue;

                memset(entry_pkg, 0, sizeof(entry_pkg));
                memset(entry_ver, 0, sizeof(entry_ver));

                {
                    size_t name_len = (size_t)(sep - entry->d_name);
                    if (name_len >= sizeof(entry_pkg)) name_len = sizeof(entry_pkg) - 1;
                    memcpy(entry_pkg, entry->d_name, name_len);
                    entry_pkg[name_len] = '\0';
                }
                runepkg_secure_strcpy(entry_ver, sizeof(entry_ver), sep + 1);

                if (strcmp(entry_pkg, pkg_name) == 0) {
                    if (!new_version || strcmp(entry_ver, new_version) != 0) {
                        char *old_path = runepkg_util_concat_path(cdir, entry->d_name);
                        if (old_path) {
                            runepkg_log_verbose("[storage] Purging stale version directory: %s\n", old_path);
                            runepkg_storage_remove_directory_tree(old_path);
                            free(old_path);
                            removed_count++;
                        }
                    }
                }
            }
            closedir(dir);
        }
        if (d == 1 && db_dirs[1]) free((void*)db_dirs[1]);
    }
    return removed_count;
}

int runepkg_storage_remove_package(const char *pkg_name, const char *pkg_version) {
    char pkg_dir_path[PATH_MAX];
    if (!pkg_name) {
        return -1;
    }

    /* Purge any host dummy virtual packages provided by pkg_name */
    runepkg_storage_remove_provided_dummies(pkg_name);

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
            if (*entries) {
                (*entries)[(*count)++] = to_add;
            } else {
                free(to_add);
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
        runepkg_log_verbose("No packages or build directories found, building empty conflicts index.\n");
        if (packages) free(packages);
        runepkg_storage_build_conflicts_replaces_index();
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

    /* Also build the conflicts/replaces binary index */
    runepkg_storage_build_conflicts_replaces_index();

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

/* --- Conflicts & Replaces Binary Index Generator & Lookup --- */

static void parse_relation_items(const char *src_pkg, const char *src_ver, const char *field_str,
                                 uint32_t rel_type,
                                 ConflictsReplacesEntry **entries, uint32_t *entry_count, uint32_t *capacity,
                                 char **string_table, uint32_t *strings_size) {
    char *copy, *token, *saveptr = NULL;

    if (!field_str || field_str[0] == '\0') return;

    copy = strdup(field_str);
    if (!copy) return;

    token = strtok_r(copy, ",", &saveptr);
    while (token) {
        char *item = runepkg_util_trim_whitespace(token);
        if (item && item[0] != '\0') {
            char target_pkg[128];
            char constraint[64];
            char *paren;

            memset(target_pkg, 0, sizeof(target_pkg));
            memset(constraint, 0, sizeof(constraint));

            paren = strchr(item, '(');
            if (paren) {
                size_t name_len = (size_t)(paren - item);
                if (name_len >= sizeof(target_pkg)) name_len = sizeof(target_pkg) - 1;
                memcpy(target_pkg, item, name_len);
                target_pkg[name_len] = '\0';

                runepkg_util_safe_strncpy(constraint, paren, sizeof(constraint));
            } else {
                runepkg_util_safe_strncpy(target_pkg, item, sizeof(target_pkg));
            }

            {
                char *t_trim = runepkg_util_trim_whitespace(target_pkg);
                char *c_trim = runepkg_util_trim_whitespace(constraint);

                if (t_trim && t_trim[0] != '\0') {
                    uint32_t off_src_pkg, off_src_ver, off_target_pkg, off_constraint;
                    size_t len;

                    if (*entry_count >= *capacity) {
                        uint32_t new_cap = (*capacity == 0) ? 64 : (*capacity * 2);
                        ConflictsReplacesEntry *new_entries = realloc(*entries, new_cap * sizeof(ConflictsReplacesEntry));
                        if (!new_entries) break;
                        *entries = new_entries;
                        *capacity = new_cap;
                    }

                    #define APPEND_STR(str_val, out_offset) do { \
                        const char *s = (str_val) ? (str_val) : ""; \
                        len = strlen(s) + 1; \
                        *string_table = realloc(*string_table, *strings_size + len); \
                        memcpy(*string_table + *strings_size, s, len); \
                        out_offset = *strings_size; \
                        *strings_size += (uint32_t)len; \
                    } while(0)

                    APPEND_STR(src_pkg, off_src_pkg);
                    APPEND_STR(src_ver, off_src_ver);
                    APPEND_STR(t_trim, off_target_pkg);
                    APPEND_STR(c_trim, off_constraint);

                    (*entries)[*entry_count].src_pkg_offset = off_src_pkg;
                    (*entries)[*entry_count].src_ver_offset = off_src_ver;
                    (*entries)[*entry_count].target_pkg_offset = off_target_pkg;
                    (*entries)[*entry_count].constraint_offset = off_constraint;
                    (*entries)[*entry_count].relation_type = rel_type;

                    (*entry_count)++;
                }
            }
        }
        token = strtok_r(NULL, ",", &saveptr);
    }
    free(copy);
}

int runepkg_storage_build_conflicts_replaces_index(void) {
    char bin_path[PATH_MAX];
    char txt_path[PATH_MAX];
    FILE *fbin = NULL, *ftxt = NULL;
    DIR *dir = NULL;
    struct dirent *entry;
    ConflictsReplacesHeader hdr;
    ConflictsReplacesEntry *entries = NULL;
    uint32_t entry_count = 0;
    uint32_t capacity = 0;
    char *string_table = NULL;
    uint32_t strings_size = 0;
    const char *db_dirs[2];
    int db_idx;

    if (!g_runepkg_db_dir) return -1;

    runepkg_log_verbose("Building conflicts/replaces binary index...\n");

    db_dirs[0] = g_runepkg_db_dir;
    db_dirs[1] = runepkg_util_concat_path(g_runepkg_db_dir, "host");

    for (db_idx = 0; db_idx < 2; db_idx++) {
        const char *current_db = db_dirs[db_idx];
        if (!current_db || !runepkg_util_is_directory(current_db)) {
            if (db_idx == 1 && db_dirs[1]) free((void*)db_dirs[1]);
            continue;
        }

        dir = opendir(current_db);
        if (dir) {
            while ((entry = readdir(dir)) != NULL) {
                char pkg_name[256];
                char pkg_ver[128];
                const char *sep;
                PkgInfo info;

                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                if (strcmp(entry->d_name, "lists") == 0 || strcmp(entry->d_name, "host") == 0) continue;

                sep = runepkg_util_find_version_separator(entry->d_name);
                if (!sep) continue;

                memset(pkg_name, 0, sizeof(pkg_name));
                memset(pkg_ver, 0, sizeof(pkg_ver));

                {
                    size_t name_len = (size_t)(sep - entry->d_name);
                    if (name_len >= sizeof(pkg_name)) name_len = sizeof(pkg_name) - 1;
                    memcpy(pkg_name, entry->d_name, name_len);
                    pkg_name[name_len] = '\0';
                }
                runepkg_secure_strcpy(pkg_ver, sizeof(pkg_ver), sep + 1);

                if (runepkg_storage_read_package_info(pkg_name, pkg_ver, &info) == 0) {
                    if (info.conflicts) {
                        parse_relation_items(pkg_name, pkg_ver, info.conflicts, RUNEPKG_RELATION_CONFLICTS,
                                             &entries, &entry_count, &capacity, &string_table, &strings_size);
                    }
                    if (info.breaks) {
                        parse_relation_items(pkg_name, pkg_ver, info.breaks, RUNEPKG_RELATION_BREAKS,
                                             &entries, &entry_count, &capacity, &string_table, &strings_size);
                    }
                    if (info.replaces) {
                        parse_relation_items(pkg_name, pkg_ver, info.replaces, RUNEPKG_RELATION_REPLACES,
                                             &entries, &entry_count, &capacity, &string_table, &strings_size);
                    }
                    if (info.provides) {
                        parse_relation_items(pkg_name, pkg_ver, info.provides, RUNEPKG_RELATION_PROVIDES,
                                             &entries, &entry_count, &capacity, &string_table, &strings_size);
                    }
                    runepkg_pack_free_package_info(&info);
                }
            }
            closedir(dir);
        }
        if (db_idx == 1 && db_dirs[1]) free((void*)db_dirs[1]);
    }

    /* Write binary index */
    snprintf(bin_path, sizeof(bin_path), "%s/%s", g_runepkg_db_dir, RUNEPKG_STORAGE_CONFLICTS_BINARY_FILE);
    fbin = fopen(bin_path, "wb");
    if (fbin) {
        hdr.magic = 0x52554E45;
        hdr.version = 1;
        hdr.entry_count = entry_count;
        hdr.strings_size = strings_size;

        fwrite(&hdr, sizeof(hdr), 1, fbin);
        if (entry_count > 0 && entries) {
            fwrite(entries, sizeof(ConflictsReplacesEntry), entry_count, fbin);
        }
        if (strings_size > 0 && string_table) {
            fwrite(string_table, 1, strings_size, fbin);
        }
        fclose(fbin);
        chmod(bin_path, 0644);
    }

    /* Write text index summary */
    snprintf(txt_path, sizeof(txt_path), "%s/%s", g_runepkg_db_dir, RUNEPKG_STORAGE_CONFLICTS_TEXT_FILE);
    ftxt = fopen(txt_path, "w");
    if (ftxt) {
        uint32_t i;
        fprintf(ftxt, "# runepkg conflicts-replaces index (%u entries)\n", entry_count);
        for (i = 0; i < entry_count; i++) {
            const char *spkg = string_table + entries[i].src_pkg_offset;
            const char *sver = string_table + entries[i].src_ver_offset;
            const char *tpkg = string_table + entries[i].target_pkg_offset;
            const char *cons = string_table + entries[i].constraint_offset;
            const char *rel_str = "UNKNOWN";

            if (entries[i].relation_type == RUNEPKG_RELATION_CONFLICTS) rel_str = "Conflicts";
            else if (entries[i].relation_type == RUNEPKG_RELATION_BREAKS) rel_str = "Breaks";
            else if (entries[i].relation_type == RUNEPKG_RELATION_REPLACES) rel_str = "Replaces";
            else if (entries[i].relation_type == RUNEPKG_RELATION_PROVIDES) rel_str = "Provides";

            fprintf(ftxt, "%s (%s) %s %s %s\n", spkg, sver, rel_str, tpkg, cons);
        }
        fclose(ftxt);
        chmod(txt_path, 0644);
    }

    if (entries) free(entries);
    if (string_table) free(string_table);

    runepkg_log_verbose("Conflicts/replaces index built: %u entries\n", entry_count);
    return 0;
}

static int get_installed_package_version(const char *pkg_name, char *out_ver, size_t ver_size) {
    PkgInfo info;
    if (!pkg_name || !out_ver) return 0;
    if (runepkg_storage_read_package_info(pkg_name, NULL, &info) == 0) {
        if (info.version) {
            runepkg_secure_strcpy(out_ver, ver_size, info.version);
            runepkg_pack_free_package_info(&info);
            return 1;
        }
        runepkg_pack_free_package_info(&info);
    }
    return 0;
}

int runepkg_storage_check_conflict(const char *pkg_name, const char *pkg_version, char *conflict_target, size_t target_size) {
    char bin_path[PATH_MAX];
    FILE *fbin;
    ConflictsReplacesHeader hdr;
    ConflictsReplacesEntry *entries = NULL;
    char *string_table = NULL;
    uint32_t i;
    int conflict_found = 0;

    if (!pkg_name || !g_runepkg_db_dir) return 0;

    snprintf(bin_path, sizeof(bin_path), "%s/%s", g_runepkg_db_dir, RUNEPKG_STORAGE_CONFLICTS_BINARY_FILE);
    fbin = fopen(bin_path, "rb");
    if (!fbin) return 0;

    if (fread(&hdr, sizeof(hdr), 1, fbin) != 1 || hdr.magic != 0x52554E45) {
        fclose(fbin);
        return 0;
    }

    if (hdr.entry_count == 0) {
        fclose(fbin);
        return 0;
    }

    entries = malloc(hdr.entry_count * sizeof(ConflictsReplacesEntry));
    string_table = malloc(hdr.strings_size);

    if (!entries || !string_table) {
        if (entries) free(entries);
        if (string_table) free(string_table);
        fclose(fbin);
        return -1;
    }

    if (fread(entries, sizeof(ConflictsReplacesEntry), hdr.entry_count, fbin) != hdr.entry_count ||
        fread(string_table, 1, hdr.strings_size, fbin) != hdr.strings_size) {
        free(entries);
        free(string_table);
        fclose(fbin);
        return -1;
    }
    fclose(fbin);

    for (i = 0; i < hdr.entry_count; i++) {
        const char *spkg = string_table + entries[i].src_pkg_offset;
        const char *tpkg = string_table + entries[i].target_pkg_offset;
        const char *cons = string_table + entries[i].constraint_offset;

        if (entries[i].relation_type == RUNEPKG_RELATION_CONFLICTS) {
            if (strcmp(spkg, pkg_name) == 0) {
                if (strcmp(tpkg, pkg_name) != 0) {
                    char inst_ver[128];
                    if (get_installed_package_version(tpkg, inst_ver, sizeof(inst_ver))) {
                        bool is_conflict = true;
                        if (cons && cons[0] != '\0') {
                            is_conflict = (runepkg_util_check_version_constraint(inst_ver, cons) == 1);
                        }
                        if (is_conflict) {
                            if (conflict_target && target_size > 0) {
                                snprintf(conflict_target, target_size, "Package '%s' conflicts with installed package '%s' (%s)", pkg_name, tpkg, inst_ver);
                            }
                            conflict_found = 1;
                            break;
                        }
                    }
                }
            }
            if (strcmp(tpkg, pkg_name) == 0) {
                if (strcmp(spkg, pkg_name) != 0) {
                    char inst_ver[128];
                    if (get_installed_package_version(spkg, inst_ver, sizeof(inst_ver))) {
                        if (conflict_target && target_size > 0) {
                            snprintf(conflict_target, target_size, "Package '%s' conflicts with installed package '%s' (%s)", pkg_name, spkg, inst_ver);
                        }
                        conflict_found = 1;
                        break;
                    }
                }
            }
        } else if (entries[i].relation_type == RUNEPKG_RELATION_BREAKS) {
            if (strcmp(tpkg, pkg_name) == 0 && pkg_version && cons && cons[0] != '\0') {
                if (strcmp(spkg, pkg_name) != 0) {
                    char inst_ver[128];
                    if (get_installed_package_version(spkg, inst_ver, sizeof(inst_ver))) {
                        if (runepkg_util_check_version_constraint(pkg_version, cons) == 1) {
                            if (conflict_target && target_size > 0) {
                                snprintf(conflict_target, target_size, "Package '%s' (%s) breaks installed package '%s' (%s)", pkg_name, pkg_version ? pkg_version : "0", spkg, inst_ver);
                            }
                            conflict_found = 1;
                            break;
                        }
                    }
                }
            }
        }
    }

    free(entries);
    free(string_table);
    return conflict_found;
}
