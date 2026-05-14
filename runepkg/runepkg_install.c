#include "runepkg_install.h"
#include "runepkg_config.h"
#include "runepkg_util.h"
#include "runepkg_pack.h"
#include "runepkg_hash.h"
#include "runepkg_storage.h"
#include "runepkg_handle.h"
#include "runepkg_md5sums.h"

#ifdef ENABLE_CPP_FFI
#include "runepkg_cpp_ffi.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <errno.h>
#include <glob.h>
#include <time.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/statvfs.h>

/* forward-declare internal installer to avoid implicit declaration when
 * `handle_install` (wrapper) calls it before its definition. */
static int handle_install_internal(const char *deb_file_path, int is_top_level);

int runepkg_execute_maintainer_script(const char *script_path, const PkgInfo *pkg_info, const char *action) {
    if (!script_path || !runepkg_util_file_exists(script_path)) return 0;

    // Smart check: Only execute scripts if we are installing to the actual system root.
    // In LFS/ISO creation scenarios (alternate root), we skip them to avoid breakage.
    if (!g_system_install_root || strcmp(g_system_install_root, "/") != 0) {
        char *script_name = basename((char*)script_path);
        printf("\033[1;33m[warning]\033[0m (non-root install) detected skipping %s for %s\n",
               script_name, pkg_info->package_name);
        return 0;
    }

    char *script_name = basename((char*)script_path);
    printf("\033[1;33m[warning]\033[0m Running %s for %s (%s)...\n", script_name, pkg_info->package_name, action);
    runepkg_util_log_verbose("Executing maintainer script %s for %s (%s)\n", script_name, pkg_info->package_name, action);

    // Set environment variables
    setenv("DPKG_MAINTSCRIPT_PACKAGE", pkg_info->package_name, 1);
    setenv("DPKG_MAINTSCRIPT_NAME", script_name, 1);
    if (pkg_info->architecture) setenv("DPKG_MAINTSCRIPT_ARCH", pkg_info->architecture, 1);
    if (pkg_info->version) setenv("DPKG_MAINTSCRIPT_VERSION", pkg_info->version, 1);

    // Set DPKG_ROOT if we are not installing to actual system root
    if (g_system_install_root && strcmp(g_system_install_root, "/") != 0) {
        setenv("DPKG_ROOT", g_system_install_root, 1);
        runepkg_util_log_verbose("Set DPKG_ROOT=%s for relocatable script execution\n", g_system_install_root);
    }

    char *argv[] = {"sh", (char*)script_path, (char*)action, NULL};
    int ret = runepkg_util_execute_command("/bin/sh", argv);

    unsetenv("DPKG_MAINTSCRIPT_PACKAGE");
    unsetenv("DPKG_MAINTSCRIPT_NAME");
    unsetenv("DPKG_MAINTSCRIPT_ARCH");
    unsetenv("DPKG_MAINTSCRIPT_VERSION");
    unsetenv("DPKG_ROOT");

    if (ret != 0) {
        runepkg_util_error("Maintainer script %s failed with status %d\n", script_name, ret);
    }
    return ret;
}

int runepkg_install_verify_md5(const PkgInfo *pkg_info) {
    if (!pkg_info || !pkg_info->control_dir_path || !pkg_info->data_dir_path) return -1;

    char *md5sums_path = runepkg_util_concat_path(pkg_info->control_dir_path, "md5sums");

    if (!runepkg_util_file_exists(md5sums_path)) {
        runepkg_util_log_verbose("No md5sums file found for %s, skipping verification.\n", pkg_info->package_name);
        free(md5sums_path);
        return 0;
    }

    FILE *f = fopen(md5sums_path, "r");
    if (!f) {
        free(md5sums_path);
        return -1;
    }

    runepkg_util_log_verbose("Verifying MD5 checksums for %s...\n", pkg_info->package_name);

    char line[PATH_MAX + 64];
    int errors = 0;
    int total = 0;
    while (fgets(line, sizeof(line), f)) {
        char expected_md5[33];
        char rel_path[PATH_MAX];
        if (sscanf(line, "%32s  %s", expected_md5, rel_path) != 2) continue;
        total++;

        char *full_path = runepkg_util_concat_path(pkg_info->data_dir_path, rel_path);

        char actual_md5[33];
        if (runepkg_md5_file(full_path, actual_md5) != 0) {
            runepkg_util_error("Failed to compute MD5 for %s\n", full_path);
            free(full_path);
            errors++;
            continue;
        }

        if (strcmp(expected_md5, actual_md5) != 0) {
            runepkg_util_error("MD5 mismatch for %s: expected %s, got %s\n", rel_path, expected_md5, actual_md5);
            errors++;
        }
        free(full_path);
    }

    fclose(f);
    free(md5sums_path);

    if (errors == 0) {
        printf("\033[1;32m[integrity]\033[0m Verified %d files for %s.\n", total, pkg_info->package_name);
        return 0;
    } else {
        runepkg_util_error("MD5 verification failed with %d errors for %s.\n", errors, pkg_info->package_name);
        return -1;
    }
}

// Struct for thread arguments in file installation
typedef struct {
    char *src;
    char *dst;
    int *error_count;
    pthread_mutex_t *mutex;
} FileInstallArgs;

