/******************************************************************************
- Filename:    runepkg_psychic.c
- Author:      <michkochris@gmail.com>
- Date:        2026-09-04
- Description: Implementation of the Psychic Debian Subsystem (runepkg_psychic)
- LICENSE:     GPL v3
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

#include "runepkg_psychic.h"
#include "runepkg_util.h"

#define PSYCHIC_MAX_MATRIX_ENTRIES 512

typedef struct {
    char name[220];
    int status;
} PsychicMatrixRecord;

static const char *default_triplet_for_deb_arch(const char *deb_arch) {
    if (!deb_arch) {
        return "x86_64-linux-gnu";
    }
    if (strcmp(deb_arch, RUNEPKG_ARCH_AMD64) == 0 || strcmp(deb_arch, "x86_64") == 0) {
        return "x86_64-linux-gnu";
    }
    if (strcmp(deb_arch, RUNEPKG_ARCH_ARM64) == 0 || strcmp(deb_arch, "aarch64") == 0) {
        return "aarch64-linux-gnu";
    }
    if (strcmp(deb_arch, RUNEPKG_ARCH_RISCV64) == 0) {
        return "riscv64-linux-gnu";
    }
    if (strcmp(deb_arch, RUNEPKG_ARCH_ARMHF) == 0 || strcmp(deb_arch, "armv7l") == 0) {
        return "arm-linux-gnueabihf";
    }
    if (strcmp(deb_arch, RUNEPKG_ARCH_I386) == 0 || strcmp(deb_arch, "i686") == 0) {
        return "i386-linux-gnu";
    }
    return "x86_64-linux-gnu";
}

static char *dup_range(const char *start, size_t len) {
    char *out;
    out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static void trim_line_endings(char *s) {
    size_t len;
    if (!s) return;
    len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                       isspace((unsigned char)s[len - 1]))) {
        s[--len] = '\0';
    }
}

void runepkg_psychic_free_target_spec(RunePkgTargetSpec *spec) {
    if (!spec) {
        return;
    }
    free(spec->deb_arch);
    free(spec->gnu_triplet);
    spec->deb_arch = NULL;
    spec->gnu_triplet = NULL;
}

int runepkg_psychic_parse_target_arch(const char *raw_target_arch, RunePkgTargetSpec *out_spec) {
    const char *colon;
    const char *norm_arch;
    const char *triplet;
    size_t left_len;
    size_t right_len;
    char *raw_left;

    if (!raw_target_arch || raw_target_arch[0] == '\0' || !out_spec) {
        return -1;
    }

    out_spec->deb_arch = NULL;
    out_spec->gnu_triplet = NULL;

    colon = strchr(raw_target_arch, ':');
    if (colon) {
        /* Reject multiple colons */
        if (strchr(colon + 1, ':') != NULL) {
            return -1;
        }
        left_len = (size_t)(colon - raw_target_arch);
        right_len = strlen(colon + 1);
        if (left_len == 0 || right_len == 0) {
            return -1;
        }

        raw_left = dup_range(raw_target_arch, left_len);
        if (!raw_left) return -1;
        norm_arch = runepkg_psychic_normalize_deb_arch(raw_left);
        out_spec->deb_arch = dup_range(norm_arch, strlen(norm_arch));
        free(raw_left);
        out_spec->gnu_triplet = dup_range(colon + 1, right_len);
        if (!out_spec->deb_arch || !out_spec->gnu_triplet) {
            runepkg_psychic_free_target_spec(out_spec);
            return -1;
        }
        return 0;
    }

    if (strchr(raw_target_arch, '-') != NULL) {
        norm_arch = runepkg_psychic_normalize_deb_arch(raw_target_arch);
        out_spec->deb_arch = dup_range(norm_arch, strlen(norm_arch));
        out_spec->gnu_triplet = dup_range(raw_target_arch, strlen(raw_target_arch));
        if (!out_spec->deb_arch || !out_spec->gnu_triplet) {
            runepkg_psychic_free_target_spec(out_spec);
            return -1;
        }
        return 0;
    }

    norm_arch = runepkg_psychic_normalize_deb_arch(raw_target_arch);
    triplet = default_triplet_for_deb_arch(norm_arch);

    out_spec->deb_arch = dup_range(norm_arch, strlen(norm_arch));
    out_spec->gnu_triplet = dup_range(triplet, strlen(triplet));
    if (!out_spec->deb_arch || !out_spec->gnu_triplet) {
        runepkg_psychic_free_target_spec(out_spec);
        return -1;
    }
    return 0;
}

