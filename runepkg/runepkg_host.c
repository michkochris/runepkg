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
#include <glob.h>

/* Global host state */
static char host_arch[32] = "unknown";
static char host_os[64] = "unknown";
static int is_wsl_cached = -1;

int runepkg_host_is_wsl(void) {
    struct utsname name;
    if (is_wsl_cached != -1) return is_wsl_cached;

    if (uname(&name) == 0) {
        if (strstr(name.release, "WSL") || strstr(name.version, "WSL") ||
            strstr(name.release, "Microsoft") || strstr(name.version, "Microsoft")) {
            is_wsl_cached = 1;
        } else {
            is_wsl_cached = 0;
        }
    } else {
        is_wsl_cached = 0;
    }
    return is_wsl_cached;
}

static void runepkg_host_prepare_wsl_env(void) {
    if (!runepkg_host_is_wsl()) return;

    runepkg_util_log_verbose("[host] WSL detected. Preparing environment stability fixes...");

    /* Fix for NetworkManager postinst: chmod: cannot access '/var/lib/NetworkManager' */
    if (runepkg_host_is_privileged()) {
        if (!runepkg_util_file_exists("/var/lib/NetworkManager")) {
            runepkg_util_log_verbose("[host] Creating /var/lib/NetworkManager for WSL compatibility.");
            runepkg_util_create_dir_recursive("/var/lib/NetworkManager", 0755);
        }
    }
}

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

    runepkg_util_log_debug("[host] Initialized: OS=%s, Arch=%s, Privileged=%d, WSL=%d",
                          host_os, host_arch, runepkg_host_is_privileged(), runepkg_host_is_wsl());

    runepkg_host_prepare_wsl_env();

    return 0;
}

/**
 * @brief Helper to parse a single stanza from dpkg/status
 */
static int parse_dpkg_stanza(FILE *fp, PkgInfo *info) {
    char line[16384];
    int found_installed = 0;
    int has_content = 0;
    char **field_ptr = NULL;
    size_t len;

    runepkg_pack_init_package_info(info);

    while (!feof(fp) && !ferror(fp)) {
        if (!fgets(line, sizeof(line), fp)) break;
        len = strlen(line);
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
            if (field_ptr == &info->description) {
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
            if (info->package_name) free(info->package_name);
            info->package_name = strdup(runepkg_util_trim_whitespace(line + 9));
            field_ptr = &info->package_name;
        } else if (strncmp(line, "Status: ", 8) == 0) {
            if (strstr(line, "installed")) found_installed = 1;
        } else if (strncmp(line, "Version: ", 9) == 0) {
            if (info->version) free(info->version);
            info->version = strdup(runepkg_util_trim_whitespace(line + 9));
        } else if (strncmp(line, "Architecture: ", 14) == 0) {
            if (info->architecture) free(info->architecture);
            info->architecture = strdup(runepkg_util_trim_whitespace(line + 14));
        } else if (strncmp(line, "Maintainer: ", 12) == 0) {
            if (info->maintainer) free(info->maintainer);
            info->maintainer = strdup(runepkg_util_trim_whitespace(line + 12));
        } else if (strncmp(line, "Depends: ", 9) == 0) {
            if (info->depends) free(info->depends);
            info->depends = strdup(runepkg_util_trim_whitespace(line + 9));
        } else if (strncmp(line, "Pre-Depends: ", 13) == 0) {
            if (info->pre_depends) free(info->pre_depends);
            info->pre_depends = strdup(runepkg_util_trim_whitespace(line + 13));
        } else if (strncmp(line, "Provides: ", 10) == 0) {
            if (info->provides) free(info->provides);
            info->provides = strdup(runepkg_util_trim_whitespace(line + 10));
        } else if (strncmp(line, "Build-Depends: ", 15) == 0) {
            runepkg_util_free_and_null(&info->build_depends);
            info->build_depends = runepkg_secure_strdup(runepkg_util_trim_whitespace(line + 15));
        } else if (strncmp(line, "Build-Depends-Indep: ", 21) == 0) {
            runepkg_util_free_and_null(&info->build_depends_indep);
            info->build_depends_indep = runepkg_secure_strdup(runepkg_util_trim_whitespace(line + 21));
        } else if (strncmp(line, "Build-Depends-Arch: ", 20) == 0) {
            runepkg_util_free_and_null(&info->build_depends_arch);
            info->build_depends_arch = runepkg_secure_strdup(runepkg_util_trim_whitespace(line + 20));
        } else if (strncmp(line, "Conflicts: ", 11) == 0) {
            if (info->conflicts) free(info->conflicts);
            info->conflicts = strdup(runepkg_util_trim_whitespace(line + 11));
        } else if (strncmp(line, "Breaks: ", 8) == 0) {
            if (info->breaks) free(info->breaks);
            info->breaks = strdup(runepkg_util_trim_whitespace(line + 8));
        } else if (strncmp(line, "Recommends: ", 12) == 0) {
            if (info->recommends) free(info->recommends);
            info->recommends = strdup(runepkg_util_trim_whitespace(line + 12));
        } else if (strncmp(line, "Suggests: ", 10) == 0) {
            if (info->suggests) free(info->suggests);
            info->suggests = strdup(runepkg_util_trim_whitespace(line + 10));
        } else if (strncmp(line, "Description: ", 13) == 0) {
            if (info->description) free(info->description);
            info->description = strdup(runepkg_util_trim_whitespace(line + 13));
            field_ptr = &info->description;
        } else if (strncmp(line, "Section: ", 9) == 0) {
            if (info->section) free(info->section);
            info->section = strdup(runepkg_util_trim_whitespace(line + 9));
        } else if (strncmp(line, "Priority: ", 10) == 0) {
            if (info->priority) free(info->priority);
            info->priority = strdup(runepkg_util_trim_whitespace(line + 10));
        } else if (strncmp(line, "Homepage: ", 10) == 0) {
            if (info->homepage) free(info->homepage);
            info->homepage = strdup(runepkg_util_trim_whitespace(line + 10));
        } else if (strncmp(line, "Installed-Size: ", 16) == 0) {
            if (info->installed_size) free(info->installed_size);
            info->installed_size = strdup(line + 16);
        } else if (strncmp(line, "Multi-Arch: ", 12) == 0) {
            if (info->multi_arch) free(info->multi_arch);
            info->multi_arch = strdup(line + 12);
        } else if (strncmp(line, "Source: ", 8) == 0) {
            if (info->source_name) free(info->source_name);
            info->source_name = strdup(line + 8);
        } else {
            field_ptr = NULL;
        }
    }

    if (has_content && found_installed && info->package_name) return 1;
    runepkg_pack_free_package_info(info);
    return 0;
}

