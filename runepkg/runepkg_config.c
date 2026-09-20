/******************************************************************************
 * Filename:    runepkg_config.c
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Configuration parsing and path setup helpers for runepkg
 * LICENSE:     GPL v3
 ******************************************************************************/

#include "runepkg_portable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <ctype.h>
#include <libgen.h>
#include <time.h>

#include "runepkg_util.h"
#include "runepkg_config.h"

/* Define PATH_MAX if not defined */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* --- Global Path Variables Definitions --- */
char *g_runepkg_base_dir = NULL;
char *g_control_dir = NULL;
char *g_runepkg_db_dir = NULL;
char *g_install_dir_internal = NULL;
char *g_system_install_root = NULL;
char *g_pkglist_txt_path = NULL;
char *g_pkglist_bin_path = NULL;
char *g_runepkg_lists_dir = NULL;
char *g_download_dir = NULL;
char *g_build_dir = NULL;
char *g_debs_dir = NULL;
char *g_log_dir = NULL;
char *g_dpkg_host = NULL;
bool g_md5_checks = true;
bool g_verify_signatures = false;

RuneSource **g_sources = NULL;
int g_sources_count = 0;

bool g_cleanup_extract_dirs = true;
bool g_batch_mode = false;
bool g_bootstrap_mode = false;
struct runepkg_hash_table *g_batch_planned_packages = NULL;

ActiveState g_active_state = {NULL, NULL, NULL, NULL, 0};
TargetProfile *g_active_profile = NULL;

/* --- External Global Variables --- */
extern bool g_verbose_mode;

/* --- Internal Function Declarations --- */
void runepkg_config_cleanup(void);
void runepkg_config_load_sources(const char *filepath);
char *runepkg_config_get_registry_path(const char *preferred_config);
char *runepkg_config_find_in_registry(void);
void runepkg_config_register_directory(const char *config_path);

/* --- Profile Management --- */

TargetProfile *runepkg_config_load_profile(const char *profile_name) {
    char *profiles_dir;
    char profile_path[PATH_MAX];
    TargetProfile *profile;

    if (!profile_name) return NULL;

    profiles_dir = runepkg_config_resolve_path(NULL, 2);
    if (!profiles_dir) return NULL;

    snprintf(profile_path, sizeof(profile_path), "%s/%s.conf", profiles_dir, profile_name);
    free(profiles_dir);

    if (!runepkg_util_file_exists(profile_path)) {
        if (access("/etc/runepkg/targets/", R_OK) == 0) {
            snprintf(profile_path, sizeof(profile_path), "/etc/runepkg/targets/%s.conf", profile_name);
        }
        if (!runepkg_util_file_exists(profile_path)) {
            if (access("targets/", R_OK) == 0) {
                snprintf(profile_path, sizeof(profile_path), "targets/%s.conf", profile_name);
            }
        }
        if (!runepkg_util_file_exists(profile_path)) return NULL;
    }

    profile = (TargetProfile *)calloc(1, sizeof(TargetProfile));
    if (!profile) return NULL;

    profile->name = strdup(profile_name);
    profile->arch = runepkg_util_get_config_value(profile_path, "arch", '=');
    profile->subarch = runepkg_util_get_config_value(profile_path, "subarch", '=');
    profile->abi = runepkg_util_get_config_value(profile_path, "abi", '=');
    profile->float_type = runepkg_util_get_config_value(profile_path, "float", '=');
    profile->libc = runepkg_util_get_config_value(profile_path, "libc", '=');
    profile->triplet = runepkg_util_get_config_value(profile_path, "triplet", '=');
    profile->deb_host_arch = runepkg_util_get_config_value(profile_path, "deb_host_arch", '=');

    profile->sysroot = runepkg_util_get_config_value(profile_path, "sysroot", '=');
    profile->crosstools = runepkg_util_get_config_value(profile_path, "crosstools", '=');
    profile->cross_bin = runepkg_util_get_config_value(profile_path, "cross_bin", '=');

    profile->cc = runepkg_util_get_config_value(profile_path, "CC", '=');
    profile->cxx = runepkg_util_get_config_value(profile_path, "CXX", '=');
    profile->ld = runepkg_util_get_config_value(profile_path, "LD", '=');
    profile->ar = runepkg_util_get_config_value(profile_path, "AR", '=');
    profile->ranlib = runepkg_util_get_config_value(profile_path, "RANLIB", '=');
    profile->strip = runepkg_util_get_config_value(profile_path, "STRIP", '=');
    profile->readelf = runepkg_util_get_config_value(profile_path, "READELF", '=');
    profile->cflags = runepkg_util_get_config_value(profile_path, "CFLAGS", '=');
    profile->cxxflags = runepkg_util_get_config_value(profile_path, "CXXFLAGS", '=');
    profile->ldflags = runepkg_util_get_config_value(profile_path, "LDFLAGS", '=');
    profile->pkg_config_sysroot_dir = runepkg_util_get_config_value(profile_path, "PKG_CONFIG_SYSROOT_DIR", '=');
    profile->pkg_config_libdir = runepkg_util_get_config_value(profile_path, "PKG_CONFIG_LIBDIR", '=');

    return profile;
}

