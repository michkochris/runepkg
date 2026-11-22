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
    char link_path[PATH_MAX];
    char target_path[PATH_MAX];
    char libc_so_path[PATH_MAX];
    char libc_so_6_path[PATH_MAX];
    char lib64_dir[PATH_MAX];
    const char *linker_name;
    struct stat st;

    if (!sysroot_path || !deb_arch || !gnu_triplet) return;

    snprintf(lib_dir, sizeof(lib_dir), "%s/lib/%s", sysroot_path, gnu_triplet);
    snprintf(usr_lib_dir, sizeof(usr_lib_dir), "%s/usr/lib/%s", sysroot_path, gnu_triplet);

    runepkg_util_create_dir_recursive(usr_lib_dir, 0755);

    /* If /lib/<gnu_triplet> exists as an empty directory, remove it so we can create symlink */
    if (lstat(lib_dir, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            if (rmdir(lib_dir) != 0) {
                (void)0;
            }
        }
    }

    if (lstat(lib_dir, &st) != 0) {
        snprintf(target_path, sizeof(target_path), "../usr/lib/%s", gnu_triplet);
        if (symlink(target_path, lib_dir) != 0) {
            (void)0;
        }
    }

    /* Anchor dynamic linker loader symlink in /lib/<linker_name> */
    linker_name = get_linker_name_for_deb_arch(deb_arch);
    if (linker_name) {
        snprintf(link_path, sizeof(link_path), "%s/lib/%s", sysroot_path, linker_name);
        if (lstat(link_path, &st) == 0) {
            unlink(link_path);
        }
        snprintf(target_path, sizeof(target_path), "../usr/lib/%s/%s", gnu_triplet, linker_name);
        if (symlink(target_path, link_path) != 0) {
            (void)0;
        }

        if (strcmp(deb_arch, "amd64") == 0 || strcmp(deb_arch, "x86_64") == 0) {
            snprintf(lib64_dir, sizeof(lib64_dir), "%s/lib64", sysroot_path);
            runepkg_util_create_dir_recursive(lib64_dir, 0755);
            snprintf(link_path, sizeof(link_path), "%s/lib64/%s", sysroot_path, linker_name);
            if (lstat(link_path, &st) == 0) {
                unlink(link_path);
            }
            snprintf(target_path, sizeof(target_path), "../usr/lib/%s/%s", gnu_triplet, linker_name);
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

    /* 3. Scan actual sysroot state and write psychic_matrix.bin + psychic_matrix.txt */
    runepkg_psychic_build_matrix_bin(sysroot_path, spec.gnu_triplet);

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

/* --- psychic_matrix.bin & psychic_matrix.txt Sysroot Scanner --- */

static void psychic_add_record(PsychicMatrixRecord *records,
                               size_t *count,
                               size_t max_count,
                               const char *name,
                               int status) {
    size_t i;
    if (!records || !count || !name || name[0] == '\0') return;
    if (*count >= max_count) return;

    for (i = 0; i < *count; i++) {
        if (strcmp(records[i].name, name) == 0) {
            if (status != 0) {
                records[i].status = status;
            }
            return;
        }
    }

    runepkg_util_safe_strncpy(records[*count].name, name, sizeof(records[*count].name));
    records[*count].status = status;
    (*count)++;
}

static void psychic_scan_include_tree(const char *base_dir,
                                      const char *prefix,
                                      PsychicMatrixRecord *records,
                                      size_t *count,
                                      size_t max_count) {
    DIR *dir;
    struct dirent *entry;

    if (!base_dir || !records || !count || *count >= max_count) return;
    dir = opendir(base_dir);
    if (!dir) return;

    while ((entry = readdir(dir)) != NULL && *count < max_count) {
        char full_path[PATH_MAX * 2];
        char rel_name[220];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, entry->d_name);
        if (stat(full_path, &st) != 0) continue;

        if (prefix && prefix[0] != '\0') {
            snprintf(rel_name, sizeof(rel_name), "%.100s/%.100s", prefix, entry->d_name);
        } else {
            snprintf(rel_name, sizeof(rel_name), "%.219s", entry->d_name);
        }

        if (S_ISDIR(st.st_mode)) {
            psychic_scan_include_tree(full_path, rel_name, records, count, max_count);
        } else if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
            psychic_add_record(records, count, max_count, rel_name, 1);
        }
    }

    closedir(dir);
}