static void generate_provides_dummy_packages(const PkgInfo *host_pkg) {
    char *copy, *token, *saveptr = NULL;

    if (!host_pkg || !host_pkg->provides || host_pkg->provides[0] == '\0') return;

    copy = strdup(host_pkg->provides);
    if (!copy) return;

    token = strtok_r(copy, ",", &saveptr);
    while (token) {
        char *virt_name = runepkg_util_trim_whitespace(token);
        if (virt_name && virt_name[0] != '\0') {
            char *paren = strchr(virt_name, '(');
            if (paren) *paren = '\0';
            virt_name = runepkg_util_trim_whitespace(virt_name);

            if (virt_name && virt_name[0] != '\0') {
                char dummy_version[64];
                PkgInfo dummy_info;

                runepkg_secure_strcpy(dummy_version, sizeof(dummy_version), host_pkg->version ? host_pkg->version : "1.0");

                if (!runepkg_storage_package_exists(virt_name, dummy_version)) {
                    if (runepkg_storage_create_package_directory(virt_name, dummy_version) == 0) {
                        char desc_buf[512];
                        runepkg_pack_init_package_info(&dummy_info);
                        dummy_info.package_name = strdup(virt_name);
                        dummy_info.version = strdup(dummy_version);

                        snprintf(desc_buf, sizeof(desc_buf), "Virtual package provided by host package %s (%s)",
                                 host_pkg->package_name ? host_pkg->package_name : "host",
                                 host_pkg->version ? host_pkg->version : "1.0");
                        dummy_info.description = strdup(desc_buf);
                        dummy_info.provides = strdup(virt_name);
                        dummy_info.architecture = host_pkg->architecture ? strdup(host_pkg->architecture) : strdup("all");
                        dummy_info.section = strdup("virtual");
                        dummy_info.maintainer = host_pkg->maintainer ? strdup(host_pkg->maintainer) : NULL;
                        dummy_info.conflicts = host_pkg->conflicts ? strdup(host_pkg->conflicts) : NULL;
                        dummy_info.breaks = host_pkg->breaks ? strdup(host_pkg->breaks) : NULL;
                        dummy_info.depends = host_pkg->depends ? strdup(host_pkg->depends) : NULL;
                        dummy_info.source_name = host_pkg->package_name ? strdup(host_pkg->package_name) : NULL;

                        runepkg_storage_write_package_info(virt_name, dummy_version, &dummy_info);
                        runepkg_pack_free_package_info(&dummy_info);
                        runepkg_util_log_verbose("[host] Created dummy virtual package for '%s' (provided by %s)", virt_name, host_pkg->package_name ? host_pkg->package_name : "host");
                    }
                }
            }
        }
        token = strtok_r(NULL, ",", &saveptr);
    }
    free(copy);
}