void runepkg_config_free_profile(TargetProfile *profile) {
    if (!profile) return;
    free(profile->name); free(profile->arch); free(profile->subarch);
    free(profile->abi); free(profile->float_type); free(profile->libc);
    free(profile->triplet); free(profile->deb_host_arch);
    free(profile->sysroot); free(profile->crosstools); free(profile->cross_bin);
    free(profile->cc); free(profile->cxx); free(profile->ld);
    free(profile->ar); free(profile->ranlib); free(profile->strip); free(profile->readelf);
    free(profile->cflags); free(profile->cxxflags); free(profile->ldflags);
    free(profile->pkg_config_sysroot_dir); free(profile->pkg_config_libdir);
    free(profile);
}

int runepkg_config_load_active_state(void) {
    char *state_path;
    char *at;

    state_path = runepkg_config_resolve_path("active_target.conf", 1);
    if (!state_path || !runepkg_util_file_exists(state_path)) {
        free(state_path);
        return -1;
    }

    if (g_active_state.active_target) free(g_active_state.active_target);
    if (g_active_state.active_triplet) free(g_active_state.active_triplet);
    if (g_active_state.active_sysroot) free(g_active_state.active_sysroot);
    if (g_active_state.active_crosstools) free(g_active_state.active_crosstools);

    g_active_state.active_target = runepkg_util_get_config_value(state_path, "ACTIVE_TARGET", '=');
    g_active_state.active_triplet = runepkg_util_get_config_value(state_path, "ACTIVE_TRIPLET", '=');
    g_active_state.active_sysroot = runepkg_util_get_config_value(state_path, "ACTIVE_SYSROOT", '=');
    g_active_state.active_crosstools = runepkg_util_get_config_value(state_path, "ACTIVE_CROSSTOOLS", '=');

    at = runepkg_util_get_config_value(state_path, "ACTIVATED_AT", '=');
    if (at) {
        g_active_state.activated_at = (int64_t)atol(at);
        free(at);
    }

    free(state_path);

    if (g_active_profile) {
        runepkg_config_free_profile(g_active_profile);
        g_active_profile = NULL;
    }

    if (g_active_state.active_target) {
        g_active_profile = runepkg_config_load_profile(g_active_state.active_target);
    }

    return 0;
}

int runepkg_config_save_active_state(const TargetProfile *profile) {
    char *state_path;
    char tmp_path[PATH_MAX];
    FILE *f;

    state_path = runepkg_config_resolve_path("active_target.conf", 1);
    if (!state_path) return -1;

    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", state_path);

    f = fopen(tmp_path, "w");
    if (!f) {
        free(state_path);
        return -1;
    }

    if (profile) {
        fprintf(f, "ACTIVE_TARGET=%s\n", profile->name ? profile->name : "");
        fprintf(f, "ACTIVE_TRIPLET=%s\n", profile->triplet ? profile->triplet : "");
        fprintf(f, "ACTIVE_SYSROOT=%s\n", profile->sysroot ? profile->sysroot : "");
        fprintf(f, "ACTIVE_CROSSTOOLS=%s\n", profile->crosstools ? profile->crosstools : "");
        fprintf(f, "ACTIVATED_AT=%ld\n", (long)time(NULL));
    }

    fclose(f);

    if (rename(tmp_path, state_path) != 0) {
        unlink(tmp_path);
        free(state_path);
        return -1;
    }

    free(state_path);
    runepkg_config_load_active_state();
    return 0;
}