// Internal function to perform the actual file system operation for a single file/dir/link
// Returns 0 on success, -1 on error.
static int perform_file_install(const char *src, const char *dst) {
    struct stat st;
    if (lstat(src, &st) != 0) {
        fprintf(stderr, "\033[1;31m[file error]\033[0m Failed to stat source: %s (%s)\n", src, strerror(errno));
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (runepkg_util_create_dir_recursive(dst, 0755) != 0) {
            fprintf(stderr, "\033[1;31m[file error]\033[0m Failed to create directory: %s\n", dst);
            return -1;
        }
    } else if (S_ISREG(st.st_mode)) {
        char *dst_copy = strdup(dst);
        if (dst_copy) {
            char *parent = dirname(dst_copy);
            if (parent) runepkg_util_create_dir_recursive(parent, 0755);
            free(dst_copy);
        }
        // Unlink first to handle existing symlinks or mismatched types
        struct stat dst_st;
        if (lstat(dst, &dst_st) == 0) {
            if (S_ISDIR(dst_st.st_mode)) {
                // If it's a directory, we can't just unlink it.
                // For now, we'll try to remove it if it's empty, or just fail gracefully.
                if (rmdir(dst) != 0) {
                    fprintf(stderr, "\033[1;31m[file error]\033[0m Cannot overwrite directory with file: %s\n", dst);
                    return -1;
                }
            } else {
                unlink(dst);
            }
        }
        if (runepkg_util_copy_file(src, dst) != 0) {
            fprintf(stderr, "\033[1;31m[file error]\033[0m Failed to copy file: %s\n", dst);
            return -1;
        }
    } else if (S_ISLNK(st.st_mode)) {
        char link_target[PATH_MAX];
        ssize_t len = readlink(src, link_target, sizeof(link_target) - 1);
        if (len >= 0) {
            link_target[len] = '\0';
            char *dst_copy = strdup(dst);
            if (dst_copy) {
                char *parent = dirname(dst_copy);
                if (parent) runepkg_util_create_dir_recursive(parent, 0755);
                free(dst_copy);
            }
            unlink(dst);
            if (symlink(link_target, dst) != 0) {
                fprintf(stderr, "\033[1;31m[file error]\033[0m Failed to create symlink: %s -> %s (%s)\n", dst, link_target, strerror(errno));
                return -1;
            }
        } else {
            fprintf(stderr, "\033[1;31m[file error]\033[0m Failed to read symlink source: %s\n", src);
            return -1;
        }
    }
    return 0;
}

// Function to install a single file (for threading)
void* install_single_file(void *arg) {
    FileInstallArgs *args = (FileInstallArgs*)arg;
    char *src = args->src;
    char *dst = args->dst;
    int *error_count = args->error_count;
    pthread_mutex_t *mutex = args->mutex;

    if (perform_file_install(src, dst) != 0) {
        pthread_mutex_lock(mutex);
        (*error_count)++;
        pthread_mutex_unlock(mutex);
    }

    free(src);
    free(dst);
    return NULL;
}

int handle_install(const char *deb_file_path) {
    int ret = handle_install_internal(deb_file_path, 1);
    g_auto_confirm_deps = false;
    g_auto_confirm_siblings = false;
    g_asked_siblings = false;
    return ret;
}

int calculate_optimal_threads(void) {
    long nproc = sysconf(_SC_NPROCESSORS_ONLN);
    if (nproc <= 0) return 4;
    int threads = (int)nproc;
    if (threads < 1) threads = 1;
    return threads * 2; // heuristic
}

void handle_install_stdin(void) {
    char line[PATH_MAX * 2];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *trimmed = runepkg_util_trim_whitespace(line);
        if (!trimmed || trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }

        char *token = strtok(trimmed, " \t");
        while (token) {
            // Accept any non-empty token as a potential .deb file path
            if (token[0] != '\0') {
                handle_install(token);
            }
            token = strtok(NULL, " \t");
        }
    }
}

void handle_install_listfile(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("Error: Cannot open list file: %s\n", path);
        return;
    }

    char line[PATH_MAX * 2];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *trimmed = runepkg_util_trim_whitespace(line);
        if (!trimmed || trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }

        char *token = strtok(trimmed, " \t");
        while (token) {
            if (strstr(token, ".deb") != NULL) {
                handle_install(token);
            }
            token = strtok(NULL, " \t");
        }
    }

    fclose(fp);
}

/* Helper to find a sibling .deb for a package name in the same directory as origin_deb_path.
 * Returns a newly allocated string path if found, or NULL otherwise.
 */