static void prune_orphaned_provides_dummies(char **active_provides, int active_count) {
    char host_dir[PATH_MAX];
    DIR *dir;
    struct dirent *entry;

    if (!g_runepkg_db_dir) return;

    snprintf(host_dir, sizeof(host_dir), "%s/host", g_runepkg_db_dir);
    if (!runepkg_util_is_directory(host_dir)) return;

    dir = opendir(host_dir);
    if (!dir) return;

    while ((entry = readdir(dir)) != NULL) {
        char pkg_name[256];
        char pkg_ver[128];
        const char *sep;
        PkgInfo info;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

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
            if ((info.section && strcmp(info.section, "virtual") == 0) ||
                (info.description && strstr(info.description, "Virtual dummy package"))) {
                int is_active = 0;
                int i;
                for (i = 0; i < active_count; i++) {
                    if (active_provides[i] && strcmp(active_provides[i], pkg_name) == 0) {
                        is_active = 1;
                        break;
                    }
                }
                if (!is_active) {
                    char *dummy_path = runepkg_util_concat_path(host_dir, entry->d_name);
                    if (dummy_path) {
                        runepkg_util_log_verbose("[host] Pruning orphaned dummy virtual package: %s", dummy_path);
                        runepkg_storage_remove_directory_tree(dummy_path);
                        free(dummy_path);
                    }
                }
            }
            runepkg_pack_free_package_info(&info);
        }
    }
    closedir(dir);
}

int runepkg_host_sync(void) {
    FILE *fp;
    const char *status_file = "/var/lib/dpkg/status";
    PkgInfo info;
    int count = 0;
    char **active_provides = NULL;
    int active_provides_count = 0;
    int active_provides_cap = 0;
    int i;

    /* Check dpkg_host config: if 'none', skip host sync but still update local index */
    if (g_dpkg_host && strcmp(g_dpkg_host, "none") == 0) {
        runepkg_util_log_debug("[host] dpkg_host is set to 'none'. Skipping host sync.");
        runepkg_storage_build_autocomplete_index();
        return 0;
    }

    runepkg_util_log_verbose("[host] Synchronizing host packages from %s...", status_file);

    if (!runepkg_util_file_exists(status_file)) {
        runepkg_util_log_debug("[host] dpkg status file not found, skipping deep sync.");
        runepkg_storage_build_autocomplete_index();
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
            if (info.provides) {
                char *pcopy = strdup(info.provides);
                if (pcopy) {
                    char *ptoken, *psave = NULL;
                    ptoken = strtok_r(pcopy, ",", &psave);
                    while (ptoken) {
                        char *vname = runepkg_util_trim_whitespace(ptoken);
                        if (vname && vname[0] != '\0') {
                            char *paren = strchr(vname, '(');
                            if (paren) *paren = '\0';
                            vname = runepkg_util_trim_whitespace(vname);
                            if (vname && vname[0] != '\0') {
                                if (active_provides_count >= active_provides_cap) {
                                    int ncap = (active_provides_cap == 0) ? 32 : active_provides_cap * 2;
                                    char **ntable = realloc(active_provides, ncap * sizeof(char*));
                                    if (ntable) {
                                        active_provides = ntable;
                                        active_provides_cap = ncap;
                                    }
                                }
                                if (active_provides_count < active_provides_cap) {
                                    active_provides[active_provides_count++] = strdup(vname);
                                }
                            }
                        }
                        ptoken = strtok_r(NULL, ",", &psave);
                    }
                    free(pcopy);
                }
                generate_provides_dummy_packages(&info);
            }
        }
        runepkg_pack_free_package_info(&info);
    }

    fclose(fp);

    /* Prune any dummy virtual packages whose host providers were removed */
    prune_orphaned_provides_dummies(active_provides, active_provides_count);

    for (i = 0; i < active_provides_count; i++) free(active_provides[i]);
    if (active_provides) free(active_provides);

    runepkg_util_log_verbose("[host] Synced %d new host packages into runepkg database.", count);
    /* Always rebuild autocomplete pool and conflicts index after sync */
    runepkg_storage_build_autocomplete_index();

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

extern char *g_system_install_root;