int runepkg_psychic_parse_qualifier(const char *pkg_spec, char **out_pkg_name, char **out_qualifier) {
    const char *colon;
    size_t pkg_len;
    size_t qual_len;

    if (!pkg_spec || pkg_spec[0] == '\0' || !out_pkg_name || !out_qualifier) {
        return -1;
    }

    *out_pkg_name = NULL;
    *out_qualifier = NULL;

    colon = strchr(pkg_spec, ':');
    if (!colon) {
        *out_pkg_name = dup_range(pkg_spec, strlen(pkg_spec));
        if (!*out_pkg_name) {
            return -1;
        }
        return 0;
    }

    /* Reject malformed double colons */
    if (strchr(colon + 1, ':') != NULL) {
        return -1;
    }

    pkg_len = (size_t)(colon - pkg_spec);
    qual_len = strlen(colon + 1);
    if (pkg_len == 0 || qual_len == 0) {
        return -1;
    }

    *out_pkg_name = dup_range(pkg_spec, pkg_len);
    *out_qualifier = dup_range(colon + 1, qual_len);
    if (!*out_pkg_name || !*out_qualifier) {
        free(*out_pkg_name);
        free(*out_qualifier);
        *out_pkg_name = NULL;
        *out_qualifier = NULL;
        return -1;
    }
    return 0;
}

RunePkgMultiArchMode runepkg_psychic_parse_multiarch_mode(const char *ma_field) {
    if (!ma_field || ma_field[0] == '\0') {
        return RUNEPKG_MA_NONE;
    }
    if (strcmp(ma_field, "same") == 0) {
        return RUNEPKG_MA_SAME;
    }
    if (strcmp(ma_field, "foreign") == 0) {
        return RUNEPKG_MA_FOREIGN;
    }
    if (strcmp(ma_field, "allowed") == 0) {
        return RUNEPKG_MA_ALLOWED;
    }
    if (strcmp(ma_field, "no") == 0) {
        return RUNEPKG_MA_NO;
    }
    return RUNEPKG_MA_NONE;
}

const char *runepkg_psychic_resolve_dependency_arch(const char *target_arch,
                                                    const char *host_arch,
                                                    const char *qualifier,
                                                    RunePkgMultiArchMode pkg_ma_mode) {
    const char *eff_target;
    const char *eff_host;

    eff_target = (target_arch && target_arch[0] != '\0')
        ? runepkg_psychic_normalize_deb_arch(target_arch)
        : RUNEPKG_ARCH_AMD64;
    eff_host = (host_arch && host_arch[0] != '\0')
        ? runepkg_psychic_normalize_deb_arch(host_arch)
        : eff_target;

    if (qualifier && qualifier[0] != '\0') {
        if (strcmp(qualifier, "native") == 0) {
            return eff_host;
        }
        if (strcmp(qualifier, "any") == 0) {
            return eff_target;
        }
        return runepkg_psychic_normalize_deb_arch(qualifier);
    }

    if (pkg_ma_mode == RUNEPKG_MA_FOREIGN && strcmp(eff_target, eff_host) != 0) {
        return eff_host;
    }
    return eff_target;
}

int runepkg_psychic_is_multiarch_satisfied(const char *candidate_arch,
                                           RunePkgMultiArchMode candidate_ma_mode,
                                           const char *qualifier,
                                           const char *target_arch,
                                           const char *host_arch) {
    const char *cand;
    const char *eff_target;
    const char *eff_host;

    if (!candidate_arch || candidate_arch[0] == '\0') {
        return 0;
    }

    cand = runepkg_psychic_normalize_deb_arch(candidate_arch);
    eff_target = (target_arch && target_arch[0] != '\0')
        ? runepkg_psychic_normalize_deb_arch(target_arch)
        : RUNEPKG_ARCH_AMD64;
    eff_host = (host_arch && host_arch[0] != '\0')
        ? runepkg_psychic_normalize_deb_arch(host_arch)
        : eff_target;

    if (qualifier && qualifier[0] != '\0') {
        if (strcmp(qualifier, "native") == 0) {
            return (strcmp(cand, eff_host) == 0 || strcmp(cand, "all") == 0) ? 1 : 0;
        }
        if (strcmp(qualifier, "any") == 0) {
            if (candidate_ma_mode == RUNEPKG_MA_ALLOWED || candidate_ma_mode == RUNEPKG_MA_FOREIGN) {
                return 1;
            }
            return (strcmp(cand, eff_target) == 0 || strcmp(cand, "all") == 0) ? 1 : 0;
        }
        return (strcmp(cand, runepkg_psychic_normalize_deb_arch(qualifier)) == 0) ? 1 : 0;
    }

    if (strcmp(cand, eff_target) == 0 || strcmp(cand, "all") == 0) {
        return 1;
    }
    if (strcmp(eff_target, eff_host) != 0 && candidate_ma_mode == RUNEPKG_MA_FOREIGN) {
        return 1;
    }
    return 0;
}