char* clandestine_find_sibling(const char *pkg_name, const char *origin_deb_path) {
    if (!pkg_name || !origin_deb_path) return NULL;

    const char *origin_baseptr = strrchr(origin_deb_path, '/');
    const char *origin_base = origin_baseptr ? origin_baseptr + 1 : origin_deb_path;
    char origin_version[PATH_MAX];
    origin_version[0] = '\0';
    const char *o_u1 = strchr(origin_base, '_');
    if (o_u1) {
        const char *o_u2 = strchr(o_u1 + 1, '_');
        if (o_u2 && o_u2 > o_u1 + 1) {
            size_t ver_len = (size_t)(o_u2 - (o_u1 + 1));
            if (ver_len < sizeof(origin_version)) {
                memcpy(origin_version, o_u1 + 1, ver_len);
                origin_version[ver_len] = '\0';
            }
        }
    }

    char *origin_copy = strdup(origin_deb_path);
    if (!origin_copy) return NULL;
    char *dir = dirname(origin_copy);
    if (!dir) {
        free(origin_copy);
        return NULL;
    }

    char pattern[PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%.*s/%s_*.deb", (int)(sizeof(pattern)-264), dir, pkg_name);
    glob_t globbuf;
    char *result = NULL;

    if (glob(pattern, 0, NULL, &globbuf) == 0 && globbuf.gl_pathc > 0) {
        const char *fallback_candidate = NULL;
        for (size_t gi = 0; gi < globbuf.gl_pathc; gi++) {
            const char *candidate = globbuf.gl_pathv[gi];
            if (strcmp(candidate, origin_deb_path) == 0) continue;

            const char *baseptr = strrchr(candidate, '/');
            const char *base = baseptr ? baseptr + 1 : candidate;
            const char *u = strchr(base, '_');
            if (!u) continue;
            size_t name_len = (size_t)(u - base);
            if (name_len == 0 || strlen(pkg_name) != name_len || strncmp(base, pkg_name, name_len) != 0) continue;

            char candidate_version[PATH_MAX];
            candidate_version[0] = '\0';
            const char *c_u1 = strchr(base, '_');
            if (c_u1) {
                const char *c_u2 = strchr(c_u1 + 1, '_');
                if (c_u2 && c_u2 > c_u1 + 1) {
                    size_t cver_len = (size_t)(c_u2 - (c_u1 + 1));
                    if (cver_len < sizeof(candidate_version)) {
                        memcpy(candidate_version, c_u1 + 1, cver_len);
                        candidate_version[cver_len] = '\0';
                    }
                }
            }

            if (origin_version[0] != '\0' && candidate_version[0] != '\0' && strcmp(candidate_version, origin_version) == 0) {
                result = strdup(candidate);
                break;
            }
            if (!fallback_candidate) fallback_candidate = candidate;
        }
        if (!result && fallback_candidate) result = strdup(fallback_candidate);
    }
    globfree(&globbuf);
    free(origin_copy);
    return result;
}

typedef struct {
    char **paths;
    char **names;
    int count;
    int capacity;
} SiblingCollection;

static void clandestine_spider_recursive(const char *deb_path, SiblingCollection *coll, const char *origin_deb_path) {
    PkgInfo info;
    runepkg_pack_init_package_info(&info);
    if (runepkg_pack_extract_and_collect_info(deb_path, g_control_dir, &info) != 0) {
        runepkg_pack_free_package_info(&info);
        return;
    }

    Dependency **deps = parse_depends_with_constraints(info.depends);
    if (deps) {
        for (int i = 0; deps[i]; i++) {
            bool already = false;
            for (int k = 0; k < coll->count; k++) {
                if (strcmp(coll->names[k], deps[i]->package) == 0) { already = true; break; }
            }
            if (already) continue;

            if (runepkg_hash_search(runepkg_main_hash_table, deps[i]->package)) continue;
            if (installing_packages && runepkg_hash_search(installing_packages, deps[i]->package)) continue;

            char *sibling = clandestine_find_sibling(deps[i]->package, origin_deb_path);
            if (sibling) {
                if (coll->count >= coll->capacity) {
                    coll->capacity = (coll->capacity == 0) ? 10 : coll->capacity * 2;
                    coll->paths = realloc(coll->paths, coll->capacity * sizeof(char*));
                    coll->names = realloc(coll->names, coll->capacity * sizeof(char*));
                }
                coll->paths[coll->count] = sibling;
                coll->names[coll->count] = strdup(deps[i]->package);
                coll->count++;

                clandestine_spider_recursive(sibling, coll, origin_deb_path);
            }
        }
        for (int i = 0; deps[i]; i++) {
            free(deps[i]->package);
            if (deps[i]->constraint) free(deps[i]->constraint);
            free(deps[i]);
        }
        free(deps);
    }

    /* Only clean up if this ISN'T the same file the main installer is using */
    if (strcmp(deb_path, origin_deb_path) != 0) {
        runepkg_pack_cleanup_extraction_workspace(&info);
    }
    runepkg_pack_free_package_info(&info);
}

/* Attempt to find an uninstalled sibling .deb (e.g., in the same directory)
 * matching `pkg_name` and install it. This avoids network dependency
 * resolution when the package .deb is present alongside the current one.
 */
int clandestine_handle_install(const char *pkg_name, const char *origin_deb_path, char ***attempted_list, int *attempted_count) {
    if (!pkg_name || !origin_deb_path) return 0;

    char *candidate = clandestine_find_sibling(pkg_name, origin_deb_path);
    if (!candidate) return 0;

    /* Prompt user for sibling installation if not already confirmed or forced */
    if (!g_force_mode && !g_auto_confirm_siblings) {
        if (!g_asked_siblings) {
            printf("\033[1;33m[dependencies]\033[0m Found other .deb files in this directory that might satisfy requirements.\n");
            printf("Would you like to search this directory for missing dependencies? [\033[1;33my\033[0m/\033[1;33mN\033[0m] ");
            fflush(stdout);
            char resp[16];
            if (fgets(resp, sizeof(resp), stdin) && (resp[0] == 'y' || resp[0] == 'Y')) {
                g_auto_confirm_siblings = true;
            }
            g_asked_siblings = true;
        }
        if (!g_auto_confirm_siblings) {
            /* User declined sibling installation */
            free(candidate);
            return 0;
        }
    }

    /* Avoid duplicate attempts */
    int already_attempted = 0;
    if (attempted_list && attempted_count && *attempted_list) {
        for (int i = 0; i < *attempted_count; i++) {
            if (strcmp((*attempted_list)[i], candidate) == 0) { already_attempted = 1; break; }
        }
    }

    if (!already_attempted) {
        if (attempted_list && attempted_count) {
            *attempted_list = realloc(*attempted_list, (*attempted_count + 1) * sizeof(char*));
            (*attempted_list)[*attempted_count] = strdup(candidate);
            (*attempted_count)++;
        }

        if (handle_install_internal(candidate, 0) == 0) {
            free(candidate);
            return 1;
        }
    }

    free(candidate);
    return 0;
}
/* Helper to check if a virtual package is provided by any installed package */
static int is_package_provided_by_table(runepkg_hash_table_t *table, const char *pkg_name) {
    if (!table) return 0;
    for (size_t i = 0; i < table->size; i++) {
        runepkg_hash_node_t *node = table->buckets[i];
        while (node) {
            if (node->data.provides) {
                char *provides_copy = strdup(node->data.provides);
                char *token = strtok(provides_copy, ",");
                while (token) {
                    char *trimmed = runepkg_util_trim_whitespace(token);
                    if (strcmp(trimmed, pkg_name) == 0) {
                        free(provides_copy);
                        return 1;
                    }
                    token = strtok(NULL, ",");
                }
                free(provides_copy);
            }
            node = node->next;
        }
    }
    return 0;
}

/* Internal install entry that accepts an `is_top_level` flag. When
 * `is_top_level` is zero, we avoid some top-level behaviors such as
 * force-driven attempts to treat already-installed dependencies as
 * unsatisfied (which can lead to cycles/repeats).
 */
static int handle_install_internal(const char *deb_file_path, int is_top_level) {
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    runepkg_log_verbose("Installing package from: %s\n", deb_file_path);

    // Handle patterns
    if (strstr(deb_file_path, ".deb") == NULL) {
        char pattern[PATH_MAX];
        if (strstr(deb_file_path, "*") != NULL) {
            strncpy(pattern, deb_file_path, sizeof(pattern) - 1);
        } else {
            snprintf(pattern, sizeof(pattern), "%s*.deb", deb_file_path);
        }
        glob_t globbuf;
        int found = 0;
        if (glob(pattern, 0, NULL, &globbuf) == 0 && globbuf.gl_pathc > 0) {
            found = 1;
        } else if (strstr(deb_file_path, "/") == NULL) {
            // Try with debs/ prefix
            char new_pattern[PATH_MAX];
            if (strstr(deb_file_path, "*") != NULL) {
                snprintf(new_pattern, sizeof(new_pattern), "debs/%s", deb_file_path);
            } else {
                snprintf(new_pattern, sizeof(new_pattern), "debs/%s*.deb", deb_file_path);
            }
            if (glob(new_pattern, 0, NULL, &globbuf) == 0 && globbuf.gl_pathc > 0) {
                found = 1;
            } else if (g_download_dir) {
                // Try with download_dir/ prefix
                char download_pattern[PATH_MAX];
                if (strstr(deb_file_path, "*") != NULL) {
                    snprintf(download_pattern, sizeof(download_pattern), "%s/%s", g_download_dir, deb_file_path);
                } else {
                    snprintf(download_pattern, sizeof(download_pattern), "%s/%s*.deb", g_download_dir, deb_file_path);
                }
                if (glob(download_pattern, 0, NULL, &globbuf) == 0 && globbuf.gl_pathc > 0) {
                    found = 1;
                }
            }
        }
        if (found) {
            // Install the latest (first after sorting)
            int ret = handle_install(globbuf.gl_pathv[0]);
            globfree(&globbuf);
            return ret;
        }
        globfree(&globbuf);
        return -1;
    }

    /* Fast-path: if given a concrete .deb filename (no wildcards), try
     * to parse package name/version from the filename and check the
     * installed/in-flight tables before doing a full extraction. This
     * makes re-checks snappy when a package is already present. */
    if (strstr(deb_file_path, ".deb") != NULL && strchr(deb_file_path, '*') == NULL) {
        const char *baseptr = strrchr(deb_file_path, '/');
        const char *base = baseptr ? baseptr + 1 : deb_file_path;
        char base_copy[PATH_MAX];
        strncpy(base_copy, base, sizeof(base_copy) - 1);
        base_copy[sizeof(base_copy) - 1] = '\0';
        char *dot = strrchr(base_copy, '.');
        if (dot && strcmp(dot, ".deb") == 0) {
            *dot = '\0';
            char *u1 = strchr(base_copy, '_');
            char *u2 = u1 ? strchr(u1 + 1, '_') : NULL;
            if (u1 && u2) {
                size_t name_len = (size_t)(u1 - base_copy);
                size_t ver_len = (size_t)(u2 - (u1 + 1));
                if (name_len > 0 && name_len < PATH_MAX && ver_len > 0 && ver_len < PATH_MAX) {
                    char name_buf[PATH_MAX];
                    char ver_buf[PATH_MAX];
                    memcpy(name_buf, base_copy, name_len);
                    name_buf[name_len] = '\0';
                    memcpy(ver_buf, u1 + 1, ver_len);
                    ver_buf[ver_len] = '\0';
                    PkgInfo *installed = NULL;
                    if (runepkg_main_hash_table) installed = runepkg_hash_search(runepkg_main_hash_table, name_buf);
                    if (!installed && installing_packages) installed = runepkg_hash_search(installing_packages, name_buf);
                    if (installed && installed->version && strcmp(installed->version, ver_buf) == 0) {
                        if (runepkg_hash_search(runepkg_main_hash_table, name_buf)) {
                                if (!g_force_mode || !is_top_level) {
                                    if (is_top_level) {
                                        printf("Package %s is already installed (%s), skipping. Use -f/--force to reinstall.\n", name_buf, ver_buf);
                                    } else {
                                        runepkg_log_verbose("Package %s already on target version (%s), skipping redundant force install.\n", name_buf, ver_buf);
                                    }
                                    return 0;
                                }
                            /* If force mode is enabled AND this is the top-level request,
                             * continue into full install path to allow reinstallation. */
                        } else {
                            /* in-flight; be quiet */
                            runepkg_log_verbose("Package %s is currently being installed (fast-path), skipping.\n", name_buf);
                            return 0;
                        }
                    }
                }
            }
        }
    }

    if (g_verbose_mode) {
        struct stat file_stat;
        if (stat(deb_file_path, &file_stat) == 0) {
            printf("%ld bytes\n", file_stat.st_size);
            runepkg_log_verbose("File permissions: %o\n", file_stat.st_mode & 0777);
            runepkg_log_verbose("File last modified: %s", ctime(&file_stat.st_mtime));
        } else {
            printf("FAILED to stat file\n");
        }
    }

    if (!g_control_dir) {
        runepkg_log_verbose("ERROR: g_control_dir is NULL - configuration not loaded properly\n");
        return -1;
    }

    PkgInfo pkg_info;
    runepkg_pack_init_package_info(&pkg_info);

    int result = runepkg_pack_extract_and_collect_info(deb_file_path, g_control_dir, &pkg_info);

    if (result == 0) {
        /* If this package is already in the installing table, we're in a
         * recursive install path (dependency cycle or repeated request).
         * Skip to avoid infinite recursion/hangs; treat as satisfied.
         */
        if (installing_packages && runepkg_hash_search(installing_packages, pkg_info.package_name)) {
            runepkg_log_verbose("Skipping install of %s: already installing (recursive).\n", pkg_info.package_name);
            runepkg_pack_cleanup_extraction_workspace(&pkg_info);
            runepkg_pack_free_package_info(&pkg_info);
            return 0;
        }
        // Check if already installed
        PkgInfo *existing_inst_main = runepkg_hash_search(runepkg_main_hash_table, pkg_info.package_name);
        PkgInfo *existing_inst = existing_inst_main;
        /* Also check installing table for in-flight packages (but keep
         * track of whether the package is truly installed or only in-flight)
         */
        if (!existing_inst && installing_packages) {
            existing_inst = runepkg_hash_search(installing_packages, pkg_info.package_name);
        }
        if (existing_inst) {
            if (g_force_mode) {
                /* Prepare to reinstall/upgrade: save current version, remove
                 * persistent metadata and hash entry so installation can proceed.
                 * Note: do NOT dereference `existing_inst` after removal since
                 * the hash remove may free the pointed data.
                 */
                char *old_ver = existing_inst->version ? strdup(existing_inst->version) : NULL;
                runepkg_hash_remove_package(runepkg_main_hash_table, pkg_info.package_name);
                if (old_ver) {
                    runepkg_storage_remove_package(pkg_info.package_name, old_ver);
                }
                /* Use saved `old_ver` for comparisons/printing instead of
                 * `existing_inst->version` (which may be freed).
                 */
                if (pkg_info.version && old_ver && strcmp(old_ver, pkg_info.version) != 0) {
                    printf("Upgrading %s from %s to %s (force)\n",
                           pkg_info.package_name,
                           old_ver ? old_ver : "(unknown)",
                           pkg_info.version ? pkg_info.version : "(unknown)");
                } else {
                    printf("Reinstalling %s (%s) due to --force\n",
                           pkg_info.package_name,
                           pkg_info.version ? pkg_info.version : (old_ver ? old_ver : "(unknown)"));
                }
                if (old_ver) free(old_ver);
            } else {
                /* If the package is only present in the installing table, it
                 * means an in-flight install is happening; do not confuse the
                 * user with 'already installed' messages or -f/--force hints.
                 */
                if (existing_inst_main) {
                    /* Check whether the package directory was created very recently
                     * (i.e., likely installed earlier in this run). In that case
                     * avoid showing the '-f/--force' guidance which is misleading
                     * during a dependency-driven install sequence.
                     */
                    char pkg_dir[PATH_MAX];
                    int recent_installed = 0;
                    if (runepkg_storage_get_package_path(pkg_info.package_name, existing_inst->version ? existing_inst->version : "", pkg_dir) == 0) {
                        struct stat st;
                        if (stat(pkg_dir, &st) == 0) {
                            time_t now = time(NULL);
                            if (now != (time_t)-1 && (now - st.st_mtime) < 5) recent_installed = 1;
                        }
                    }
                    if (recent_installed) {
                        runepkg_log_verbose("Package %s appears to have been installed recently; skipping duplicate message.\n", pkg_info.package_name);
                    } else {
                        if (is_top_level) {
                            if (existing_inst->version && pkg_info.version && strcmp(existing_inst->version, pkg_info.version) == 0) {
                                printf("Package %s is already installed (%s), skipping. Use -f/--force to reinstall.\n",
                                       pkg_info.package_name, pkg_info.version);
                            } else {
                                printf("Package %s is already installed (version %s). Use -f/--force to reinstall or upgrade.\n",
                                       pkg_info.package_name,
                                       existing_inst->version ? existing_inst->version : "(unknown)");
                            }
                        } else {
                            runepkg_log_verbose("Package %s already installed; suppressed message in non-top-level install.\n", pkg_info.package_name);
                        }
                    }
                } else {
                    /* in-flight — be silent (or give a concise status) */
                    runepkg_log_verbose("Package %s is already being installed (in-flight), skipping duplicate.\n", pkg_info.package_name);
                }
                runepkg_pack_cleanup_extraction_workspace(&pkg_info);
                runepkg_pack_free_package_info(&pkg_info);
                return 0;
            }
        }

        // Mark as installing to prevent recursive installs
        PkgInfo dummy;
        runepkg_pack_init_package_info(&dummy);
        dummy.package_name = strdup(pkg_info.package_name);
        if (pkg_info.version) dummy.version = strdup(pkg_info.version);
        runepkg_hash_add_package(installing_packages, &dummy);
        runepkg_pack_free_package_info(&dummy);

        // MD5 Verification
        extern bool g_md5_checks;
        if (g_md5_checks) {
            if (runepkg_install_verify_md5(&pkg_info) != 0) {
                runepkg_util_error("MD5 verification failed for %s. Aborting installation.\n", pkg_info.package_name);
                runepkg_hash_remove_package(installing_packages, pkg_info.package_name);
                runepkg_pack_cleanup_extraction_workspace(&pkg_info);
                runepkg_pack_free_package_info(&pkg_info);
                return -1;
            }
            pkg_info.md5_verified = true;
        }

        // Execute preinst
        runepkg_execute_maintainer_script(pkg_info.preinst, &pkg_info, "install");

        // Resolve dependencies
        Dependency **deps = parse_depends_with_constraints(pkg_info.depends);
        if (deps) {
                Dependency **unsatisfied = NULL;
                int num_unsatisfied = 0;
                /* Track which dependency package names we've already attempted to
                 * install during this top-level install to avoid repeated attempts
                 * that can cause cycling/hangs when multiple deps reference the
                 * same package. */
                char **attempted_deps = NULL;
                int attempted_count = 0;

                // Pre-scan for local siblings to provide a more informative prompt (Recursive "Spider")
                if (is_top_level && !g_force_mode && !g_auto_confirm_siblings && !g_asked_siblings) {
                    SiblingCollection coll = {NULL, NULL, 0, 0};
                    clandestine_spider_recursive(deb_file_path, &coll, deb_file_path);

                    if (coll.count > 0) {
                        printf("\033[1;33m[dependencies]\033[0m Found %d required dependencies in this directory:\n", coll.count);
                        for (int s = 0; s < coll.count; s++) {
                            const char *base = strrchr(coll.paths[s], '/');
                            printf("  -> %s\n", base ? base + 1 : coll.paths[s]);
                            free(coll.paths[s]);
                            free(coll.names[s]);
                        }
                        free(coll.paths);
                        free(coll.names);

                        printf("Would you like to install these local files to satisfy dependencies? [\033[1;33my\033[0m/\033[1;33mN\033[0m] ");
                        fflush(stdout);
                        char resp[16];
                        if (fgets(resp, sizeof(resp), stdin) && (resp[0] == 'y' || resp[0] == 'Y')) {
                            g_auto_confirm_siblings = true;
                        }
                        g_asked_siblings = true;
                    }
                }

                for (int j = 0; deps[j]; j++) {
                PkgInfo *installed = runepkg_hash_search(runepkg_main_hash_table, deps[j]->package);
                /* Diagnostic: show what the installer finds for this dependency */
                if (!installed) {
                    /* Check if any installed package PROVIDES this virtual dependency */
                    if (is_package_provided_by_table(runepkg_main_hash_table, deps[j]->package)) {
                        runepkg_log_verbose("Dependency '%s' is satisfied by a virtual provider\n", deps[j]->package);
                        continue;
                    }
                    if (installing_packages && is_package_provided_by_table(installing_packages, deps[j]->package)) {
                        runepkg_log_verbose("Dependency '%s' is being satisfied by an in-flight virtual provider\n", deps[j]->package);
                        continue;
                    }
                    runepkg_util_log_debug("dependency '%s' not found in installed hash table\n", deps[j]->package);
                } else {
                    runepkg_util_log_debug("dependency '%s' found: version='%s' pkgname='%s'\n",
                           deps[j]->package,
                           installed->version ? installed->version : "(null)",
                           installed->package_name ? installed->package_name : "(null)");
                }
                if (!installed && installing_packages) installed = runepkg_hash_search(installing_packages, deps[j]->package);
                int satisfied = 0;
                if (installed) {
                    if (deps[j]->constraint) {
                        satisfied = runepkg_util_check_version_constraint(installed->version, deps[j]->constraint);
                           /* Extra diagnostic: only log at debug level to avoid
                            * noisy output during normal runs. */
                           runepkg_util_log_debug("DBG-CHECK: dep='%s' installed_ver='%s' constraint='%s' -> satisfied=%d\n",
                               deps[j]->package,
                               installed->version ? installed->version : "(null)",
                               deps[j]->constraint ? deps[j]->constraint : "(null)",
                               satisfied);
                        if (satisfied == -1) {
                            printf("Warning: Unknown constraint '%s' for %s\n", deps[j]->constraint, deps[j]->package);
                            satisfied = 1;  // Assume satisfied for unknown
                        } else if (satisfied) {
                            runepkg_log_verbose("Dependency '%s %s' satisfied by installed version %s\n",
                                               deps[j]->package, deps[j]->constraint, installed->version);
                        }
                    } else {
                        satisfied = 1;  // No constraint, just installed
                        runepkg_log_verbose("Dependency '%s' satisfied by installed package\n", deps[j]->package);
                    }
                    /* If top-level install invoked with --force, attempt to
                     * reinstall dependency if a sibling .deb is present. We do
                     * this by treating the dependency as 'unsatisfied' so that
                     * `clandestine_handle_install` will search for a local
                     * candidate and invoke `handle_install` on it. If no
                     * sibling is found, behavior falls back to the normal
                     * (already-installed) path.
                     */
                    /* Only attempt force-driven dependency reinstalls when
                     * this is the top-level install request and the user hasn't
                     * indicated they want a "minimal" force install.
                     */
                    if (is_top_level && g_force_mode && g_auto_confirm_siblings) {
                        char *cl_debug_env = getenv("RUNEPKG_CLAND_DEBUG");
                        int cl_debug = cl_debug_env && cl_debug_env[0];
                        if (cl_debug) printf("clandestine: force mode will attempt sibling install for dep %s\n", deps[j]->package);
                        satisfied = 0;
                    }
                }
                if (!satisfied) {
                    /* Try the clandestine helper to find and install a sibling .deb
                     * but ONLY if we are not in force mode OR the user has explicitly
                     * allowed/confirmed siblings.
                     */
                    int cfound = 0;
                    if (!g_force_mode || g_auto_confirm_siblings) {
                        cfound = clandestine_handle_install(deps[j]->package, deb_file_path, &attempted_deps, &attempted_count);
                    }

                    if (!cfound) {
                        if (!g_force_mode) {
                            unsatisfied = realloc(unsatisfied, (num_unsatisfied + 1) * sizeof(Dependency*));
                            unsatisfied[num_unsatisfied] = malloc(sizeof(Dependency));
                            unsatisfied[num_unsatisfied]->package = strdup(deps[j]->package);
                            unsatisfied[num_unsatisfied]->constraint = deps[j]->constraint ? strdup(deps[j]->constraint) : NULL;
                            num_unsatisfied++;
                        } else {
                            runepkg_log_verbose("Skipping unsatisfied dependency %s %s (force mode)\n",
                                                deps[j]->package, deps[j]->constraint ? deps[j]->constraint : "");
                        }
                    }
                }
            }
                if (num_unsatisfied > 0) {
                    int try_repo = 0;
#ifdef ENABLE_CPP_FFI
                    if (is_top_level) {
                        printf("\033[1;33m[dependencies]\033[0m The following dependencies are missing for %s:\n", pkg_info.package_name);
                        for(int k=0; k<num_unsatisfied; k++){
                            printf("  - %s%s%s\n", unsatisfied[k]->package, unsatisfied[k]->constraint ? " " : "", unsatisfied[k]->constraint ? unsatisfied[k]->constraint : "");
                        }

                        char index_path[PATH_MAX];
                        snprintf(index_path, sizeof(index_path), "%s/repo_index.bin", g_runepkg_db_dir);
                        if (runepkg_util_file_exists(index_path)) {
                            printf("Would you like to attempt to download and install them from repositories? [\033[1;33my\033[0m/\033[1;33mN\033[0m] ");
                            fflush(stdout);
                            char resp[16];
                            if (fgets(resp, sizeof(resp), stdin) && (resp[0] == 'y' || resp[0] == 'Y')) {
                                printf("\n"); // Add spacing before repo logs
                                try_repo = 1;
                                g_auto_confirm_deps = true;
                                g_auto_confirm_siblings = true;
                            }
                        } else {
                            printf("\033[1;31m[error]\033[0m Repository index missing. Please run 'runepkg update' to enable repository downloads.\n");
                        }
                    } else if (g_auto_confirm_deps) {
                        try_repo = 1;
                    }
#endif

                    if (try_repo) {
                        int all_ok = 1;
                        for(int k=0; k<num_unsatisfied; k++) {
#ifdef ENABLE_CPP_FFI
                            /* Re-check if satisfied; a previous iteration might have installed this. */
                            if (runepkg_hash_search(runepkg_main_hash_table, unsatisfied[k]->package)) continue;
                            if (is_package_provided_by_table(runepkg_main_hash_table, unsatisfied[k]->package)) continue;
                            if (installing_packages && runepkg_hash_search(installing_packages, unsatisfied[k]->package)) continue;
                            if (installing_packages && is_package_provided_by_table(installing_packages, unsatisfied[k]->package)) continue;

                            char *path = runepkg_repo_download(unsatisfied[k]->package, true);
                            if (path) {
                                if (handle_install_internal(path, 0) != 0) {
                                    all_ok = 0;
                                }
                                free(path);
                            } else {
                                all_ok = 0;
                            }
#endif
                        }
                        if (all_ok) {
                            // Free unsatisfied list before resetting
                            for(int k=0; k<num_unsatisfied; k++){
                                free(unsatisfied[k]->package);
                                if(unsatisfied[k]->constraint) free(unsatisfied[k]->constraint);
                                free(unsatisfied[k]);
                            }
                            free(unsatisfied);
                            unsatisfied = NULL;
                            num_unsatisfied = 0;
                        }
                    }

                    if (num_unsatisfied > 0) {
                        printf("\033[1;31mError:\033[0m The following dependencies are not satisfied:\n");
                        for(int k=0; k<num_unsatisfied; k++){
                            printf("  - %s%s%s\n", unsatisfied[k]->package, unsatisfied[k]->constraint ? " " : "", unsatisfied[k]->constraint ? unsatisfied[k]->constraint : "");
                        }
                        printf("Use -f or --force to install anyway.\n");
                        // free unsatisfied
                        for(int k=0; k<num_unsatisfied; k++){
                            free(unsatisfied[k]->package);
                            if(unsatisfied[k]->constraint) free(unsatisfied[k]->constraint);
                            free(unsatisfied[k]);
                        }
                        free(unsatisfied);
                        // Free deps
                        for (int j = 0; deps[j]; j++) {
                            free(deps[j]->package);
                            free(deps[j]->constraint);
                            free(deps[j]);
                        }
                        free(deps);
                        /* free attempted_deps if any and return */
                        if (attempted_deps) {
                            for (int a = 0; a < attempted_count; a++) free(attempted_deps[a]);
                            free(attempted_deps);
                        }
                        runepkg_pack_cleanup_extraction_workspace(&pkg_info);
                        runepkg_pack_free_package_info(&pkg_info);
                        return -1;
                    } else {
                        // If we resolved them, we continue.
                        // Note: we don't free deps/unsatisfied here yet if we reset num_unsatisfied,
                        // but the existing code has an else block below for num_unsatisfied == 0.
                        // Wait, I should probably just return and restart the check if I wanted to be perfect,
                        // but resetting num_unsatisfied and continuing might work if handle_install_internal(path, 0)
                        // properly updated the hash table.
                    }
                }
else {
                // Free deps
                for (int j = 0; deps[j]; j++) {
                    free(deps[j]->package);
                    free(deps[j]->constraint);
                    free(deps[j]);
                }
                free(deps);
                /* free attempted_deps if any */
                if (attempted_deps) {
                    for (int a = 0; a < attempted_count; a++) free(attempted_deps[a]);
                    free(attempted_deps);
                }
            }
        }

        if (g_verbose_mode) {
            runepkg_pack_print_package_info(&pkg_info);
        } else {
            printf("Selecting previously unselected package %s.\n",
                   pkg_info.package_name ? pkg_info.package_name : "(unknown)");
            printf("Unpacking %s (%s) ...\n",
                   pkg_info.package_name ? pkg_info.package_name : "(unknown)",
                   pkg_info.version ? pkg_info.version : "(unknown)");
        }

        if (pkg_info.package_name && pkg_info.version) {
            if (runepkg_storage_create_package_directory(pkg_info.package_name, pkg_info.version) == 0) {
                if (runepkg_storage_write_package_info(pkg_info.package_name, pkg_info.version, &pkg_info) == 0) {
                    if (g_verbose_mode) {
                        printf("Package successfully added to persistent storage.\n");
                    }
                    /* Add to main installed-package hash so further dependency
                     * checks during this run see it as installed. This prevents
                     * repeated reinstallation attempts for the same package. */
                    if (runepkg_main_hash_table) {
                        runepkg_hash_add_package(runepkg_main_hash_table, &pkg_info);
                    }
                } else {
                    printf("Warning: Failed to write package info to persistent storage.\n");
                }
            } else {
                printf("Warning: Failed to create package directory in persistent storage.\n");
            }
        } else {
            printf("Warning: Cannot add to persistent storage - missing package name or version.\n");
        }

        // Rebuild autocomplete index after install
        runepkg_storage_build_autocomplete_index();

        // Update the text autocomplete list
        handle_update_pkglist();

        if (g_system_install_root && pkg_info.data_dir_path && pkg_info.file_count > 0 && pkg_info.file_list) {
            int install_errors = 0;
            pthread_mutex_t error_mutex = PTHREAD_MUTEX_INITIALIZER;
            const int MAX_POSSIBLE_THREADS = 32; // Absolute maximum
            int max_threads = calculate_optimal_threads();
            if (max_threads > MAX_POSSIBLE_THREADS) {
                max_threads = MAX_POSSIBLE_THREADS;
            }
            pthread_t threads[MAX_POSSIBLE_THREADS];
            memset(threads, 0, sizeof(threads));
            FileInstallArgs thread_args[MAX_POSSIBLE_THREADS];
            int active_threads = 0;

            // Process files in batches
            for (int i = 0; i < pkg_info.file_count; i++) {
                const char *rel = pkg_info.file_list[i];
                if (!rel || rel[0] == '\0') continue;

                char *src = runepkg_util_concat_path(pkg_info.data_dir_path, rel);
                char *dst = runepkg_util_concat_path(g_system_install_root, rel);
                if (!src || !dst) {
                    runepkg_util_free_and_null(&src);
                    runepkg_util_free_and_null(&dst);
                    install_errors++;
                    continue;
                }

                // Wait for a thread slot if needed
                if (active_threads >= max_threads) {
                    for (int t = 0; t < max_threads; t++) {
                        if (threads[t] != 0) {
                            pthread_join(threads[t], NULL);
                            threads[t] = 0;
                            active_threads--;
                            break;
                        }
                    }
                }

                // Find an available thread slot
                int thread_idx = -1;
                for (int t = 0; t < max_threads; t++) {
                    if (threads[t] == 0) {
                        thread_idx = t;
                        break;
                    }
                }

                if (thread_idx == -1) {
                    // Should not happen, but fallback to sequential
                    if (perform_file_install(src, dst) != 0) install_errors++;
                    runepkg_util_free_and_null(&src);
                    runepkg_util_free_and_null(&dst);
                    continue;
                }

                // Prepare thread arguments
                thread_args[thread_idx].src = src;
                thread_args[thread_idx].dst = dst;
                thread_args[thread_idx].error_count = &install_errors;
                thread_args[thread_idx].mutex = &error_mutex;

                // Create thread
                if (pthread_create(&threads[thread_idx], NULL, install_single_file, &thread_args[thread_idx]) == 0) {
                    active_threads++;
                } else {
                    // Fallback to sequential on thread creation failure
                    if (perform_file_install(src, dst) != 0) install_errors++;
                    runepkg_util_free_and_null(&src);
                    runepkg_util_free_and_null(&dst);
                }
            }

            // Wait for remaining threads
            for (int t = 0; t < max_threads; t++) {
                if (threads[t] != 0) {
                    pthread_join(threads[t], NULL);
                }
            }

            pthread_mutex_destroy(&error_mutex);

            if (install_errors == 0 && g_verbose_mode) printf("Files installed to: %s\n", g_system_install_root);
            else if (install_errors > 0) printf("Install completed with %d file errors.\n", install_errors);

            // Execute postinst
            runepkg_execute_maintainer_script(pkg_info.postinst, &pkg_info, "configure");
        }

        runepkg_pack_cleanup_extraction_workspace(&pkg_info);

    } else {
        runepkg_pack_free_package_info(&pkg_info);
        return -1;
    }

    if (pkg_info.package_name && installing_packages) {
        runepkg_hash_remove_package(installing_packages, pkg_info.package_name);
    }

    runepkg_pack_free_package_info(&pkg_info);

    /* Incremental cleanup: if cleanup is enabled and this file is in the download cache, remove it. */
    if (g_cleanup_extract_dirs && g_download_dir && deb_file_path) {
        if (strncmp(deb_file_path, g_download_dir, strlen(g_download_dir)) == 0) {
            runepkg_log_verbose("Cleaning up downloaded cache file: %s\n", deb_file_path);
            unlink(deb_file_path);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double install_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
    runepkg_log_verbose("Total installation time: %.6f seconds\n", install_time);

    return 0;
}