/* Internal helper to update dpkg status file by replacing/adding a package stanza */
static int runepkg_host_update_status_file(const PkgInfo *pkg_info) {
    const char *status_path = "/var/lib/dpkg/status";
    char tmp_path[PATH_MAX];
    FILE *in, *out;
    char line[16384];
    int skip_current = 0;

    if (access(status_path, R_OK) != 0) return 0; /* No status file to update */

    snprintf(tmp_path, sizeof(tmp_path), "%s.runepkg.tmp", status_path);
    in = fopen(status_path, "r");
    if (!in) return -1;
    out = fopen(tmp_path, "w");
    if (!out) { fclose(in); return -1; }

    while (fgets(line, sizeof(line), in)) {
        if (strncmp(line, "Package: ", 9) == 0) {
            const char *p_name = line + 9;
            char name_buf[256];
            size_t len = strlen(p_name);
            if (len > 0 && p_name[len-1] == '\n') len--;
            if (len > 0 && p_name[len-1] == '\r') len--;
            if (len >= sizeof(name_buf)) len = sizeof(name_buf) - 1;
            strncpy(name_buf, p_name, len);
            name_buf[len] = '\0';

            if (strcmp(name_buf, pkg_info->package_name) == 0) {
                skip_current = 1;
            } else {
                skip_current = 0;
            }
        } else if (skip_current && (line[0] == '\n' || line[0] == '\r')) {
            skip_current = 0;
            continue; /* Skip the blank line at end of stanza */
        }

        if (!skip_current) {
            fputs(line, out);
        }
    }

    /* Append the new/updated stanza */
    fprintf(out, "\nPackage: %s\n", pkg_info->package_name);
    fprintf(out, "Status: install ok installed\n");
    if (pkg_info->version) fprintf(out, "Version: %s\n", pkg_info->version);
    if (pkg_info->architecture) fprintf(out, "Architecture: %s\n", pkg_info->architecture);
    else fprintf(out, "Architecture: amd64\n");
    if (pkg_info->maintainer) fprintf(out, "Maintainer: %s\n", pkg_info->maintainer);
    if (pkg_info->section) fprintf(out, "Section: %s\n", pkg_info->section);
    if (pkg_info->priority) fprintf(out, "Priority: %s\n", pkg_info->priority);
    if (pkg_info->depends) fprintf(out, "Depends: %s\n", pkg_info->depends);
    if (pkg_info->pre_depends) fprintf(out, "Pre-Depends: %s\n", pkg_info->pre_depends);
    if (pkg_info->provides) fprintf(out, "Provides: %s\n", pkg_info->provides);
    if (pkg_info->build_depends) fprintf(out, "Build-Depends: %s\n", pkg_info->build_depends);
    if (pkg_info->build_depends_indep) fprintf(out, "Build-Depends-Indep: %s\n", pkg_info->build_depends_indep);
    if (pkg_info->build_depends_arch) fprintf(out, "Build-Depends-Arch: %s\n", pkg_info->build_depends_arch);
    if (pkg_info->conflicts) fprintf(out, "Conflicts: %s\n", pkg_info->conflicts);
    if (pkg_info->breaks) fprintf(out, "Breaks: %s\n", pkg_info->breaks);
    if (pkg_info->recommends) fprintf(out, "Recommends: %s\n", pkg_info->recommends);
    if (pkg_info->suggests) fprintf(out, "Suggests: %s\n", pkg_info->suggests);
    if (pkg_info->installed_size) fprintf(out, "Installed-Size: %s\n", pkg_info->installed_size);
    if (pkg_info->multi_arch) fprintf(out, "Multi-Arch: %s\n", pkg_info->multi_arch);
    if (pkg_info->source_name) fprintf(out, "Source: %s\n", pkg_info->source_name);
    if (pkg_info->homepage) fprintf(out, "Homepage: %s\n", pkg_info->homepage);
    if (pkg_info->description) fprintf(out, "Description: %s\n", pkg_info->description);
    else fprintf(out, "Description: Injected by runepkg high-speed engine\n");
    fprintf(out, "\n");

    fclose(in);
    if (fflush(out) != 0 || fsync(fileno(out)) != 0) {
        fclose(out);
        unlink(tmp_path);
        return -1;
    }
    fclose(out);

    if (rename(tmp_path, status_path) != 0) {
        unlink(tmp_path);
        return -1;
    }

    return 0;
}

