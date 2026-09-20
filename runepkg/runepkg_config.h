/******************************************************************************
 * Filename:    runepkg_config.h
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Configuration declarations for runepkg
 * LICENSE:     GPL v3
 ******************************************************************************/

#ifndef RUNEPKG_CONFIG_H
#define RUNEPKG_CONFIG_H

#include "runepkg_portable.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

/* Define PATH_MAX if not defined */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* --- Global Path Variables Declarations --- */
extern char *g_runepkg_base_dir;
extern char *g_control_dir;
extern char *g_runepkg_db_dir;
extern char *g_install_dir_internal;
extern char *g_system_install_root;
extern char *g_pkglist_txt_path;
extern char *g_pkglist_bin_path;
extern char *g_runepkg_lists_dir;
extern char *g_download_dir;
extern char *g_build_dir;
extern char *g_debs_dir;
extern char *g_log_dir;
extern char *g_dpkg_host;
extern bool g_md5_checks;
extern bool g_verify_signatures;

extern bool g_cleanup_extract_dirs;
extern bool g_auto_confirm_deps;
extern bool g_auto_confirm_siblings;
extern bool g_asked_siblings;
extern bool g_completion_mode;
extern bool g_batch_mode;
extern bool g_bootstrap_mode;
extern struct runepkg_hash_table *g_batch_planned_packages;

/* --- Target Profile & Active State --- */

typedef struct {
    char *name;
    char *arch;
    char *subarch;
    char *abi;
    char *float_type;
    char *libc;
    char *triplet;
    char *deb_host_arch;

    char *sysroot;
    char *crosstools;
    char *cross_bin;

    char *cc;
    char *cxx;
    char *ld;
    char *ar;
    char *ranlib;
    char *strip;
    char *readelf;
    char *cflags;
    char *cxxflags;
    char *ldflags;
    char *pkg_config_sysroot_dir;
    char *pkg_config_libdir;
} TargetProfile;

typedef struct {
    char *active_target;
    char *active_triplet;
    char *active_sysroot;
    char *active_crosstools;
    int64_t activated_at;
} ActiveState;

extern ActiveState g_active_state;
extern TargetProfile *g_active_profile;

/* --- Source Configuration --- */

typedef struct {
    char *type;       /* "deb" or "deb-src" */
    char *url;
    char *suite;
    char *components; /* Space-separated list */
} RuneSource;

extern RuneSource **g_sources;
extern int g_sources_count;

/* --- Function Prototypes --- */

void runepkg_init_paths(void);
int runepkg_config_load(void);
void runepkg_config_cleanup(void);
char *runepkg_get_config_file_path(void);

char *runepkg_config_resolve_path(const char *filename, int type);
TargetProfile *runepkg_config_load_profile(const char *profile_name);
void runepkg_config_free_profile(TargetProfile *profile);
int runepkg_config_save_active_state(const TargetProfile *profile);
int runepkg_config_load_active_state(void);

#endif /* RUNEPKG_CONFIG_H */
