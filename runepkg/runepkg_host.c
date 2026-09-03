/******************************************************************************
 * Filename:    runepkg_host.c
 * Author:      <michkochris@gmail.com>
 * Date:        started 01-05-2025
 * Description: Host integration and environment management for runepkg
 *
 * Copyright (c) 2025 runepkg (Runar Linux) All rights reserved.
 * GPLV3
 ******************************************************************************/

#include "runepkg_host.h"
#include "runepkg_util.h"
#include "runepkg_storage.h"
#include "runepkg_config.h"
#include "runepkg_defensive.h"
#include "runepkg_pack.h"
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <ctype.h>

/* Global host state */
static char host_arch[32] = "unknown";
static char host_os[64] = "unknown";

int runepkg_host_init(void) {
    struct utsname name;
    if (uname(&name) == 0) {
        runepkg_util_safe_strncpy(host_os, name.sysname, sizeof(host_os));

        /* Map machine name to debian arch */
        if (strcmp(name.machine, "x86_64") == 0) {
            runepkg_util_safe_strncpy(host_arch, "amd64", sizeof(host_arch));
        } else if (strcmp(name.machine, "aarch64") == 0) {
            runepkg_util_safe_strncpy(host_arch, "arm64", sizeof(host_arch));
        } else if (strstr(name.machine, "arm") != NULL) {
            runepkg_util_safe_strncpy(host_arch, "armhf", sizeof(host_arch));
        } else {
            runepkg_util_safe_strncpy(host_arch, name.machine, sizeof(host_arch));
        }
    }

    runepkg_util_log_debug("[host] Initialized: OS=%s, Arch=%s, Privileged=%d",
                          host_os, host_arch, runepkg_host_is_privileged());
    return 0;
}

/**
 * @brief Helper to parse a single stanza from dpkg/status
 */
static int parse_dpkg_stanza(FILE *fp, PkgInfo *info) {
    char line[16384];
    int found_installed = 0;
    int has_content = 0;
    char *current_field = NULL;
    char **field_ptr = NULL;

    runepkg_pack_init_package_info(info);

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len-1] == '\r') line[--len] = '\0';

        if (len == 0) {
            /* End of stanza */
            if (has_content && found_installed) return 1;
            runepkg_pack_free_package_info(info);
            if (has_content) return 0; /* Not installed or empty */
            continue;
        }

        has_content = 1;

        if (line[0] == ' ') {
            /* Continuation line for description */
            if (current_field && field_ptr) {
                size_t old_len = *field_ptr ? strlen(*field_ptr) : 0;
                size_t new_len = old_len + strlen(line) + 2;
                char *tmp = realloc(*field_ptr, new_len);
                if (tmp) {
                    *field_ptr = tmp;
                    if (old_len > 0) strcat(*field_ptr, "\n");
                    strcat(*field_ptr, line + 1);
                }
            }
            continue;
        }

        if (strncmp(line, "Package: ", 9) == 0) {
            info->package_name = strdup(line + 9);
            current_field = "Package";
            field_ptr = &info->package_name;
        } else if (strncmp(line, "Status: ", 8) == 0) {
            if (strstr(line, "installed")) found_installed = 1;
        } else if (strncmp(line, "Version: ", 9) == 0) {
            info->version = strdup(line + 9);
        } else if (strncmp(line, "Architecture: ", 14) == 0) {
            info->architecture = strdup(line + 14);
        } else if (strncmp(line, "Maintainer: ", 12) == 0) {
            info->maintainer = strdup(line + 12);
        } else if (strncmp(line, "Depends: ", 9) == 0) {
            info->depends = strdup(line + 9);
        } else if (strncmp(line, "Pre-Depends: ", 13) == 0) {
            info->pre_depends = strdup(line + 13);
        } else if (strncmp(line, "Provides: ", 10) == 0) {
            info->provides = strdup(line + 10);
        } else if (strncmp(line, "Description: ", 13) == 0) {
            info->description = strdup(line + 13);
            current_field = "Description";
            field_ptr = &info->description;
        } else if (strncmp(line, "Section: ", 9) == 0) {
            info->section = strdup(line + 9);
        } else if (strncmp(line, "Priority: ", 10) == 0) {
            info->priority = strdup(line + 10);
        } else if (strncmp(line, "Homepage: ", 10) == 0) {
            info->homepage = strdup(line + 10);
        } else if (strncmp(line, "Installed-Size: ", 16) == 0) {
            info->installed_size = strdup(line + 16);
        } else {
            current_field = NULL;
            field_ptr = NULL;
        }
    }

    if (has_content && found_installed && info->package_name) return 1;
    runepkg_pack_free_package_info(info);
    return 0;
}

int runepkg_host_sync(void) {
    FILE *fp;
    const char *status_file = "/var/lib/dpkg/status";
    PkgInfo info;
    int count = 0;

    runepkg_util_log_verbose("[host] Synchronizing host packages from %s...", status_file);

    if (!runepkg_util_file_exists(status_file)) {
        runepkg_util_log_debug("[host] dpkg status file not found, skipping deep sync.");
        return 0;
    }

    fp = fopen(status_file, "r");
    if (!fp) {
        runepkg_util_error("[host] Failed to open %s for reading.", status_file);
        return -1;
    }

    while (parse_dpkg_stanza(fp, &info)) {
        if (info.package_name && info.version) {
            /* Check if already in DB to avoid redundant writes */
            if (!runepkg_storage_package_exists(info.package_name, info.version)) {
                if (runepkg_storage_create_package_directory(info.package_name, info.version) == 0) {
                    runepkg_storage_write_package_info(info.package_name, info.version, &info);
                    count++;
                }
            }
        }
        runepkg_pack_free_package_info(&info);
    }

    fclose(fp);

    if (count > 0) {
        runepkg_util_log_verbose("[host] Synced %d new host packages into runepkg database.", count);
        /* Rebuild autocomplete pool to include new host packages */
        runepkg_storage_build_autocomplete_index();
    }

    return 0;
}