int runepkg_psychic_detect_target_arch(const char *sysroot_path, char *arch_buf, size_t buf_sz) {
    char marker_path[PATH_MAX];
    FILE *f;

    if (!arch_buf || buf_sz == 0) return -1;

    /* 1. If sysroot_path is provided, check if an architecture is already hinted/staged */
    if (sysroot_path && sysroot_path[0] != '\0') {
        snprintf(marker_path, sizeof(marker_path), "%s/etc/runepkg_target_arch", sysroot_path);
        if (runepkg_util_file_exists(marker_path)) {
            f = fopen(marker_path, "r");
            if (f) {
                if (fgets(arch_buf, (int)buf_sz, f)) {
                    trim_line_endings(arch_buf);
                    fclose(f);
                    return 0;
                }
                fclose(f);
            }
        }
    }

    /* 2. Fallback to host architecture detection via compiler macros */
#if defined(__x86_64__) || defined(_M_X64)
    runepkg_util_safe_strncpy(arch_buf, RUNEPKG_ARCH_AMD64, buf_sz);
#elif defined(__aarch64__) || defined(_M_ARM64)
    runepkg_util_safe_strncpy(arch_buf, RUNEPKG_ARCH_ARM64, buf_sz);
#elif defined(__riscv) && __riscv_xlen == 64
    runepkg_util_safe_strncpy(arch_buf, RUNEPKG_ARCH_RISCV64, buf_sz);
#elif defined(__arm__)
    runepkg_util_safe_strncpy(arch_buf, RUNEPKG_ARCH_ARMHF, buf_sz);
#elif defined(__i386__) || defined(_M_IX86)
    runepkg_util_safe_strncpy(arch_buf, RUNEPKG_ARCH_I386, buf_sz);
#else
    runepkg_util_safe_strncpy(arch_buf, RUNEPKG_ARCH_AMD64, buf_sz);
#endif

    return 0;
}

static const char *get_linker_name_for_deb_arch(const char *deb_arch) {
    if (!deb_arch) return NULL;
    if (strcmp(deb_arch, "arm64") == 0 || strcmp(deb_arch, "aarch64") == 0) {
        return "ld-linux-aarch64.so.1";
    }
    if (strcmp(deb_arch, "amd64") == 0 || strcmp(deb_arch, "x86_64") == 0) {
        return "ld-linux-x86-64.so.2";
    }
    if (strcmp(deb_arch, "armhf") == 0 || strcmp(deb_arch, "armv7l") == 0) {
        return "ld-linux-armhf.so.3";
    }
    if (strcmp(deb_arch, "riscv64") == 0) {
        return "ld-linux-riscv64-lp64d.so.1";
    }
    if (strcmp(deb_arch, "i386") == 0 || strcmp(deb_arch, "i686") == 0) {
        return "ld-linux.so.2";
    }
    return NULL;
}

void runepkg_psychic_anchor_dynamic_linker(const char *sysroot_path, const char *deb_arch, const char *gnu_triplet) {
    char lib_dir[PATH_MAX];
    char usr_lib_dir[PATH_MAX];
    char lib_sub_dir[PATH_MAX];
    char link_path[PATH_MAX];
    char target_path[PATH_MAX];
    char libc_so_path[PATH_MAX];
    char libc_so_6_path[PATH_MAX];
    char lib64_dir[PATH_MAX];
    char usr_lib64_dir[PATH_MAX];
    const char *linker_name;
    struct stat st;

    if (!sysroot_path || !deb_arch || !gnu_triplet) return;

    snprintf(lib_dir, sizeof(lib_dir), "%s/lib", sysroot_path);
    snprintf(lib_sub_dir, sizeof(lib_sub_dir), "%s/lib/%s", sysroot_path, gnu_triplet);
    snprintf(usr_lib_dir, sizeof(usr_lib_dir), "%s/usr/lib/%s", sysroot_path, gnu_triplet);

    runepkg_util_create_dir_recursive(lib_dir, 0755);
    runepkg_util_create_dir_recursive(lib_sub_dir, 0755);
    runepkg_util_create_dir_recursive(usr_lib_dir, 0755);

    /* Anchor dynamic linker loader symlink matching rescue_kali.sh */
    linker_name = get_linker_name_for_deb_arch(deb_arch);
    if (linker_name) {
        /* /usr/lib/<linker_name> -> <gnu_triplet>/<linker_name> */
        snprintf(link_path, sizeof(link_path), "%s/usr/lib/%s", sysroot_path, linker_name);
        if (lstat(link_path, &st) == 0) {
            unlink(link_path);
        }
        snprintf(target_path, sizeof(target_path), "%s/%s", gnu_triplet, linker_name);
        if (symlink(target_path, link_path) != 0) {
            (void)0;
        }

        if (strcmp(deb_arch, "amd64") == 0 || strcmp(deb_arch, "x86_64") == 0) {
            snprintf(lib64_dir, sizeof(lib64_dir), "%s/lib64", sysroot_path);
            snprintf(usr_lib64_dir, sizeof(usr_lib64_dir), "%s/usr/lib64", sysroot_path);
            runepkg_util_create_dir_recursive(usr_lib64_dir, 0755);

            /* /lib64 -> usr/lib64 */
            if (lstat(lib64_dir, &st) == 0) {
                unlink(lib64_dir);
            }
            if (symlink("usr/lib64", lib64_dir) != 0) {
                (void)0;
            }

            /* /usr/lib64/<linker_name> -> ../lib/x86_64-linux-gnu/<linker_name> */
            snprintf(link_path, sizeof(link_path), "%s/usr/lib64/%s", sysroot_path, linker_name);
            if (lstat(link_path, &st) == 0) {
                unlink(link_path);
            }
            snprintf(target_path, sizeof(target_path), "../lib/%s/%s", gnu_triplet, linker_name);
            if (symlink(target_path, link_path) != 0) {
                (void)0;
            }
        }
    }

    /* Ensure /usr/lib/<gnu_triplet>/libc.so symlink exists if libc.so.6 is present */
    snprintf(libc_so_6_path, sizeof(libc_so_6_path), "%s/usr/lib/%s/libc.so.6", sysroot_path, gnu_triplet);
    snprintf(libc_so_path, sizeof(libc_so_path), "%s/usr/lib/%s/libc.so", sysroot_path, gnu_triplet);
    if (lstat(libc_so_6_path, &st) == 0 && lstat(libc_so_path, &st) != 0) {
        if (symlink("libc.so.6", libc_so_path) != 0) {
            (void)0;
        }
    }
}

