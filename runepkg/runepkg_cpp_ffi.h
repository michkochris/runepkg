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
/* Security Perimeter & Cryptographic Trust Declarations                       */
/* -------------------------------------------------------------------------- */

int runepkg_security_verify_sha256(const char *filepath, const char *expected_hash);
int runepkg_security_verify_sha512(const char *filepath, const char *expected_hash);
int runepkg_security_verify_gpg(const char *filepath, const char *keyring_path);
int runepkg_security_sanitize_path(const char *base_dir, const char *entry_path, char *out_buf, size_t max_len);
int runepkg_security_apply_rlimits(size_t max_bytes, size_t max_files);
int runepkg_security_drop_privileges(const char *username);
int runepkg_security_drop_privileges_for_worker(const char *username);
int runepkg_security_is_root(void);

/* -------------------------------------------------------------------------- */
/* Extended C++ Utility Bridge Declarations                                   */
/* -------------------------------------------------------------------------- */

char** runepkg_util_cpp_split_string(const char *str, char delim, int *count_out);
void runepkg_util_cpp_free_string_array(char **arr, int count);
char* runepkg_util_cpp_read_file(const char *filepath);
int runepkg_util_cpp_exec_cmd(const char *cmd, char **output_out);

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

#endif /* RUNEPKG_CPP_FFI_H */