char *runepkg_config_resolve_path(const char *filename, int type) {
    const char *home = getenv("HOME");
    char *result = NULL;
    char *config_path = runepkg_get_config_file_path();

    if (config_path) {
        char *dir = strdup(config_path);
        char *config_dir = dirname(dir);

        if (access(config_dir, W_OK) == 0) {
            if (type == 1) {
                if (strcmp(filename, "config_registry") == 0) {
                    if (strcmp(config_dir, "/etc/runepkg") == 0)
                        result = strdup("/etc/runepkg/config_registry.txt");
                    else
                        result = runepkg_util_concat_path(config_dir, "config_registry.txt");
                } else if (strcmp(filename, "active_target.conf") == 0) {
                    if (strcmp(config_dir, "/etc/runepkg") == 0)
                        result = strdup("/etc/runepkg/active_target.conf");
                    else if (home && strcmp(config_dir, home) == 0)
                        result = runepkg_util_concat_path(home, ".runepkg_target.conf");
                    else
                        result = runepkg_util_concat_path(config_dir, "active_target.conf");
                }
            } else if (type == 2) {
                result = runepkg_util_concat_path(config_dir, "targets/");
            }
        }
        free(dir);
        free(config_path);
    }

    if (!result && home) {
        if (type == 1) {
            if (strcmp(filename, "config_registry") == 0)
                result = runepkg_util_concat_path(home, ".runepkg_config.registry");
            else if (strcmp(filename, "active_target.conf") == 0)
                result = runepkg_util_concat_path(home, ".runepkg_target.conf");
        } else if (type == 2) {
            result = runepkg_util_concat_path(home, ".runepkg_targets/");
        }
    }

    if (!result && type == 1) {
         if (strcmp(filename, "config_registry") == 0)
             result = strdup("/etc/runepkg/config_registry.txt");
         else if (strcmp(filename, "active_target.conf") == 0)
             result = strdup("/etc/runepkg/active_target.conf");
    } else if (!result && type == 2) {
         result = strdup("/etc/runepkg/targets/");
    }

    return result;
}

char *runepkg_config_get_registry_path(const char *preferred_config) {
    const char *home = getenv("HOME");
    (void)preferred_config;

    if (access("/etc/runepkg/config_registry.txt", R_OK) == 0) {
        return strdup("/etc/runepkg/config_registry.txt");
    }
    if (home) {
        return runepkg_util_concat_path(home, ".runepkg_config.registry");
    }
    return strdup("/etc/runepkg/config_registry.txt");
}

char *runepkg_config_find_in_registry(void) {
    char *registry_path = runepkg_config_get_registry_path(NULL);
    char cwd[PATH_MAX];
    char *found_config = NULL;
    FILE *f;
    char line[PATH_MAX * 2];

    if (!registry_path || !runepkg_util_file_exists(registry_path)) {
        free(registry_path);
        return NULL;
    }

    if (!getcwd(cwd, sizeof(cwd))) {
        free(registry_path);
        return NULL;
    }

    f = fopen(registry_path, "r");
    if (!f) {
        free(registry_path);
        return NULL;
    }

    while (fgets(line, sizeof(line), f)) {
        char *trimmed = runepkg_util_trim_whitespace(line);
        char *sep;
        if (trimmed[0] == '#' || trimmed[0] == '\0') continue;

        sep = strchr(trimmed, '=');
        if (sep) {
            char *reg_dir, *reg_config;
            size_t reg_dir_len;
            *sep = '\0';
            reg_dir = runepkg_util_trim_whitespace(trimmed);
            reg_config = runepkg_util_trim_whitespace(sep + 1);

            reg_dir_len = strlen(reg_dir);
            if (strcmp(cwd, reg_dir) == 0 ||
                (strncmp(cwd, reg_dir, reg_dir_len) == 0 && cwd[reg_dir_len] == '/')) {
                found_config = strdup(reg_config);
                break;
            }
        }
    }

    fclose(f);
    free(registry_path);
    return found_config;
}