int runepkg_psychic_prophecy_check(const char *sysroot_path, const char *target_arch) {
    char dir_buf[PATH_MAX];
    char marker_path[PATH_MAX];
    FILE *f;
    RunePkgTargetSpec spec;
    const char *eff_arch;

    if (!sysroot_path || sysroot_path[0] == '\0') return -1;
    eff_arch = (target_arch && target_arch[0] != '\0') ? target_arch : RUNEPKG_ARCH_AMD64;

    if (runepkg_psychic_parse_target_arch(eff_arch, &spec) != 0) {
        return -1;
    }

    runepkg_util_log_verbose("[psychic] Initiating pre-flight prophecy check for target arch: %s:%s in sysroot: %s\n",
        spec.deb_arch, spec.gnu_triplet, sysroot_path);

    /* 1. Ensure base FHS directories exist */
    runepkg_util_create_dir_recursive(sysroot_path, 0755);

    snprintf(dir_buf, sizeof(dir_buf), "%s/etc", sysroot_path);
    runepkg_util_create_dir_recursive(dir_buf, 0755);

    snprintf(dir_buf, sizeof(dir_buf), "%s/var/lib/dpkg", sysroot_path);
    runepkg_util_create_dir_recursive(dir_buf, 0755);

    snprintf(dir_buf, sizeof(dir_buf), "%s/lib", sysroot_path);
    runepkg_util_create_dir_recursive(dir_buf, 0755);

    snprintf(dir_buf, sizeof(dir_buf), "%s/usr/lib/%s", sysroot_path, spec.gnu_triplet);
    runepkg_util_create_dir_recursive(dir_buf, 0755);

    snprintf(dir_buf, sizeof(dir_buf), "%s/usr/include/%s", sysroot_path, spec.gnu_triplet);
    runepkg_util_create_dir_recursive(dir_buf, 0755);

    /* Store architecture marker (<deb_arch>:<gnu_triplet>) */
    snprintf(marker_path, sizeof(marker_path), "%s/etc/runepkg_target_arch", sysroot_path);
    f = fopen(marker_path, "w");
    if (f) {
        fprintf(f, "%s:%s\n", spec.deb_arch, spec.gnu_triplet);
        fclose(f);
    }

    /* 2. Anchor Multi-Arch FHS Symlinks and Dynamic Linker */
    runepkg_psychic_anchor_dynamic_linker(sysroot_path, spec.deb_arch, spec.gnu_triplet);

    /* 3. Check and guard Debian FHS symlinks */
    runepkg_psychic_check_fhs_symlinks(sysroot_path);

    runepkg_psychic_free_target_spec(&spec);
    runepkg_util_log_verbose("[psychic] Pre-flight prophecy check completed successfully.\n");
    return 0;
}

int runepkg_psychic_resolve_virtual(const char *virtual_pkg, char *concrete_buf, size_t buf_sz) {
    if (!virtual_pkg || !concrete_buf || buf_sz == 0) return -1;

    /* Hardcoded Debian Virtual Package Telepathy mapping */
    if (strcmp(virtual_pkg, "default-mta") == 0 || strcmp(virtual_pkg, "mail-transport-agent") == 0) {
        runepkg_util_safe_strncpy(concrete_buf, "postfix", buf_sz);
        return 0;
    }
    if (strcmp(virtual_pkg, "libc-dev") == 0) {
        runepkg_util_safe_strncpy(concrete_buf, "libc6-dev", buf_sz);
        return 0;
    }
    if (strcmp(virtual_pkg, "c-compiler") == 0) {
        runepkg_util_safe_strncpy(concrete_buf, "gcc", buf_sz);
        return 0;
    }
    if (strcmp(virtual_pkg, "cpp-compiler") == 0) {
        runepkg_util_safe_strncpy(concrete_buf, "g++", buf_sz);
        return 0;
    }
    if (strcmp(virtual_pkg, "awk") == 0) {
        runepkg_util_safe_strncpy(concrete_buf, "gawk", buf_sz);
        return 0;
    }
    if (strcmp(virtual_pkg, "pager") == 0) {
        runepkg_util_safe_strncpy(concrete_buf, "less", buf_sz);
        return 0;
    }

    /* Not a recognized virtual package, return original name */
    runepkg_util_safe_strncpy(concrete_buf, virtual_pkg, buf_sz);
    return -1;
}

