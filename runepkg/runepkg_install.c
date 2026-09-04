/*****************************************************************************
 * Filename:    runepkg_install.c
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Package installation engine for runepkg
 * LICENSE:     GPL v3
 ******************************************************************************/

#include "runepkg_portable.h"
#include "runepkg_defensive.h"
#include "runepkg_state.h"
#include "runepkg_install.h"
#include "runepkg_config.h"
#include "runepkg_util.h"
#include "runepkg_pack.h"
#include "runepkg_hash.h"
#include "runepkg_storage.h"
#include "runepkg_crypto.h"
#include "runepkg_handle.h"
#include "runepkg_md5sums.h"
#include "runepkg_host.h"

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
    char *script_name;
    int ret;

    if (!script_path || !runepkg_util_file_exists(script_path)) return 0;

    /* Smart check: Only execute scripts if we are installing to the actual system root.
     * In LFS/ISO creation scenarios (alternate root), we skip them to avoid breakage. */
    if (!g_system_install_root || strcmp(g_system_install_root, "/") != 0) {
        script_name = basename((char*)script_path);
        printf("\033[1;33m[warning]\033[0m Non-root: skipping %s for %s\n",
               script_name, pkg_info->package_name);
        return 0;
    }

    script_name = basename((char*)script_path);
    printf("\033[1;33m[warning]\033[0m Running %s for %s (%s)...\n", script_name, pkg_info->package_name, action);
    runepkg_util_log_verbose("Executing maintainer script %s for %s (%s)\n", script_name, pkg_info->package_name, action);

    /* Set environment variables */
    setenv("DPKG_MAINTSCRIPT_PACKAGE", pkg_info->package_name, 1);
    setenv("DPKG_MAINTSCRIPT_NAME", script_name, 1);
    if (pkg_info->architecture) setenv("DPKG_MAINTSCRIPT_ARCH", pkg_info->architecture, 1);
    if (pkg_info->version) setenv("DPKG_MAINTSCRIPT_VERSION", pkg_info->version, 1);

    /* Set DPKG_ROOT if we are not installing to actual system root */
    if (g_system_install_root && strcmp(g_system_install_root, "/") != 0) {
        setenv("DPKG_ROOT", g_system_install_root, 1);
        runepkg_util_log_verbose("Set DPKG_ROOT=%s for relocatable script execution\n", g_system_install_root);
    }

    {
        char *argv[4];
        argv[0] = (char*)"sh";
        argv[1] = (char*)script_path;
        argv[2] = (char*)action;
        argv[3] = NULL;
        ret = runepkg_util_execute_command("/bin/sh", argv);
    }

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
    char *md5sums_path;
    FILE *f;
    char *line = NULL;
    size_t line_cap = 0;
    int errors = 0;

    if (!pkg_info || !pkg_info->control_dir_path || !pkg_info->data_dir_path) return -1;

    md5sums_path = runepkg_util_concat_path(pkg_info->control_dir_path, "md5sums");

    if (!runepkg_util_file_exists(md5sums_path)) {
        runepkg_util_log_verbose("No md5sums file found for %s, skipping verification.\n", pkg_info->package_name);
        free(md5sums_path);
        return 0;
    }

    f = fopen(md5sums_path, "r");
    if (!f) {
        free(md5sums_path);
        return -1;
    }

    runepkg_util_log_verbose("Verifying MD5 checksums for %s...\n", pkg_info->package_name);

    line = malloc(1024);
    line_cap = 1024;
    while (fgets(line, (int)line_cap, f) != NULL) {
        char expected_md5[33];
        char *rel_path;
        char *end;
        char *full_path;
        struct stat st;
        char actual_md5[33];

        /* Remove trailing newline */
        line[strcspn(line, "\r\n")] = 0;

        if (strlen(line) < 35) continue;

        runepkg_util_safe_strncpy(expected_md5, line, 33);
        expected_md5[32] = '\0';

        /* Path starts after hash and two spaces (usually) */
        rel_path = line + 32;
        while (*rel_path == ' ' || *rel_path == '\t' || *rel_path == '*') rel_path++;

        if (*rel_path == '\0') continue;

        /* Trim trailing whitespace and potential slashes from the path */
        end = rel_path + strlen(rel_path) - 1;
        while (end >= rel_path && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n' || *end == '/')) {
            *end = '\0';
            end--;
        }

        if (*rel_path == '\0') continue;

        /* Skip leading ./ if present */
        if (rel_path[0] == '.' && rel_path[1] == '/') rel_path += 2;

        if (*rel_path == '\0') continue;

        full_path = runepkg_util_concat_path(pkg_info->data_dir_path, rel_path);

        /* Skip non-regular files: MD5 checksums in Debian packages only apply to regular files. */
        if (lstat(full_path, &st) == 0 && !S_ISREG(st.st_mode)) {
            free(full_path);
            continue;
        }

        if (runepkg_md5_file(full_path, actual_md5) != 0) {
            /* Check if file exists; if it doesn't, it's a real integrity error. */
            if (lstat(full_path, &st) == 0 && !S_ISREG(st.st_mode)) {
                free(full_path);
                continue;
            }
            perror("MD5 computation error");
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

    free(line);
    fclose(f);
    free(md5sums_path);

    if (errors == 0) {
        printf("\033[1;34m[md5sums]\033[0m \033[1;32mPassed\033[0m for %s\n",
               pkg_info->package_name);
        return 0;
    } else {
        runepkg_util_error("MD5 verification failed with %d errors for %s.\n", errors, pkg_info->package_name);
        return -1;
    }
}

/* Struct for thread arguments in file installation */
typedef struct {
    char *src;
    char *dst;
    int *error_count;
    pthread_mutex_t *mutex;
} FileInstallArgs;

/* Internal function to perform the actual file system operation for a single file/dir/link
 * Returns 0 on success, -1 on error. */
static int perform_file_install(const char *src, const char *dst) {
    extern char *g_system_install_root;
    struct stat st;

    /* Security Check: Ensure destination is within system install root */
    if (g_system_install_root && runepkg_util_is_path_under_dir(dst, g_system_install_root) == 0) {
        runepkg_util_error("Security: Attempted to install file outside root: %s\n", dst);
        return -1;
    }

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
        {
            struct stat dst_st;
            if (lstat(dst, &dst_st) == 0) {
                if (S_ISDIR(dst_st.st_mode)) {
                    if (rmdir(dst) != 0) {
                        fprintf(stderr, "\033[1;31m[file error]\033[0m Cannot overwrite directory with file: %s\n", dst);
                        return -1;
                    }
                } else {
                    unlink(dst);
                }
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

            /* Security: Block absolute symlinks unless g_system_install_root is "/"
             * User preference: Change to a warning in non-root mode instead of a fatal error. */
            if (link_target[0] == '/' && g_system_install_root && strcmp(g_system_install_root, "/") != 0) {
                printf("\033[1;33m[warning]\033[0m Non-root: absolute symlink: %s -> %s\n", dst, link_target);
            } else if (strstr(link_target, "..")) {
                runepkg_util_log_verbose("Warning: Relative symlink contains '..': %s -> %s\n", dst, link_target);
            }

            {
                char *dst_copy = strdup(dst);
                if (dst_copy) {
                    char *parent = dirname(dst_copy);
                    if (parent) runepkg_util_create_dir_recursive(parent, 0755);
                    free(dst_copy);
                }
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

/* Function to install a single file (for threading) */
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
    TransactionContext tx_ctx;
    int ret;

    if (runepkg_fsm_init(&tx_ctx, deb_file_path ? deb_file_path : "unknown", "1.0") == 0) {
        step_prepare(&tx_ctx);
        runepkg_fsm_transition(&tx_ctx, RUNEPKG_STATE_STAGING);
    }

    ret = handle_install_internal(deb_file_path, 1);

    if (ret == 0) {
        runepkg_fsm_transition(&tx_ctx, RUNEPKG_STATE_COMMITTING);
        step_commit(&tx_ctx);
        runepkg_fsm_transition(&tx_ctx, RUNEPKG_STATE_CLEANUP);
        step_cleanup(&tx_ctx);
    } else {
        runepkg_fsm_transition(&tx_ctx, RUNEPKG_STATE_ROLLBACK);
        runepkg_log_fail("Package installation failed", tx_ctx.log_dir);
        step_rollback(&tx_ctx);
        step_cleanup(&tx_ctx);
    }

    g_auto_confirm_deps = false;
    g_auto_confirm_siblings = false;
    g_asked_siblings = false;
    return ret;
}

int runepkg_install_batch_item(const char *deb_file_path) {
    return handle_install_internal(deb_file_path, 0);
}

int calculate_optimal_threads(void) {
    long nproc = sysconf(_SC_NPROCESSORS_ONLN);
    int threads;
    if (nproc <= 0) return 4;
    threads = (int)nproc;
    if (threads < 1) threads = 1;
    return threads * 2; /* heuristic */
}

void handle_install_stdin(void) {
    char line[PATH_MAX * 2];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *trimmed = runepkg_util_trim_whitespace(line);
        char *token;
        if (!trimmed || trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }

        token = strtok(trimmed, " \t");
        while (token) {
            /* Accept any non-empty token as a potential .deb file path */
            if (token[0] != '\0') {
                handle_install(token);
            }
            token = strtok(NULL, " \t");
        }
    }
}

void handle_install_listfile(const char *path) {
    FILE *fp = fopen(path, "r");
    char line[PATH_MAX * 2];

    if (!fp) {
        printf("Error: Cannot open list file: %s\n", path);
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *trimmed = runepkg_util_trim_whitespace(line);
        char *token;
        if (!trimmed || trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }

        token = strtok(trimmed, " \t");
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
 * Returns a newly allocated string path if found, or NULL otherwise. */
char* clandestine_find_sibling(const char *pkg_name, const char *origin_deb_path) {
    const char *origin_baseptr;
    const char *origin_base;
    char origin_version[PATH_MAX];
    const char *o_u1;
    char *origin_copy;
    char *dir;
    char pattern[PATH_MAX];
    glob_t globbuf;
    char *result = NULL;

    if (!pkg_name || !origin_deb_path) return NULL;

    origin_baseptr = strrchr(origin_deb_path, '/');
    origin_base = origin_baseptr ? origin_baseptr + 1 : origin_deb_path;
    origin_version[0] = '\0';
    o_u1 = strchr(origin_base, '_');
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

    origin_copy = strdup(origin_deb_path);
    if (!origin_copy) return NULL;
    dir = dirname(origin_copy);
    if (!dir) {
        free(origin_copy);
        return NULL;
    }

    snprintf(pattern, sizeof(pattern), "%.*s/%s_*.deb", (int)(sizeof(pattern)-264), dir, pkg_name);

    if (glob(pattern, 0, NULL, &globbuf) == 0 && globbuf.gl_pathc > 0) {
        const char *fallback_candidate = NULL;
        size_t gi;
        for (gi = 0; gi < globbuf.gl_pathc; gi++) {
            const char *candidate = globbuf.gl_pathv[gi];
            const char *baseptr;
            const char *base;
            const char *u;
            size_t name_len;
            char candidate_version[PATH_MAX];
            const char *c_u1;

            if (strcmp(candidate, origin_deb_path) == 0) continue;

            baseptr = strrchr(candidate, '/');
            base = baseptr ? baseptr + 1 : candidate;
            u = strchr(base, '_');
            if (!u) continue;
            name_len = (size_t)(u - base);
            if (name_len == 0 || strlen(pkg_name) != name_len || strncmp(base, pkg_name, name_len) != 0) continue;

            candidate_version[0] = '\0';
            c_u1 = strchr(base, '_');
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
    Dependency **deps;

    runepkg_pack_init_package_info(&info);
    if (runepkg_pack_extract_and_collect_info(deb_path, g_control_dir, &info) != 0) {
        runepkg_pack_free_package_info(&info);
        return;
    }

    deps = parse_depends_with_constraints(info.depends);
    if (deps) {
        int i;
        for (i = 0; deps[i]; i++) {
            bool already = false;
            int k;
            char *sibling;

            for (k = 0; k < coll->count; k++) {
                if (strcmp(coll->names[k], deps[i]->package) == 0) { already = true; break; }
            }
            if (already) continue;

            if (runepkg_hash_search(runepkg_main_hash_table, deps[i]->package)) continue;
            if (installing_packages && runepkg_hash_search(installing_packages, deps[i]->package)) continue;

            sibling = clandestine_find_sibling(deps[i]->package, origin_deb_path);
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
        for (i = 0; deps[i]; i++) {
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

int clandestine_handle_install(const char *pkg_name, const char *origin_deb_path, char ***attempted_list, int *attempted_count) {
    char *candidate;

    if (!pkg_name || !origin_deb_path) return 0;

    candidate = clandestine_find_sibling(pkg_name, origin_deb_path);
    if (!candidate) return 0;

    /* Prompt user for sibling installation if not already confirmed or forced */
    if (!g_force_mode && !g_auto_confirm_siblings) {
        if (!g_asked_siblings) {
            printf("\033[1;33m[dependencies]\033[0m Found other .deb files in this directory that might satisfy requirements.\n");
            printf("Would you like to search this directory for missing dependencies? [\033[1;33my\033[0m/\033[1;33mN\033[0m] ");
            fflush(stdout);
            {
                char resp[16];
                if (fgets(resp, sizeof(resp), stdin) && (resp[0] == 'y' || resp[0] == 'Y')) {
                    g_auto_confirm_siblings = true;
                }
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
    {
        int already_attempted = 0;
        if (attempted_list && attempted_count && *attempted_list) {
            int i;
            for (i = 0; i < *attempted_count; i++) {
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
    }

    free(candidate);
    return 0;
}

static int handle_install_internal(const char *deb_file_path, int is_top_level) {
    struct timespec start_time, end_time;
    PkgInfo pkg_info;
    int result;
    double install_time;

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    runepkg_log_verbose("Installing package from: %s\n", deb_file_path);

    /* Handle patterns */
    if (strstr(deb_file_path, ".deb") == NULL) {
        char pattern[PATH_MAX];
        glob_t globbuf;
        int found = 0;

        if (strstr(deb_file_path, "*") != NULL) {
            runepkg_secure_strcpy(pattern, sizeof(pattern), deb_file_path);
        } else {
            snprintf(pattern, sizeof(pattern), "%s*.deb", deb_file_path);
        }

        if (glob(pattern, 0, NULL, &globbuf) == 0 && globbuf.gl_pathc > 0) {
            found = 1;
        } else if (strstr(deb_file_path, "/") == NULL) {
            /* Try with debs/ prefix */
            char new_pattern[PATH_MAX];
            if (strstr(deb_file_path, "*") != NULL) {
                snprintf(new_pattern, sizeof(new_pattern), "debs/%s", deb_file_path);
            } else {
                snprintf(new_pattern, sizeof(new_pattern), "debs/%s*.deb", deb_file_path);
            }
            if (glob(new_pattern, 0, NULL, &globbuf) == 0 && globbuf.gl_pathc > 0) {
                found = 1;
            } else if (g_download_dir) {
                /* Try with download_dir/ prefix */
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
            /* Install the latest (first after sorting) */
            int ret = handle_install(globbuf.gl_pathv[0]);
            globfree(&globbuf);
            return ret;
        }
        globfree(&globbuf);
        return -1;
    }

    /* Fast-path: if given a concrete .deb filename (no wildcards) */
    if (strstr(deb_file_path, ".deb") != NULL && strchr(deb_file_path, '*') == NULL) {
        const char *baseptr = strrchr(deb_file_path, '/');
        const char *base = baseptr ? baseptr + 1 : deb_file_path;
        char base_copy[PATH_MAX];
        runepkg_secure_strcpy(base_copy, sizeof(base_copy), base);
        {
            char *dot = strrchr(base_copy, '.');
            if (dot && strcmp(dot, ".deb") == 0) {
                *dot = '\0';
                {
                    char *u1 = strchr(base_copy, '_');
                    char *u2 = u1 ? strchr(u1 + 1, '_') : NULL;
                    if (u1 && u2) {
                        size_t name_len = (size_t)(u1 - base_copy);
                        size_t ver_len = (size_t)(u2 - (u1 + 1));
                        if (name_len > 0 && name_len < PATH_MAX && ver_len > 0 && ver_len < PATH_MAX) {
                            char name_buf[PATH_MAX];
                            char ver_buf[PATH_MAX];
                            PkgInfo *installed = NULL;

                            memcpy(name_buf, base_copy, name_len);
                            name_buf[name_len] = '\0';
                            memcpy(ver_buf, u1 + 1, ver_len);
                            ver_buf[ver_len] = '\0';

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
                                } else {
                                    runepkg_log_verbose("Package %s is currently being installed (fast-path), skipping.\n", name_buf);
                                    return 0;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (g_verbose_mode) {
        struct stat file_stat;
        if (stat(deb_file_path, &file_stat) == 0) {
            printf("%ld bytes\n", (long)file_stat.st_size);
            runepkg_log_verbose("File permissions: %o\n", (unsigned int)(file_stat.st_mode & 0777));
            runepkg_log_verbose("File last modified: %s", ctime(&file_stat.st_mtime));
        } else {
            printf("FAILED to stat file\n");
        }
    }

    if (!g_control_dir) {
        runepkg_log_verbose("ERROR: g_control_dir is NULL - configuration not loaded properly\n");
        return -1;
    }

    runepkg_pack_init_package_info(&pkg_info);

    result = runepkg_pack_extract_and_collect_info(deb_file_path, g_control_dir, &pkg_info);

    if (result == 0) {
        PkgInfo *existing_inst_main;
        PkgInfo *existing_inst;

        if (installing_packages && runepkg_hash_search(installing_packages, pkg_info.package_name)) {
            runepkg_log_verbose("Skipping install of %s: already installing (recursive).\n", pkg_info.package_name);
            runepkg_pack_cleanup_extraction_workspace(&pkg_info);
            runepkg_pack_free_package_info(&pkg_info);
            return 0;
        }

        existing_inst_main = runepkg_hash_search(runepkg_main_hash_table, pkg_info.package_name);
        existing_inst = existing_inst_main;

        if (!existing_inst && installing_packages) {
            existing_inst = runepkg_hash_search(installing_packages, pkg_info.package_name);
        }
        if (existing_inst) {
            if (g_force_mode) {
                char *old_ver = existing_inst->version ? strdup(existing_inst->version) : NULL;
                runepkg_hash_remove_package(runepkg_main_hash_table, pkg_info.package_name);
                if (old_ver) {
                    runepkg_storage_remove_package(pkg_info.package_name, old_ver);
                }
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
                if (existing_inst_main) {
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
                    runepkg_log_verbose("Package %s is already being installed (in-flight), skipping duplicate.\n", pkg_info.package_name);
                }
                runepkg_pack_cleanup_extraction_workspace(&pkg_info);
                runepkg_pack_free_package_info(&pkg_info);
                return 0;
            }
        }

        {
            PkgInfo dummy;
            runepkg_pack_init_package_info(&dummy);
            dummy.package_name = strdup(pkg_info.package_name);
            if (pkg_info.version) dummy.version = strdup(pkg_info.version);
            runepkg_hash_add_package(installing_packages, &dummy);
            runepkg_pack_free_package_info(&dummy);
        }

        if (runepkg_crypto_is_enabled()) {
            char sig_path[PATH_MAX];
            snprintf(sig_path, sizeof(sig_path), "%s.sig", deb_file_path);
            if (runepkg_util_file_exists(sig_path)) {
                if (runepkg_crypto_verify_file(deb_file_path, sig_path) != 0) {
                    printf("\033[1;31m[error]\033[0m Failed gpg_sig for %s-%s\n",
                           pkg_info.package_name, pkg_info.version ? pkg_info.version : "0");
                    printf("\033[1;31m[error]\033[0m Aborting installation %s-%s\n",
                           pkg_info.package_name, pkg_info.version ? pkg_info.version : "0");
                    runepkg_hash_remove_package(installing_packages, pkg_info.package_name);
                    runepkg_pack_cleanup_extraction_workspace(&pkg_info);
                    runepkg_pack_free_package_info(&pkg_info);
                    return -1;
                }
            } else {
                printf("\033[1;34m[gpg_sig]\033[0m \033[1;33mSkipped\033[0m for %s (no signature found)\n", pkg_info.package_name);
            }
        }

        {
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
        }

        runepkg_execute_maintainer_script(pkg_info.preinst, &pkg_info, "install");

        {
            Dependency **deps = parse_depends_with_constraints(pkg_info.depends);
            Dependency **pre_deps = parse_depends_with_constraints(pkg_info.pre_depends);
            Dependency **unsatisfied = NULL;
            int num_unsatisfied = 0;
            char **attempted_deps = NULL;
            int attempted_count = 0;
            int j, pass;

            if (is_top_level && !g_force_mode && !g_auto_confirm_siblings && !g_asked_siblings && !g_batch_mode) {
                SiblingCollection coll;
                coll.paths = NULL; coll.names = NULL; coll.count = 0; coll.capacity = 0;
                clandestine_spider_recursive(deb_file_path, &coll, deb_file_path);

                if (coll.count > 0) {
                    int s;
                    printf("\033[1;33m[clandestine]\033[0m Found %d required dependencies in this directory.\n", coll.count);
                    printf("\033[1;34m[ritual]\033[0m runepkg prefers using these local files over network downloads.\n\n");
                    for (s = 0; s < coll.count; s++) {
                        const char *base = strrchr(coll.paths[s], '/');
                        printf("  -> %s\n", base ? base + 1 : coll.paths[s]);
                        free(coll.paths[s]);
                        free(coll.names[s]);
                    }
                    free(coll.paths);
                    free(coll.names);

                    printf("\nWould you like to proceed with the local install to satisfy dependencies? [\033[1;33my\033[0m/\033[1;33mN\033[0m] ");
                    fflush(stdout);
                    {
                        char resp[16];
                        if (fgets(resp, sizeof(resp), stdin) && (resp[0] == 'y' || resp[0] == 'Y')) {
                            g_auto_confirm_siblings = true;
                        }
                    }
                    g_asked_siblings = true;
                }
            }

            /* Loop over both Pre-Depends and Depends */
            for (pass = 0; pass < 2; pass++) {
                Dependency **curr_deps = (pass == 0) ? pre_deps : deps;
                if (!curr_deps) continue;

                for (j = 0; curr_deps[j]; j++) {
                    PkgInfo *installed = runepkg_hash_search(runepkg_main_hash_table, curr_deps[j]->package);
                    int satisfied = 0;

                    if (!installed && g_batch_planned_packages) {
                        installed = runepkg_hash_search(g_batch_planned_packages, curr_deps[j]->package);
                    }

                    if (!installed) {
                        if (is_package_provided_by_table(runepkg_main_hash_table, curr_deps[j]->package)) {
                            runepkg_log_verbose("Dependency '%s' is satisfied by a virtual provider\n", curr_deps[j]->package);
                            continue;
                        }
                        if (installing_packages && is_package_provided_by_table(installing_packages, curr_deps[j]->package)) {
                            runepkg_log_verbose("Dependency '%s' is being satisfied by an in-flight virtual provider\n", curr_deps[j]->package);
                            continue;
                        }
                        if (g_batch_planned_packages && is_package_provided_by_table(g_batch_planned_packages, curr_deps[j]->package)) {
                            runepkg_log_verbose("Dependency '%s' will be satisfied by a planned virtual provider\n", curr_deps[j]->package);
                            continue;
                        }
                    }

                    if (!installed && installing_packages) installed = runepkg_hash_search(installing_packages, curr_deps[j]->package);

                    if (installed) {
                        if (curr_deps[j]->constraint) {
                            satisfied = runepkg_util_check_version_constraint(installed->version, curr_deps[j]->constraint);
                            if (satisfied == -1) {
                                printf("Warning: Unknown constraint '%s' for %s\n", curr_deps[j]->constraint, curr_deps[j]->package);
                                satisfied = 1;
                            }
                        } else {
                            satisfied = 1;
                        }
                        if (is_top_level && g_force_mode && g_auto_confirm_siblings) {
                            satisfied = 0;
                        }
                    }
                    if (!satisfied) {
                        int cfound = 0;
                        if (!g_batch_mode && (!g_force_mode || g_auto_confirm_siblings)) {
                            cfound = clandestine_handle_install(curr_deps[j]->package, deb_file_path, &attempted_deps, &attempted_count);
                        }

                        if (!cfound) {
                            if (!g_force_mode) {
                                unsatisfied = realloc(unsatisfied, (num_unsatisfied + 1) * sizeof(Dependency*));
                                unsatisfied[num_unsatisfied] = malloc(sizeof(Dependency));
                                unsatisfied[num_unsatisfied]->package = strdup(curr_deps[j]->package);
                                unsatisfied[num_unsatisfied]->constraint = curr_deps[j]->constraint ? strdup(curr_deps[j]->constraint) : NULL;
                                num_unsatisfied++;
                            }
                        }
                    }
                }
            }

            if (num_unsatisfied > 0) {
                int try_repo_flag = 0;
#ifdef ENABLE_CPP_FFI
                if (is_top_level && !g_batch_mode) {
                    int k;
                    printf("\033[1;33m[dependencies]\033[0m The following dependencies are missing for %s:\n", pkg_info.package_name);
                    for(k=0; k<num_unsatisfied; k++){
                        printf("  - %s%s%s\n", unsatisfied[k]->package, unsatisfied[k]->constraint ? " " : "", unsatisfied[k]->constraint ? unsatisfied[k]->constraint : "");
                    }

                    {
                        char index_path[PATH_MAX];
                        snprintf(index_path, sizeof(index_path), "%s/repo_index.bin", g_runepkg_db_dir);
                        if (runepkg_util_file_exists(index_path)) {
                            printf("Would you like to attempt to download and install them from repositories? [\033[1;33my\033[0m/\033[1;33mN\033[0m] ");
                            fflush(stdout);
                            {
                                char resp[16];
                                if (fgets(resp, sizeof(resp), stdin) && (resp[0] == 'y' || resp[0] == 'Y')) {
                                    printf("\n");
                                    try_repo_flag = 1;
                                    g_auto_confirm_deps = true;
                                    g_auto_confirm_siblings = true;
                                }
                            }
                        } else {
                            printf("\033[1;31m[error]\033[0m Repository index missing. Please run 'runepkg update' to enable repository downloads.\n");
                        }
                    }
                } else if (g_auto_confirm_deps) {
                    try_repo_flag = 1;
                }
#endif

                if (try_repo_flag) {
                    int all_ok = 1;
                    int k;
                    for(k=0; k<num_unsatisfied; k++) {
#ifdef ENABLE_CPP_FFI
                        if (runepkg_hash_search(runepkg_main_hash_table, unsatisfied[k]->package)) continue;
                        if (is_package_provided_by_table(runepkg_main_hash_table, unsatisfied[k]->package)) continue;
                        if (installing_packages && runepkg_hash_search(installing_packages, unsatisfied[k]->package)) continue;

                        {
                            char *path = runepkg_repo_download(unsatisfied[k]->package, true);
                            if (path) {
                                if (handle_install_internal(path, 0) != 0) all_ok = 0;
                                free(path);
                            } else all_ok = 0;
                        }
#endif
                    }
                    if (all_ok) {
                        for(k=0; k<num_unsatisfied; k++){
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
                    int k;
                    printf("\033[1;31mError:\033[0m The following dependencies are not satisfied:\n");
                    for(k=0; k<num_unsatisfied; k++){
                        printf("  - %s%s%s\n", unsatisfied[k]->package, unsatisfied[k]->constraint ? " " : "", unsatisfied[k]->constraint ? unsatisfied[k]->constraint : "");
                    }
                    printf("Use -f or --force to install anyway.\n");
                    for(k=0; k<num_unsatisfied; k++){
                        free(unsatisfied[k]->package);
                        if(unsatisfied[k]->constraint) free(unsatisfied[k]->constraint);
                        free(unsatisfied[k]);
                    }
                    free(unsatisfied);
                    if (deps) {
                        for (j = 0; deps[j]; j++) {
                            free(deps[j]->package);
                            free(deps[j]->constraint);
                            free(deps[j]);
                        }
                        free(deps);
                    }
                    if (pre_deps) {
                        for (j = 0; pre_deps[j]; j++) {
                            free(pre_deps[j]->package);
                            free(pre_deps[j]->constraint);
                            free(pre_deps[j]);
                        }
                        free(pre_deps);
                    }
                    if (attempted_deps) {
                        int a;
                        for (a = 0; a < attempted_count; a++) free(attempted_deps[a]);
                        free(attempted_deps);
                    }
                    runepkg_pack_cleanup_extraction_workspace(&pkg_info);
                    runepkg_pack_free_package_info(&pkg_info);
                    return -1;
                }
            } else {
                if (deps) {
                    for (j = 0; deps[j]; j++) {
                        free(deps[j]->package);
                        free(deps[j]->constraint);
                        free(deps[j]);
                    }
                    free(deps);
                }
                if (pre_deps) {
                    for (j = 0; pre_deps[j]; j++) {
                        free(pre_deps[j]->package);
                        free(pre_deps[j]->constraint);
                        free(pre_deps[j]);
                    }
                    free(pre_deps);
                }
                if (attempted_deps) {
                    int a;
                    for (a = 0; a < attempted_count; a++) free(attempted_deps[a]);
                    free(attempted_deps);
                }
            }
        }

        if (g_verbose_mode) {
            runepkg_pack_print_package_info(&pkg_info);
        } else {
            printf("\033[1;34m[runepkg]\033[0m Installing %s-%s\n",
                   pkg_info.package_name ? pkg_info.package_name : "(unknown)",
                   pkg_info.version ? pkg_info.version : "(unknown)");
        }

        if (pkg_info.package_name && pkg_info.version) {
            if (runepkg_storage_create_package_directory(pkg_info.package_name, pkg_info.version) == 0) {
                if (runepkg_storage_write_package_info(pkg_info.package_name, pkg_info.version, &pkg_info) == 0) {
                    if (runepkg_main_hash_table) {
                        runepkg_hash_add_package(runepkg_main_hash_table, &pkg_info);
                    }

                    {
                        char *src_md5 = runepkg_util_concat_path(pkg_info.control_dir_path, "md5sums");
                        if (runepkg_util_file_exists(src_md5)) {
                            char pkg_db_path[PATH_MAX];
                            runepkg_storage_get_package_path(pkg_info.package_name, pkg_info.version, pkg_db_path);
                            {
                                char *dst_md5 = runepkg_util_concat_path(pkg_db_path, "md5sums");
                                if (runepkg_util_copy_file(src_md5, dst_md5) == 0) {
                                    runepkg_log_verbose("MD5 sums persisted to %s\n", dst_md5);
                                }
                                free(dst_md5);
                            }
                        }
                        free(src_md5);
                    }
                }
            }
        }

        runepkg_storage_build_autocomplete_index();
        handle_update_pkglist();

        if (g_system_install_root && pkg_info.data_dir_path && pkg_info.file_count > 0 && pkg_info.file_list) {
            int install_errors = 0;
            pthread_mutex_t error_mutex = PTHREAD_MUTEX_INITIALIZER;
            const int MAX_POSSIBLE_THREADS = 32;
            int max_threads = calculate_optimal_threads();
            pthread_t threads[32];
            FileInstallArgs thread_args[32];
            int active_threads = 0;
            int i;

            if (max_threads > MAX_POSSIBLE_THREADS) max_threads = MAX_POSSIBLE_THREADS;
            memset(threads, 0, sizeof(threads));

            for (i = 0; i < pkg_info.file_count; i++) {
                const char *rel = pkg_info.file_list[i];
                char *src;
                char *dst;
                int thread_idx = -1;
                int t;

                if (!rel || rel[0] == '\0') continue;

                src = runepkg_util_concat_path(pkg_info.data_dir_path, rel);
                dst = runepkg_util_concat_path(g_system_install_root, rel);
                if (!src || !dst) {
                    runepkg_util_free_and_null(&src);
                    runepkg_util_free_and_null(&dst);
                    install_errors++;
                    continue;
                }

                if (active_threads >= max_threads) {
                    for (t = 0; t < max_threads; t++) {
                        if (threads[t] != 0) {
                            pthread_join(threads[t], NULL);
                            threads[t] = 0;
                            active_threads--;
                            break;
                        }
                    }
                }

                for (t = 0; t < max_threads; t++) {
                    if (threads[t] == 0) {
                        thread_idx = t;
                        break;
                    }
                }

                if (thread_idx == -1) {
                    if (perform_file_install(src, dst) != 0) install_errors++;
                    runepkg_util_free_and_null(&src);
                    runepkg_util_free_and_null(&dst);
                    continue;
                }

                thread_args[thread_idx].src = src;
                thread_args[thread_idx].dst = dst;
                thread_args[thread_idx].error_count = &install_errors;
                thread_args[thread_idx].mutex = &error_mutex;

                if (pthread_create(&threads[thread_idx], NULL, install_single_file, &thread_args[thread_idx]) == 0) {
                    active_threads++;
                } else {
                    if (perform_file_install(src, dst) != 0) install_errors++;
                    runepkg_util_free_and_null(&src);
                    runepkg_util_free_and_null(&dst);
                }
            }

            for (i = 0; i < max_threads; i++) {
                if (threads[i] != 0) pthread_join(threads[i], NULL);
            }

            pthread_mutex_destroy(&error_mutex);
            runepkg_execute_maintainer_script(pkg_info.postinst, &pkg_info, "configure");
        }

        /* Integration: Notify host layer that a new installation occurred */
        runepkg_host_register_install(&pkg_info);

        runepkg_pack_cleanup_extraction_workspace(&pkg_info);

    } else {
        runepkg_pack_free_package_info(&pkg_info);
        return -1;
    }

    if (pkg_info.package_name && installing_packages) {
        runepkg_hash_remove_package(installing_packages, pkg_info.package_name);
    }

    /* Incremental cleanup */
    if (g_cleanup_extract_dirs && g_download_dir && deb_file_path) {
        if (strncmp(deb_file_path, g_download_dir, strlen(g_download_dir)) == 0) {
            unlink(deb_file_path);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    install_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
    runepkg_log_verbose("Total installation time: %.6f seconds\n", install_time);

    runepkg_pack_free_package_info(&pkg_info);
    return 0;
}