void runepkg_config_register_directory(const char *config_path) {
    char *registry_path = runepkg_config_get_registry_path(config_path);
    char cwd[PATH_MAX];
    FILE *f;
    char **lines = NULL;
    int line_count = 0;
    char buffer[PATH_MAX * 2];
    bool found = false;
    int i;

    if (!registry_path || !config_path) {
        free(registry_path);
        return;
    }

    if (!getcwd(cwd, sizeof(cwd))) {
        free(registry_path);
        return;
    }

    runepkg_util_create_dir_recursive("/etc/runepkg", 0755);

    f = fopen(registry_path, "r");
    if (f) {
        while (fgets(buffer, sizeof(buffer), f)) {
            lines = (char **)realloc(lines, sizeof(char *) * (line_count + 1));
            lines[line_count++] = strdup(buffer);
        }
        fclose(f);
    }

    for (i = 0; i < line_count; i++) {
        char *trimmed = strdup(lines[i]);
        char *sep = strchr(trimmed, '=');
        if (sep) {
            char *reg_dir;
            *sep = '\0';
            reg_dir = runepkg_util_trim_whitespace(trimmed);
            if (strcmp(reg_dir, cwd) == 0) {
                free(lines[i]);
                snprintf(buffer, sizeof(buffer), "%s = %s\n", cwd, config_path);
                lines[i] = strdup(buffer);
                found = true;
                free(trimmed);
                break;
            }
        }
        free(trimmed);
    }

    f = fopen(registry_path, "w");
    if (f) {
        for (i = 0; i < line_count; i++) {
            fputs(lines[i], f);
        }
        if (!found) {
            fprintf(f, "%s = %s\n", cwd, config_path);
        }
        fflush(f);
        fsync(fileno(f));
        fclose(f);
    }

    for (i = 0; i < line_count; i++) free(lines[i]);
    free(lines);
    free(registry_path);
}

char *runepkg_get_config_file_path(void) {
    char *env_config_path;
    char *registry_config_path;
    const char *system_config_path = "/etc/runepkg/runepkgconfig";
    const char *home = getenv("HOME");

    env_config_path = getenv("RUNEPKG_CONFIG_PATH");
    if (env_config_path && runepkg_util_file_exists(env_config_path)) {
        return strdup(env_config_path);
    }

    registry_config_path = runepkg_config_find_in_registry();
    if (registry_config_path && runepkg_util_file_exists(registry_config_path)) {
        return registry_config_path;
    }
    free(registry_config_path);

    if (runepkg_util_file_exists(system_config_path)) {
        return strdup(system_config_path);
    }

    if (home) {
        char user_path[PATH_MAX];
        snprintf(user_path, sizeof(user_path), "%s/.runepkg.conf", home);
        if (runepkg_util_file_exists(user_path)) {
            return strdup(user_path);
        }
    }

    return NULL;
}