static int prefix_has_token(const char *s, size_t len, const char *token) {
    size_t tlen;
    size_t i;

    if (!s || !token || len == 0) {
        return 0;
    }
    tlen = strlen(token);
    if (tlen == 0 || tlen > len) {
        return 0;
    }
    for (i = 0; i + tlen <= len; i++) {
        if (strncmp(s + i, token, tlen) == 0) {
            return 1;
        }
    }
    return 0;
}

const char *runepkg_psychic_normalize_deb_arch(const char *arch) {
    const char *colon;
    size_t len;

    if (!arch) return "amd64";

    colon = strchr(arch, ':');
    if (colon) {
        const char *left = arch;
        len = (size_t)(colon - arch);
        if (len == 0) return "amd64";

        if ((len == 7 && strncmp(left, "aarch64", 7) == 0) ||
            (len == 5 && strncmp(left, "arm64", 5) == 0) ||
            prefix_has_token(left, len, "aarch64") ||
            prefix_has_token(left, len, "arm64")) {
            return "arm64";
        }
        if ((len == 6 && strncmp(left, "x86_64", 6) == 0) ||
            (len == 5 && strncmp(left, "amd64", 5) == 0) ||
            prefix_has_token(left, len, "x86_64") ||
            prefix_has_token(left, len, "amd64")) {
            return "amd64";
        }
        if ((len == 7 && strncmp(left, "riscv64", 7) == 0) ||
            prefix_has_token(left, len, "riscv64")) {
            return "riscv64";
        }
        if ((len == 5 && strncmp(left, "armhf", 5) == 0) ||
            (len == 6 && strncmp(left, "armv7l", 6) == 0) ||
            prefix_has_token(left, len, "armhf") ||
            prefix_has_token(left, len, "armv7") ||
            prefix_has_token(left, len, "gnueabihf")) {
            return "armhf";
        }
        if ((len == 4 && strncmp(left, "i386", 4) == 0) ||
            (len == 4 && strncmp(left, "i686", 4) == 0) ||
            prefix_has_token(left, len, "i386") ||
            prefix_has_token(left, len, "i686")) {
            return "i386";
        }

        {
            static char ring[4][64];
            static int ring_idx = 0;
            char *buf;
            ring_idx = (ring_idx + 1) % 4;
            buf = ring[ring_idx];
            if (len >= 64) len = 63;
            memcpy(buf, arch, len);
            buf[len] = '\0';
            return buf;
        }
    }

    if (strcmp(arch, "aarch64") == 0 || strcmp(arch, "arm64") == 0 ||
        strstr(arch, "aarch64") || strstr(arch, "arm64")) {
        return "arm64";
    }
    if (strcmp(arch, "x86_64") == 0 || strcmp(arch, "amd64") == 0 ||
        strstr(arch, "x86_64") || strstr(arch, "amd64")) {
        return "amd64";
    }
    if (strcmp(arch, "riscv64") == 0 || strstr(arch, "riscv64")) {
        return "riscv64";
    }
    if (strcmp(arch, "armhf") == 0 || strcmp(arch, "armv7l") == 0 ||
        strstr(arch, "armhf") || strstr(arch, "armv7") || strstr(arch, "gnueabihf")) {
        return "armhf";
    }
    if (strcmp(arch, "i386") == 0 || strcmp(arch, "i686") == 0 ||
        strstr(arch, "i386") || strstr(arch, "i686")) {
        return "i386";
    }
    return arch;
}

/* --- Debian FHS & Protected Symlink Guards --- */

int runepkg_psychic_guard_protected_symlink(const char *path) {
    struct stat st;
    char target[PATH_MAX];
    ssize_t len;

    if (!path) return 0;

    if (lstat(path, &st) != 0) {
        return 0; /* Path doesn't exist */
    }

    if (S_ISLNK(st.st_mode)) {
        len = readlink(path, target, sizeof(target) - 1);
        if (len < 0) {
            fprintf(stderr, "\033[1;31m[psychic warning]\033[0m Protected symlink %s is dangling/broken!\n", path);
            return -1;
        }
        target[len] = '\0';
        if (stat(path, &st) != 0) {
            fprintf(stderr, "\033[1;31m[psychic warning]\033[0m Protected symlink %s points to non-existent target '%s'!\n", path, target);
            return -1;
        }
    }
    return 0;
}