int runepkg_host_register_install(const PkgInfo *pkg_info) {
    char list_path[PATH_MAX + 128];
    FILE *list_file;
    int i;

    if (!pkg_info || !pkg_info->package_name) return -1;

    runepkg_util_log_verbose("[host] Registering installation & injecting into dpkg host: %s (%s)",
                            pkg_info->package_name, pkg_info->version ? pkg_info->version : "unknown");

    /* If dpkg_host is not 'none' and we are installing to system root '/', inject into host dpkg database */
    if ((!g_dpkg_host || strcmp(g_dpkg_host, "none") != 0) && (!g_system_install_root || strcmp(g_system_install_root, "/") == 0)) {
        if (runepkg_host_update_status_file(pkg_info) == 0) {
             runepkg_util_log_verbose("[host] Atomically updated package stanza for %s in /var/lib/dpkg/status", pkg_info->package_name);
        }

        if (pkg_info->file_count > 0 && pkg_info->file_list) {
            snprintf(list_path, sizeof(list_path), "/var/lib/dpkg/info/%s.list", pkg_info->package_name);
            list_file = fopen(list_path, "w");
            if (list_file) {
                for (i = 0; i < pkg_info->file_count; i++) {
                    const char *rel = pkg_info->file_list[i];
                    if (rel && rel[0] != '\0') {
                        if (rel[0] == '/') fprintf(list_file, "%s\n", rel);
                        else fprintf(list_file, "/%s\n", rel);
                    }
                }
                fclose(list_file);
                runepkg_util_log_verbose("[host] Generated dpkg file list: %s", list_path);
            }
        }
    }

    return 0;
}

int runepkg_host_unregister_removal(const char *pkg_name) {
    char *db_dir = g_runepkg_db_dir;
    int removed = 0;
    char info_pattern[PATH_MAX + 128];
    glob_t glob_result;
    int i;
    char cmd[PATH_MAX + 128];
    char pkg_path[PATH_MAX * 4];
    char host_db_dir[PATH_MAX * 2];

    if (!pkg_name || !db_dir) return -1;

    runepkg_util_log_verbose("[host] Unregistering removal: %s", pkg_name);

    /* If dpkg_host is set to 'none', do not touch host system dpkg /var/lib/dpkg */
    if (g_dpkg_host && strcmp(g_dpkg_host, "none") == 0) {
        runepkg_util_log_verbose("[host] dpkg_host=none. Skipping host dpkg trace removal.");
    } else if (!g_dpkg_host || strcmp(g_dpkg_host, "auto") == 0 || strcmp(g_dpkg_host, "yes") == 0) {
        /* Purge all traces of dpkg storage on host system (/var/lib/dpkg) */
        snprintf(info_pattern, sizeof(info_pattern), "/var/lib/dpkg/info/%s*", pkg_name);
        if (glob(info_pattern, 0, NULL, &glob_result) == 0) {
            for (i = 0; i < (int)glob_result.gl_pathc; i++) {
                runepkg_util_log_verbose("[host] Removing dpkg info trace: %s", glob_result.gl_pathv[i]);
                (void)unlink(glob_result.gl_pathv[i]);
            }
            globfree(&glob_result);
        }

        /* If privileged, also invoke dpkg --purge --force-depends to clean up status database */
        if (runepkg_host_is_privileged() && access("/usr/bin/dpkg", X_OK) == 0) {
            snprintf(cmd, sizeof(cmd), "dpkg --purge --force-depends %s >/dev/null 2>&1", pkg_name);
            {
                int sys_ret = system(cmd);
                (void)sys_ret;
            }
        }
    }

    snprintf(pkg_path, sizeof(pkg_path), "%s/%s", db_dir, pkg_name);
    if (runepkg_util_file_exists(pkg_path)) {
        runepkg_util_log_verbose("[host] Purging host metadata for %s at %s", pkg_name, pkg_path);
        runepkg_storage_remove_directory_tree(pkg_path);
        removed = 1;
    }

    /* Also check host subdirectory if it exists: g_runepkg_db_dir/host */
    snprintf(host_db_dir, sizeof(host_db_dir), "%s/host", db_dir);
    snprintf(pkg_path, sizeof(pkg_path), "%s/%s", host_db_dir, pkg_name);
    if (runepkg_util_file_exists(pkg_path)) {
        runepkg_util_log_verbose("[host] Purging host sub-db metadata for %s at %s", pkg_name, pkg_path);
        runepkg_storage_remove_directory_tree(pkg_path);
        removed = 1;
    }

    if (removed) {
        runepkg_storage_build_autocomplete_index();
    }

    return removed ? 0 : -1;
}

int runepkg_host_query_package(const char *pkg_name, HostPackageInfo *out_info) {
    HostPackageInfo **list = NULL;
    int count = 0;
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