static void psychic_scan_flat_dir(const char *dir_path,
                                  PsychicMatrixRecord *records,
                                  size_t *count,
                                  size_t max_count) {
    DIR *dir;
    struct dirent *entry;

    if (!dir_path || !records || !count || *count >= max_count) return;
    dir = opendir(dir_path);
    if (!dir) return;

    while ((entry = readdir(dir)) != NULL && *count < max_count) {
        char full_path[PATH_MAX * 2];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (stat(full_path, &st) == 0 && (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))) {
            psychic_add_record(records, count, max_count, entry->d_name, 1);
        }
    }

    closedir(dir);
}

static void psychic_scan_installed_pkgs(const char *db_dir,
                                        PsychicMatrixRecord *records,
                                        size_t *count,
                                        size_t max_count) {
    char status_path[PATH_MAX + 64];
    FILE *sf;
    char line[512];

    if (!db_dir || !records || !count || *count >= max_count) return;

    snprintf(status_path, sizeof(status_path), "%.*s/installed_packages.txt",
             (int)(PATH_MAX - 1), db_dir);
    sf = fopen(status_path, "r");
    if (!sf) return;

    while (fgets(line, sizeof(line), sf) != NULL && *count < max_count) {
        char *sep;
        trim_line_endings(line);
        if (line[0] == '\0' || line[0] == '#') continue;
        sep = strchr(line, ':');
        if (sep) {
            *sep = '\0';
        }
        if (line[0] != '\0') {
            char pkg_entry[220];
            snprintf(pkg_entry, sizeof(pkg_entry), "pkg:%.210s", line);
            psychic_add_record(records, count, max_count, pkg_entry, 1);
        }
    }

    fclose(sf);
}

static void write_psychic_matrix_txt(const char *txt_path,
                                     const char *sysroot_path,
                                     const char *gnu_triplet,
                                     const PsychicMatrixRecord *records,
                                     size_t count) {
    FILE *tf;
    size_t i;

    if (!txt_path || !records) return;

    tf = fopen(txt_path, "w");
    if (!tf) return;

    fprintf(tf, "# psychic_matrix.txt (live sysroot scan)\n");
    fprintf(tf, "# sysroot=%s\n", sysroot_path ? sysroot_path : "");
    fprintf(tf, "# triplet=%s\n", gnu_triplet ? gnu_triplet : "");
    fprintf(tf, "# entries=%lu\n", (unsigned long)count);

    if (count == 0) {
        fprintf(tf, "# empty sysroot scan\n");
    } else {
        for (i = 0; i < count; i++) {
            fprintf(tf, "%s:%d\n", records[i].name, records[i].status);
        }
    }

    fclose(tf);
}