int runepkg_psychic_check_fhs_symlinks(const char *sysroot_path) {
    char path_buf[PATH_MAX];
    struct stat st;
    int violations = 0;
    const char *fhs_links[] = { "lib", "lib64", "bin", "sbin" };
    size_t i;

    if (!sysroot_path) sysroot_path = "";

    for (i = 0; i < sizeof(fhs_links) / sizeof(fhs_links[0]); i++) {
        if (sysroot_path[0] != '\0') {
            snprintf(path_buf, sizeof(path_buf), "%s/%s", sysroot_path, fhs_links[i]);
        } else {
            snprintf(path_buf, sizeof(path_buf), "/%s", fhs_links[i]);
        }

        if (lstat(path_buf, &st) == 0) {
            if (S_ISLNK(st.st_mode)) {
                if (stat(path_buf, &st) != 0) {
                    fprintf(stderr, "\033[1;31m[psychic FHS error]\033[0m FHS symlink %s is dangling!\n", path_buf);
                    violations++;
                }
            }
        }
    }

    if (sysroot_path[0] != '\0') {
        snprintf(path_buf, sizeof(path_buf), "%s/bin/ls", sysroot_path);
    } else {
        snprintf(path_buf, sizeof(path_buf), "/bin/ls");
    }
    runepkg_psychic_guard_protected_symlink(path_buf);

    return violations > 0 ? -1 : 0;
}

int runepkg_psychic_prophesy_missing_package(const char *sysroot_path,
                                              const char *missing_item,
                                              const char *deb_arch,
                                              char *out_pkg_buf,
                                              size_t buf_sz) {
    const char *norm_arch;
    char clean_item[256];
    const char *cand_pkg = NULL;
    (void)sysroot_path;

    if (!missing_item || !out_pkg_buf || buf_sz == 0) return -1;
    norm_arch = runepkg_psychic_normalize_deb_arch(deb_arch);

    /* Normalize missing_item string (e.g. "<zlib.h>" -> "zlib.h", "-lz" -> "zlib.h") */
    if (missing_item[0] == '<') {
        size_t len = strlen(missing_item);
        if (len > 2 && missing_item[len - 1] == '>') {
            snprintf(clean_item, sizeof(clean_item), "%.*s", (int)(len - 2), missing_item + 1);
        } else {
            snprintf(clean_item, sizeof(clean_item), "%s", missing_item);
        }
    } else if (strcmp(missing_item, "-lz") == 0) {
        snprintf(clean_item, sizeof(clean_item), "%s", "zlib.h");
    } else if (strcmp(missing_item, "-lssl") == 0 || strcmp(missing_item, "-lcrypto") == 0) {
        snprintf(clean_item, sizeof(clean_item), "%s", "openssl/ssl.h");
    } else {
        snprintf(clean_item, sizeof(clean_item), "%s", missing_item);
    }

    if (strcmp(clean_item, "zlib.h") == 0) cand_pkg = "zlib1g-dev";
    else if (strcmp(clean_item, "openssl/ssl.h") == 0) cand_pkg = "libssl-dev";
    else if (strcmp(clean_item, "sqlite3.h") == 0) cand_pkg = "libsqlite3-dev";
    else if (strcmp(clean_item, "curl/curl.h") == 0) cand_pkg = "libcurl4-openssl-dev";
    else if (strcmp(clean_item, "ffi.h") == 0) cand_pkg = "libffi-dev";
    else if (strcmp(clean_item, "glib.h") == 0) cand_pkg = "libglib2.0-dev";
    else if (strcmp(clean_item, "libtool") == 0) cand_pkg = "libtool";
    else if (strcmp(clean_item, "m4") == 0) cand_pkg = "m4";

    if (cand_pkg) {
        snprintf(out_pkg_buf, buf_sz, "%s:%s", cand_pkg, norm_arch);
        printf("\033[1;36m[psychic prophecy]\033[0m Missing item '%s'. Recommended Debian package: %s\n",
            missing_item, out_pkg_buf);
        return 0;
    }

    return -1;
}

