/******************************************************************************/
/* Filename:    runepkg_cpp_ffi.h                                              */
/* Author:      <michkochris@gmail.com>                                        */
/* Date:        2026-08-29                                                     */
/* Description: Master C/C++ FFI Header & Extended Engine Declarations         */
/* License:     GPL v3                                                         */
/******************************************************************************/

#ifndef RUNEPKG_CPP_FFI_H
#define RUNEPKG_CPP_FFI_H

#include <stddef.h>
#include "runepkg_portable.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Rune Dependency Resolver & Graph Harvester Types                           */
/* -------------------------------------------------------------------------- */

typedef struct RuneTargetNode {
    char *package_name;
    char *version;
    char *arch;
    char *source_name;              /* For source: pkg name; For binary: .deb filename */
    char *binary_filename;          /* Path relative to repo root */
    size_t download_size;
    char **target_build_depends;    /* Pruned Tier 3 C/C++ libraries only */
    int target_build_depends_count;
    char **host_tools_required;     /* Tier 0/2 host tools (make, autoconf, etc.) */
    int host_tools_count;
} RuneTargetNode;

typedef struct RuneTargetPlan {
    RuneTargetNode *nodes;
    int node_count;
} RuneTargetPlan;

/* -------------------------------------------------------------------------- */
/* Core Engine Availability & System Ingestion                                */
/* -------------------------------------------------------------------------- */

int runepkg_cpp_ffi_available(void);
int runepkg_host_dpkg_sync(void);

/* -------------------------------------------------------------------------- */
/* Repository Synchronization & Inspection                                    */
/* -------------------------------------------------------------------------- */

int runepkg_update(void);
int runepkg_upgrade(void);
int runepkg_repo_search(const char *query);
int runepkg_repo_info(const char *pkg_name);
int runepkg_repo_package_exists(const char *pkg_name);
char* runepkg_repo_find_source_for_binary(const char *bin_pkg_name);
char* runepkg_repo_get_source_build_depends(const char *src_pkg_name);

/* -------------------------------------------------------------------------- */
/* Binary Package Fetching & Installation                                     */
/* -------------------------------------------------------------------------- */

int runepkg_repo_install(const char *pkg_name);
int runepkg_repo_install_multiple(const char **pkg_names, int count);
char* runepkg_repo_download(const char *pkg_name, bool recursive);
int runepkg_repo_download_multiple(const char **pkg_names, int count, bool recursive);
int runepkg_repo_build_depends_download(const char *pkg_name);
int runepkg_repo_build_depends_download_multiple(const char **pkg_names, int count);

/* -------------------------------------------------------------------------- */
/* Source Package Operations & Tree Unpacking                                 */
/* -------------------------------------------------------------------------- */

int runepkg_repo_source_download(const char *pkg_name);
int runepkg_repo_source_download_multiple(const char **pkg_names, int count);
int runepkg_repo_source_depends_download(const char *pkg_name);
int runepkg_repo_source_depends_download_multiple(const char **pkg_names, int count);
int runepkg_repo_source_build_depends_download(const char *pkg_name);
int runepkg_repo_source_build_depends_download_multiple(const char **pkg_names, int count);
int runepkg_source_unpack(const char *dsc_path);

/* -------------------------------------------------------------------------- */
/* Forge Build Engine & Package Assembly                                      */
/* -------------------------------------------------------------------------- */

int runepkg_source_build(const char *dsc_path);
int runepkg_source_build_split(const char *dsc_path, const char *target_pkg);
int runepkg_source_build_sysroot(const char *dsc_path);

/* -------------------------------------------------------------------------- */
/* Toolchain Bootstrap Engine & Target Profiles                               */
/* -------------------------------------------------------------------------- */

int handle_switch(const char *target);
void handle_print_profile(void);
int handle_build_toolchain(const char *target);
int handle_build_toolchain_with_targets(const char *target, const char **pkg_names, int count);
int handle_build_toolchain_engine(const char *target_name);
int handle_build_toolchain_targets(const char *target_name, const char **pkg_names, int count);

/* -------------------------------------------------------------------------- */
/* Dependency Graph Harvester & Target Plan Resolver                          */
/* -------------------------------------------------------------------------- */

int runepkg_resolver_harvest_graph(const char *sources_dir, const char *out_db_path);
int runepkg_resolver_resolve_target(const char *pkg_name, RuneTargetPlan **out_plan);
int runepkg_resolver_get_install_plan(const char *pkg_name, RuneTargetPlan **out_plan);
void runepkg_resolver_free_plan(RuneTargetPlan *plan);
int runepkg_resolver_dump_tree(const char *pkg_name);

#ifdef __cplusplus
}
#endif

/* -------------------------------------------------------------------------- */
/* C++ Matrix Engine & Recipe Definitions                                     */
/* -------------------------------------------------------------------------- */
#ifdef __cplusplus

#include <string>
#include <vector>
#include <functional>
#include <filesystem>

namespace fs = std::filesystem;

enum class BuildSystem {
    AUTOTOOLS,
    CMAKE,
    MESON,
    CUSTOM_MAKE
};

struct ProfileMatrix {
    std::string profile_name;
    std::string arch;        /* "x86_64", "aarch64", "armhf", "riscv64" */
    std::string libc;        /* "musl", "glibc" */
    bool is_static = false;
    bool is_pie = false;
    std::string triplet;
    std::string sysroot;
    std::string crosstools;
    std::string cross_bin;
};

struct RuneMatrixRecipe {
    std::string package_name;
    BuildSystem build_system = BuildSystem::AUTOTOOLS;

    std::vector<std::string> extra_conf_args;
    std::vector<std::string> make_build_targets;
    std::vector<std::string> make_install_targets;

    std::function<bool(const fs::path& work_dir, const ProfileMatrix& matrix)> pre_configure_hook;
    std::function<bool(const fs::path& work_dir, const ProfileMatrix& matrix)> custom_build_hook;
    std::function<bool(const fs::path& work_dir, const fs::path& dest_dir, const ProfileMatrix& matrix)> custom_install_hook;
};

class RuneMatrixEngine {
public:
    static ProfileMatrix parse_profile(const std::string& profile_name);
    static const RuneMatrixRecipe* get_recipe(const std::string& pkg_name);
    static BuildSystem detect_build_system(const fs::path& src_dir);
};

#endif /* __cplusplus */

#endif /* RUNEPKG_CPP_FFI_H */