int runepkg_psychic_build_matrix_bin(const char *sysroot_path, const char *gnu_triplet) {
    char db_dir[PATH_MAX + 64];
    char bin_path[PATH_MAX + 128];
    char txt_path[PATH_MAX + 128];
    char scan_dir[PATH_MAX + 128];
    FILE *f;
    PsychicMatrixHeader hdr;
    PsychicMatrixRecord *records;
    size_t count = 0;
    size_t i;

    if (!sysroot_path || sysroot_path[0] == '\0') return -1;

    snprintf(db_dir, sizeof(db_dir), "%.*s/srv/lib/runepkg_dir/runepkg_db",
             (int)(PATH_MAX - 64), sysroot_path);
    runepkg_util_create_dir_recursive(db_dir, 0755);

    snprintf(bin_path, sizeof(bin_path), "%.*s/psychic_matrix.bin",
             (int)PATH_MAX, db_dir);
    snprintf(txt_path, sizeof(txt_path), "%.*s/psychic_matrix.txt",
             (int)PATH_MAX, db_dir);

    records = (PsychicMatrixRecord *)calloc(PSYCHIC_MAX_MATRIX_ENTRIES, sizeof(PsychicMatrixRecord));
    if (!records) return -1;

    psychic_scan_installed_pkgs(db_dir, records, &count, PSYCHIC_MAX_MATRIX_ENTRIES);

    snprintf(scan_dir, sizeof(scan_dir), "%.*s/usr/include", (int)(PATH_MAX - 32), sysroot_path);
    psychic_scan_include_tree(scan_dir, "", records, &count, PSYCHIC_MAX_MATRIX_ENTRIES);

    if (gnu_triplet && gnu_triplet[0] != '\0') {
        snprintf(scan_dir, sizeof(scan_dir), "%.*s/usr/include/%.*s",
                 (int)(PATH_MAX / 2), sysroot_path, 128, gnu_triplet);
        psychic_scan_include_tree(scan_dir, "", records, &count, PSYCHIC_MAX_MATRIX_ENTRIES);

        snprintf(scan_dir, sizeof(scan_dir), "%.*s/usr/lib/%.*s",
                 (int)(PATH_MAX / 2), sysroot_path, 128, gnu_triplet);
        psychic_scan_flat_dir(scan_dir, records, &count, PSYCHIC_MAX_MATRIX_ENTRIES);
    }

    snprintf(scan_dir, sizeof(scan_dir), "%.*s/usr/bin", (int)(PATH_MAX - 32), sysroot_path);
    psychic_scan_flat_dir(scan_dir, records, &count, PSYCHIC_MAX_MATRIX_ENTRIES);

    hdr.magic[0] = 'P';
    hdr.magic[1] = 'S';
    hdr.magic[2] = 'Y';
    hdr.magic[3] = 'C';
    hdr.version = PSYCHIC_MATRIX_VERSION;
    hdr.entry_count = (unsigned int)count;
    hdr.total_bytes = sizeof(PsychicMatrixHeader) + (count * 256);

    f = fopen(bin_path, "wb");
    if (!f) {
        free(records);
        return -1;
    }

    if (fwrite(&hdr, sizeof(PsychicMatrixHeader), 1, f) != 1) {
        fclose(f);
        free(records);
        return -1;
    }

    for (i = 0; i < count; i++) {
        char entry_buf[256];
        memset(entry_buf, 0, sizeof(entry_buf));
        snprintf(entry_buf, sizeof(entry_buf), "%.220s:%d", records[i].name, records[i].status);
        fwrite(entry_buf, 256, 1, f);
    }

    fclose(f);
    write_psychic_matrix_txt(txt_path, sysroot_path, gnu_triplet, records, count);
    free(records);

    runepkg_util_log_verbose("[psychic] Scanned sysroot and serialized psychic_matrix.bin + psychic_matrix.txt (%lu entries)\n",
                             (unsigned long)count);
    return 0;
}

int runepkg_psychic_query_matrix_bin(const char *sysroot_path, const char *file_name) {
    char bin_path[PATH_MAX + 64];
    FILE *f;
    PsychicMatrixHeader hdr;
    struct stat st;
    size_t i;

    if (!sysroot_path || !file_name || file_name[0] == '\0') return -1;

    snprintf(bin_path, sizeof(bin_path), "%.*s/srv/lib/runepkg_dir/runepkg_db/psychic_matrix.bin",
             (int)(PATH_MAX - 64), sysroot_path);
    if (stat(bin_path, &st) != 0) return 0;

    f = fopen(bin_path, "rb");
    if (!f) return 0;

    if (fread(&hdr, sizeof(PsychicMatrixHeader), 1, f) != 1) {
        fclose(f);
        return 0;
    }

    if (memcmp(hdr.magic, "PSYC", 4) != 0 || hdr.version != PSYCHIC_MATRIX_VERSION) {
        fclose(f);
        return 0;
    }

    if (st.st_size < (off_t)hdr.total_bytes) {
        fclose(f);
        return 0;
    }

    for (i = 0; i < hdr.entry_count; i++) {
        char entry_buf[256];
        char *colon;
        if (fread(entry_buf, 256, 1, f) != 1) break;
        entry_buf[255] = '\0';
        colon = strrchr(entry_buf, ':');
        if (colon && colon != entry_buf) {
            *colon = '\0';
            if (strcmp(entry_buf, file_name) == 0) {
                int status = atoi(colon + 1);
                fclose(f);
                return status;
            }
        }
    }

    fclose(f);
    return 0;
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