int runepkg_config_load(void) {
    char *config_file_path = runepkg_get_config_file_path();
    const char *home = getenv("HOME");

    if (!config_file_path) {
        if (!home) {
            fprintf(stderr, "Error: HOME environment variable not set.\n");
            return -1;
        }
        g_runepkg_base_dir = runepkg_util_concat_path(home, "runepkg_dir");
        g_control_dir = runepkg_util_concat_path(home, "runepkg_dir/control_dir");
        g_runepkg_db_dir = runepkg_util_concat_path(home, "runepkg_dir/runepkg_db");
        g_install_dir_internal = runepkg_util_concat_path(home, "runepkg_dir/install_dir");
        g_system_install_root = strdup(g_install_dir_internal);
        g_pkglist_txt_path = runepkg_util_concat_path(g_runepkg_db_dir, "runepkg_autocomplete.txt");
        g_pkglist_bin_path = runepkg_util_concat_path(g_runepkg_db_dir, "runepkg_autocomplete.bin");
        g_runepkg_lists_dir = runepkg_util_concat_path(g_runepkg_db_dir, "lists");
        g_download_dir = runepkg_util_concat_path(g_runepkg_base_dir, "download_dir");
        g_build_dir = runepkg_util_concat_path(g_runepkg_base_dir, "build_dir");
        g_debs_dir = runepkg_util_concat_path(g_runepkg_base_dir, "debs");
        g_log_dir = runepkg_util_concat_path(g_runepkg_base_dir, "log_dir");
        g_md5_checks = true;
    } else {
        char *cleanup_val, *md5_checks_val, *verify_sigs_val;
        runepkg_config_cleanup();
        g_runepkg_base_dir = runepkg_util_get_config_value(config_file_path, "runepkg_dir", '=');
        g_control_dir = runepkg_util_get_config_value(config_file_path, "control_dir", '=');
        g_runepkg_db_dir = runepkg_util_get_config_value(config_file_path, "runepkg_db", '=');
        g_install_dir_internal = runepkg_util_get_config_value(config_file_path, "install_dir", '=');
        g_system_install_root = strdup(g_install_dir_internal);
        g_pkglist_txt_path = runepkg_util_concat_path(g_runepkg_db_dir, "runepkg_autocomplete.txt");
        g_pkglist_bin_path = runepkg_util_concat_path(g_runepkg_db_dir, "runepkg_autocomplete.bin");
        g_runepkg_lists_dir = runepkg_util_concat_path(g_runepkg_db_dir, "lists");
        g_download_dir = runepkg_util_get_config_value(config_file_path, "download_dir", '=');
        if (!g_download_dir && g_runepkg_base_dir) g_download_dir = runepkg_util_concat_path(g_runepkg_base_dir, "download_dir");
        g_build_dir = runepkg_util_get_config_value(config_file_path, "build_dir", '=');
        if (!g_build_dir && g_runepkg_base_dir) g_build_dir = runepkg_util_concat_path(g_runepkg_base_dir, "build_dir");
        g_debs_dir = runepkg_util_get_config_value(config_file_path, "runepkg_debs", '=');
        if (!g_debs_dir && g_runepkg_base_dir) g_debs_dir = runepkg_util_concat_path(g_runepkg_base_dir, "debs");
        g_log_dir = runepkg_util_get_config_value(config_file_path, "log_dir", '=');
        if (!g_log_dir && g_runepkg_base_dir) g_log_dir = runepkg_util_concat_path(g_runepkg_base_dir, "log_dir");
        g_dpkg_host = runepkg_util_get_config_value(config_file_path, "dpkg_host", '=');

        cleanup_val = runepkg_util_get_config_value(config_file_path, "cleanup", '=');
        md5_checks_val = runepkg_util_get_config_value(config_file_path, "md5_checks", '=');
        verify_sigs_val = runepkg_util_get_config_value(config_file_path, "gpg_check", '=');
        g_cleanup_extract_dirs = runepkg_util_parse_yes_no(cleanup_val, true);
        g_md5_checks = runepkg_util_parse_yes_no(md5_checks_val, true);
        g_verify_signatures = runepkg_util_parse_yes_no(verify_sigs_val, true);
        free(cleanup_val); free(md5_checks_val); free(verify_sigs_val);
        runepkg_config_load_sources(config_file_path);
    }
    free(config_file_path);
    return 0;
}

void runepkg_config_cleanup(void) {
    runepkg_util_free_and_null(&g_runepkg_base_dir);
    runepkg_util_free_and_null(&g_control_dir);
    runepkg_util_free_and_null(&g_runepkg_db_dir);
    runepkg_util_free_and_null(&g_install_dir_internal);
    runepkg_util_free_and_null(&g_system_install_root);
    runepkg_util_free_and_null(&g_pkglist_txt_path);
    runepkg_util_free_and_null(&g_pkglist_bin_path);
    runepkg_util_free_and_null(&g_runepkg_lists_dir);
    runepkg_util_free_and_null(&g_download_dir);
    runepkg_util_free_and_null(&g_build_dir);
    runepkg_util_free_and_null(&g_debs_dir);
    runepkg_util_free_and_null(&g_log_dir);
    runepkg_util_free_and_null(&g_dpkg_host);

    if (g_sources) {
        int i;
        for (i = 0; i < g_sources_count; i++) {
            if (g_sources[i]) {
                free(g_sources[i]->type); free(g_sources[i]->url);
                free(g_sources[i]->suite); free(g_sources[i]->components);
                free(g_sources[i]);
            }
        }
        free(g_sources); g_sources = NULL; g_sources_count = 0;
    }

    runepkg_util_free_and_null(&g_active_state.active_target);
    runepkg_util_free_and_null(&g_active_state.active_triplet);
    runepkg_util_free_and_null(&g_active_state.active_sysroot);
    runepkg_util_free_and_null(&g_active_state.active_crosstools);

    if (g_active_profile) {
        runepkg_config_free_profile(g_active_profile);
        g_active_profile = NULL;
    }
}