int runepkg_psychic_reconcile_ld_symlink(const char *sysroot_path) {
    char root[PATH_MAX];
    char ld_path[PATH_MAX];
    char path_buf[PATH_MAX];
    struct stat st;
    struct stat target_st;
    const char *dirs[4];
    size_t d;
    int ld_needs_healing;
    const char *candidates[4];
    int healed;
    size_t c;
    char best_link_target[256];
    char cand_path[PATH_MAX];
    char sh_path[PATH_MAX];
    char dash_bin[PATH_MAX];
    char usr_dir[PATH_MAX];
    char cmd[PATH_MAX * 4 + 256];
    char target_val[64];

    if (!sysroot_path || sysroot_path[0] == '\0') {
        root[0] = '\0';
    } else {
        snprintf(root, sizeof(root), "%s", sysroot_path);
    }

    /* 1. Ensure Usrmerge Symlinks (/bin, /sbin, /lib, /lib64) are correct */
    dirs[0] = "bin";
    dirs[1] = "sbin";
    dirs[2] = "lib";
    dirs[3] = "lib64";

    for (d = 0; d < 4; d++) {
        if (root[0] != '\0') {
            snprintf(path_buf, sizeof(path_buf), "%s/%s", root, dirs[d]);
        } else {
            snprintf(path_buf, sizeof(path_buf), "/%s", dirs[d]);
        }

        if (lstat(path_buf, &st) == 0 && !S_ISLNK(st.st_mode) && S_ISDIR(st.st_mode)) {
            if (root[0] != '\0') {
                snprintf(usr_dir, sizeof(usr_dir), "%s/usr/%s", root, dirs[d]);
            } else {
                snprintf(usr_dir, sizeof(usr_dir), "/usr/%s", dirs[d]);
            }
            snprintf(cmd, sizeof(cmd), "mkdir -p %s && cp -a %s/. %s/ 2>/dev/null || true", usr_dir, path_buf, usr_dir);
            if (system(cmd) != 0) { /* handled */ }
            snprintf(cmd, sizeof(cmd), "rm -rf %s", path_buf);
            if (system(cmd) != 0) { /* handled */ }
        }
        if (lstat(path_buf, &st) != 0) {
            snprintf(target_val, sizeof(target_val), "usr/%s", dirs[d]);
            if (symlink(target_val, path_buf) < 0) { /* handled */ }
        }
    }

    /* Ensure dynamic loader and Multi-Arch linkers are anchored */
    {
        char detected_arch[64];
        RunePkgTargetSpec spec;
        if (runepkg_psychic_detect_target_arch(sysroot_path, detected_arch, sizeof(detected_arch)) == 0) {
            if (runepkg_psychic_parse_target_arch(detected_arch, &spec) == 0) {
                runepkg_psychic_anchor_dynamic_linker(sysroot_path, spec.deb_arch, spec.gnu_triplet);
                runepkg_psychic_free_target_spec(&spec);
            } else {
                runepkg_psychic_anchor_dynamic_linker(sysroot_path, "amd64", "x86_64-linux-gnu");
            }
        } else {
            runepkg_psychic_anchor_dynamic_linker(sysroot_path, "amd64", "x86_64-linux-gnu");
        }
    }

    /* 2. Reconcile /usr/bin/ld symlink (Only if binutils/linker candidate is present) */
    if (root[0] != '\0' && strcmp(root, "/") != 0) {
        snprintf(ld_path, sizeof(ld_path), "%s/usr/bin/ld", root);
    } else {
        snprintf(ld_path, sizeof(ld_path), "/usr/bin/ld");
    }

    ld_needs_healing = 0;
    if (lstat(ld_path, &st) != 0 || !S_ISLNK(st.st_mode) || stat(ld_path, &target_st) != 0) {
        ld_needs_healing = 1;
    }

    if (ld_needs_healing) {
        candidates[0] = "x86_64-linux-gnu-ld.bfd";
        candidates[1] = "aarch64-linux-gnu-ld.bfd";
        candidates[2] = "ld.bfd";
        candidates[3] = "ld.gold";
        healed = 0;
        memset(best_link_target, 0, sizeof(best_link_target));

        for (c = 0; c < 4; c++) {
            if (root[0] != '\0' && strcmp(root, "/") != 0) {
                snprintf(cand_path, sizeof(cand_path), "%s/usr/bin/%s", root, candidates[c]);
            } else {
                snprintf(cand_path, sizeof(cand_path), "/usr/bin/%s", candidates[c]);
            }
            if (stat(cand_path, &target_st) == 0) {
                if (c == 0) {
                    snprintf(best_link_target, sizeof(best_link_target), "x86_64-linux-gnu-ld");
                } else {
                    snprintf(best_link_target, sizeof(best_link_target), "%s", candidates[c]);
                }
                healed = 1;
                break;
            }
        }

        if (healed) {
            if (unlink(ld_path) < 0) { /* handled */ }
            if (symlink(best_link_target, ld_path) == 0) {
                fprintf(stdout, "\033[1;35m[psychic linker prophecy]\033[0m Healed and reconciled /usr/bin/ld -> %s\n", best_link_target);
            }
        } else {
            if (lstat(ld_path, &st) == 0 && S_ISLNK(st.st_mode) && stat(ld_path, &target_st) != 0) {
                unlink(ld_path);
                runepkg_log_verbose("[psychic linker prophecy] Removed dangling /usr/bin/ld symlink (no binutils candidate found).\n");
            } else {
                runepkg_log_verbose("[psychic linker prophecy] No linker candidate found; leaving /usr/bin/ld untouched (no binutils).\n");
            }
        }
    }

    /* 3. Reconcile /bin/sh */
    if (root[0] != '\0' && strcmp(root, "/") != 0) {
        snprintf(sh_path, sizeof(sh_path), "%s/bin/sh", root);
    } else {
        snprintf(sh_path, sizeof(sh_path), "/bin/sh");
    }
    if (lstat(sh_path, &st) != 0 || stat(sh_path, &target_st) != 0) {
        if (unlink(sh_path) < 0) { /* handled */ }
        if (root[0] != '\0' && strcmp(root, "/") != 0) {
            snprintf(dash_bin, sizeof(dash_bin), "%s/usr/bin/dash", root);
        } else {
            snprintf(dash_bin, sizeof(dash_bin), "/usr/bin/dash");
        }
        if (stat(dash_bin, &target_st) == 0) {
            if (symlink("dash", sh_path) == 0) {
                fprintf(stdout, "\033[1;35m[psychic shell prophecy]\033[0m Reconciled /bin/sh -> dash\n");
            }
        } else {
            if (symlink("bash", sh_path) == 0) {
                fprintf(stdout, "\033[1;35m[psychic shell prophecy]\033[0m Reconciled /bin/sh -> bash\n");
            }
        }
    }

    return 0;
}