int runepkg_host_collect_installed(HostPackageInfo ***out_packages, int *out_count) {
    DIR *dir;
    int count = 0;
    int capacity = 32;
    HostPackageInfo **list;
    struct dirent *entry;

    if (!out_packages || !out_count) return -1;

    if (!g_runepkg_db_dir) return -1;

    dir = opendir(g_runepkg_db_dir);
    if (!dir) {
        *out_count = 0;
        return 0;
    }

    list = (HostPackageInfo **)malloc(sizeof(HostPackageInfo*) * capacity);
    if (!list) {
        closedir(dir);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if ((entry->d_type == DT_DIR || entry->d_type == DT_UNKNOWN) &&
            strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0 &&
            strcmp(entry->d_name, "lists") != 0 && strcmp(entry->d_name, "host") != 0) {

            char pkg_name[256];
            char pkg_version[64];
            const char *ver_dash = runepkg_util_find_version_separator(entry->d_name);

            if (ver_dash && ver_dash != entry->d_name) {
                size_t name_len = (size_t)(ver_dash - entry->d_name);
                if (name_len >= sizeof(pkg_name)) name_len = sizeof(pkg_name) - 1;

                runepkg_util_safe_strncpy(pkg_name, entry->d_name, name_len + 1);
                pkg_name[name_len] = '\0';
                runepkg_secure_strcpy(pkg_version, sizeof(pkg_version), ver_dash + 1);

                if (count >= capacity) {
                    capacity *= 2;
                    list = (HostPackageInfo **)realloc(list, sizeof(HostPackageInfo*) * capacity);
                    if (!list) {
                        closedir(dir);
                        return -1;
                    }
                }

                list[count] = (HostPackageInfo *)calloc(1, sizeof(HostPackageInfo));
                if (list[count]) {
                    runepkg_util_safe_strncpy(list[count]->name, pkg_name, sizeof(list[count]->name));
                    runepkg_util_safe_strncpy(list[count]->version, pkg_version, sizeof(list[count]->version));
                    runepkg_util_safe_strncpy(list[count]->status, "installed", sizeof(list[count]->status));
                    list[count]->install_time = time(NULL);
                    count++;
                }
            }
        }
    }

    closedir(dir);

    *out_packages = list;
    *out_count = count;
    return 0;
}

int runepkg_host_register_install(const PkgInfo *pkg_info) {
    if (!pkg_info) return -1;

    runepkg_util_log_verbose("[host] Registering installation: %s (%s)",
                            pkg_info->package_name, pkg_info->version);

    /* For host integration, we might want to trigger 'dpkg -i' here?
     * But for now, just ensure the metadata is in our DB. */
    if (runepkg_storage_write_package_info(pkg_info->package_name, pkg_info->version, pkg_info) == 0) {
        return 0;
    }

    return -1;
}

int runepkg_host_unregister_removal(const char *pkg_name) {
    DIR *dir;
    struct dirent *entry;
    char *db_dir = g_runepkg_db_dir;
    int removed = 0;

    if (!pkg_name || !db_dir) return -1;

    runepkg_util_log_verbose("[host] Unregistering removal: %s", pkg_name);

    dir = opendir(db_dir);
    if (!dir) return -1;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR || entry->d_type == DT_UNKNOWN) {
            const char *ver_dash = runepkg_util_find_version_separator(entry->d_name);
            if (ver_dash && ver_dash != entry->d_name) {
                size_t name_len = (size_t)(ver_dash - entry->d_name);
                if (strlen(pkg_name) == name_len && strncmp(entry->d_name, pkg_name, name_len) == 0) {
                    char pkg_path[PATH_MAX];
                    snprintf(pkg_path, sizeof(pkg_path), "%s/%s", db_dir, entry->d_name);
                    runepkg_util_log_verbose("[host] Purging host metadata for %s at %s", pkg_name, pkg_path);
                    runepkg_storage_remove_directory_tree(pkg_path);
                    removed = 1;
                }
            }
        }
    }
    closedir(dir);

    if (removed) {
        runepkg_storage_build_autocomplete_index();
    }

    return removed ? 0 : -1;
}

int runepkg_host_query_package(const char *pkg_name, HostPackageInfo *out_info) {
    HostPackageInfo **list;
    int count;
    int found = 0;
    int i;

    if (!pkg_name || !out_info) return -1;

    if (runepkg_host_collect_installed(&list, &count) == 0) {
        for (i = 0; i < count; i++) {
            if (strcmp(list[i]->name, pkg_name) == 0) {
                memcpy(out_info, list[i], sizeof(HostPackageInfo));
                found = 1;
                break;
            }
        }
        runepkg_host_free_package_list(list, count);
    }

    return found;
}

const char *runepkg_host_get_architecture(void) {
    return host_arch;
}

int runepkg_host_is_privileged(void) {
    return (geteuid() == 0);
}

void runepkg_host_free_package_list(HostPackageInfo **packages, int count) {
    int i;
    if (!packages) return;
    for (i = 0; i < count; i++) {
        free(packages[i]);
    }
    free(packages);
}