void runepkg_config_load_sources(const char *filepath) {
    FILE *file = fopen(filepath, "r");
    char line[PATH_MAX * 2];
    if (!file) return;
    while (fgets(line, sizeof(line), file)) {
        char *trimmed = runepkg_util_trim_whitespace(line);
        if (strncmp(trimmed, "deb ", 4) == 0 || strncmp(trimmed, "deb-src ", 8) == 0) {
            char *type = NULL, *url = NULL, *suite = NULL, *components = NULL;
            char *token = strtok(trimmed, " \t");
            if (token) type = strdup(token);
            token = strtok(NULL, " \t");
            if (token) url = strdup(token);
            token = strtok(NULL, " \t");
            if (token) suite = strdup(token);
            components = strtok(NULL, "");
            if (components) components = strdup(runepkg_util_trim_whitespace(components));

            if (type && url && suite) {
                g_sources = (RuneSource **)realloc(g_sources, sizeof(RuneSource *) * (g_sources_count + 1));
                g_sources[g_sources_count] = (RuneSource *)calloc(1, sizeof(RuneSource));
                g_sources[g_sources_count]->type = type;
                g_sources[g_sources_count]->url = url;
                g_sources[g_sources_count]->suite = suite;
                g_sources[g_sources_count]->components = components;
                g_sources_count++;
            } else {
                free(type); free(url); free(suite); free(components);
            }
        }
    }
    fclose(file);
}

void runepkg_init_paths(void) {
    runepkg_log_verbose("Initializing runepkg paths from config...\n");
    if (runepkg_config_load() != 0) {
        fprintf(stderr, "Error: Failed to load runepkg configuration. Exiting.\n");
        exit(EXIT_FAILURE);
    }
    runepkg_config_load_active_state();

    if (!g_runepkg_base_dir || !g_control_dir || !g_runepkg_db_dir || !g_install_dir_internal) {
        fprintf(stderr, "Error: Critical path variables are NULL. Exiting.\n");
        runepkg_config_cleanup();
        exit(EXIT_FAILURE);
    }

    if (!runepkg_util_file_exists(g_runepkg_base_dir)) {
        if (runepkg_util_create_dir_recursive(g_runepkg_base_dir, 0755) != 0) {
            fprintf(stderr, "Error: Failed to create base directory %s\n", g_runepkg_base_dir);
            runepkg_config_cleanup(); exit(EXIT_FAILURE);
        }
    }
    if (!runepkg_util_file_exists(g_control_dir)) runepkg_util_create_dir_recursive(g_control_dir, 0755);
    if (!runepkg_util_file_exists(g_runepkg_db_dir)) runepkg_util_create_dir_recursive(g_runepkg_db_dir, 0755);
    if (!runepkg_util_file_exists(g_runepkg_lists_dir)) runepkg_util_create_dir_recursive(g_runepkg_lists_dir, 0755);
    if (!runepkg_util_file_exists(g_download_dir)) runepkg_util_create_dir_recursive(g_download_dir, 0755);
    if (!runepkg_util_file_exists(g_build_dir)) runepkg_util_create_dir_recursive(g_build_dir, 0755);
    if (!runepkg_util_file_exists(g_debs_dir)) runepkg_util_create_dir_recursive(g_debs_dir, 0755);
    if (g_log_dir && !runepkg_util_file_exists(g_log_dir)) runepkg_util_create_dir_recursive(g_log_dir, 0755);
}