/* --- Psychic Matryoshka & Rescue Tooling Injector --- */

int runepkg_psychic_inject_matryoshka_essentials(const char **pkg_names_in, int count_in, char ***pkg_names_out, int *count_out) {
    int has_binutils = 0;
    int has_libc = 0;
    int has_coreutils = 0;
    int has_dash = 0;
    int has_bash = 0;
    int has_base_files = 0;
    int new_count;
    int i;
    int out_idx;
    char **out_list;

    if (!pkg_names_in || count_in <= 0 || !pkg_names_out || !count_out) {
        return -1;
    }

    for (i = 0; i < count_in; i++) {
        if (pkg_names_in[i]) {
            if (strcmp(pkg_names_in[i], "binutils") == 0 || strncmp(pkg_names_in[i], "binutils", 8) == 0) {
                has_binutils = 1;
            }
            if (strcmp(pkg_names_in[i], "libc6") == 0 || strncmp(pkg_names_in[i], "libc6", 5) == 0) {
                has_libc = 1;
            }
            if (strcmp(pkg_names_in[i], "coreutils") == 0) {
                has_coreutils = 1;
            }
            if (strcmp(pkg_names_in[i], "dash") == 0) {
                has_dash = 1;
            }
            if (strcmp(pkg_names_in[i], "bash") == 0) {
                has_bash = 1;
            }
            if (strcmp(pkg_names_in[i], "base-files") == 0) {
                has_base_files = 1;
            }
        }
    }

    new_count = count_in + (!has_binutils ? 1 : 0) + (!has_libc ? 1 : 0) +
                (!has_coreutils ? 1 : 0) + (!has_dash ? 1 : 0) + (!has_bash ? 1 : 0) + (!has_base_files ? 1 : 0);
    out_list = (char **)malloc((size_t)new_count * sizeof(char *));
    if (!out_list) {
        return -1;
    }

    out_idx = 0;
    if (!has_binutils) {
        out_list[out_idx++] = strdup("binutils");
        fprintf(stdout, "\033[1;35m[psychic matryoshka prophecy]\033[0m Automatically injected absolute rescue tool 'binutils'.\n");
    }
    if (!has_libc) {
        out_list[out_idx++] = strdup("libc6");
        fprintf(stdout, "\033[1;35m[psychic matryoshka prophecy]\033[0m Automatically injected core runtime 'libc6'.\n");
    }
    if (!has_coreutils) {
        out_list[out_idx++] = strdup("coreutils");
        fprintf(stdout, "\033[1;35m[psychic matryoshka prophecy]\033[0m Automatically injected base utility 'coreutils'.\n");
    }
    if (!has_dash) {
        out_list[out_idx++] = strdup("dash");
        fprintf(stdout, "\033[1;35m[psychic matryoshka prophecy]\033[0m Automatically injected POSIX shell 'dash'.\n");
    }
    if (!has_bash) {
        out_list[out_idx++] = strdup("bash");
        fprintf(stdout, "\033[1;35m[psychic matryoshka prophecy]\033[0m Automatically injected GNU Bourne Again Shell 'bash'.\n");
    }
    if (!has_base_files) {
        out_list[out_idx++] = strdup("base-files");
        fprintf(stdout, "\033[1;35m[psychic matryoshka prophecy]\033[0m Automatically injected FHS base files 'base-files'.\n");
    }

    for (i = 0; i < count_in; i++) {
        if (pkg_names_in[i]) {
            out_list[out_idx++] = strdup(pkg_names_in[i]);
        }
    }

    *pkg_names_out = out_list;
    *count_out = out_idx;
    return 0;
}
