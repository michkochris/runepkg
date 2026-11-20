/******************************************************************************
 * Filename:    runepkg_psychic.c
 * Author:      <michkochris@gmail.com>
 * Date:        2026-09-04
 * Description: Implementation of the Psychic Debian Subsystem (runepkg_psychic)
 * LICENSE:     GPL v3
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

#include "runepkg_psychic.h"
#include "runepkg_util.h"

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
    char *nl;

    if (!arch_buf || buf_sz == 0) return -1;

    /* 1. If sysroot_path is provided, check if an architecture is already hinted/staged */
    if (sysroot_path && sysroot_path[0] != '\0') {
        snprintf(marker_path, sizeof(marker_path), "%s/etc/runepkg_target_arch", sysroot_path);
        if (runepkg_util_file_exists(marker_path)) {
            f = fopen(marker_path, "r");
            if (f) {
                if (fgets(arch_buf, (int)buf_sz, f)) {
                    nl = strchr(arch_buf, '\n');
                    if (nl) *nl = '\0';
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

const char *runepkg_psychic_normalize_deb_arch(const char *arch) {
    const char *colon;
    size_t len;

    if (!arch) return "amd64";

    colon = strchr(arch, ':');
    if (colon) {
        len = (size_t)(colon - arch);
        if (len == 0) return "amd64";
        if ((len == 7 && strncmp(arch, "aarch64", 7) == 0) || (len == 5 && strncmp(arch, "arm64", 5) == 0)) return "arm64";
        if ((len == 6 && strncmp(arch, "x86_64", 6) == 0) || (len == 5 && strncmp(arch, "amd64", 5) == 0)) return "amd64";
        if (len == 7 && strncmp(arch, "riscv64", 7) == 0) return "riscv64";
        if ((len == 5 && strncmp(arch, "armhf", 5) == 0) || (len == 6 && strncmp(arch, "armv7l", 6) == 0)) return "armhf";
        if ((len == 4 && strncmp(arch, "i386", 4) == 0) || (len == 4 && strncmp(arch, "i686", 4) == 0)) return "i386";

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

    if (strcmp(arch, "aarch64") == 0 || strcmp(arch, "arm64") == 0) return "arm64";
    if (strcmp(arch, "x86_64") == 0 || strcmp(arch, "amd64") == 0) return "amd64";
    if (strcmp(arch, "riscv64") == 0) return "riscv64";
    if (strcmp(arch, "armhf") == 0 || strcmp(arch, "armv7l") == 0) return "armhf";
    if (strcmp(arch, "i386") == 0 || strcmp(arch, "i686") == 0) return "i386";
    return arch;
}
