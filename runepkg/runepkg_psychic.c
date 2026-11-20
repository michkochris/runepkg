/****************************************************************--------------
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

int runepkg_psychic_detect_target_arch(const char *sysroot_path, char *arch_buf, size_t buf_sz) {
    if (!arch_buf || buf_sz == 0) return -1;

    /* 1. If sysroot_path is provided, check if an architecture is already hinted/staged */
    if (sysroot_path && sysroot_path[0] != '\0') {
        char marker_path[PATH_MAX];
        snprintf(marker_path, sizeof(marker_path), "%s/etc/runepkg_target_arch", sysroot_path);
        if (runepkg_util_file_exists(marker_path)) {
            FILE *f = fopen(marker_path, "r");
            if (f) {
                if (fgets(arch_buf, (int)buf_sz, f)) {
                    char *nl = strchr(arch_buf, '\n');
                    if (nl) *nl = '\0';
                    fclose(f);
                    return 0;
                }
                fclose(f);
            }
        }
    }

    /* 2. Fallback to host architecture detection via uname or default */
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

int runepkg_psychic_prophecy_check(const char *sysroot_path, const char *target_arch) {
    char dir_buf[PATH_MAX];
    char multiarch_tuple[64];

    if (!sysroot_path || sysroot_path[0] == '\0') return -1;
    if (!target_arch || target_arch[0] == '\0') target_arch = RUNEPKG_ARCH_AMD64;

    runepkg_util_log_verbose("[psychic] Initiating pre-flight prophecy check for target arch: %s in sysroot: %s\n",
        target_arch, sysroot_path);

    /* 1. Ensure base FHS directories exist */
    runepkg_util_create_dir_recursive(sysroot_path, 0755);

    snprintf(dir_buf, sizeof(dir_buf), "%s/etc", sysroot_path);
    runepkg_util_create_dir_recursive(dir_buf, 0755);

    snprintf(dir_buf, sizeof(dir_buf), "%s/var/lib/dpkg", sysroot_path);
    runepkg_util_create_dir_recursive(dir_buf, 0755);

    /* Store architecture marker */
    {
        char marker_path[PATH_MAX];
        snprintf(marker_path, sizeof(marker_path), "%s/etc/runepkg_target_arch", sysroot_path);
        {
            FILE *f = fopen(marker_path, "w");
            if (f) {
                fprintf(f, "%s\n", target_arch);
                fclose(f);
            }
        }
    }

    /* 2. Determine Debian Multi-Arch Tuple */
    if (strcmp(target_arch, RUNEPKG_ARCH_AMD64) == 0 || strcmp(target_arch, "x86_64") == 0) {
        runepkg_util_safe_strncpy(multiarch_tuple, "x86_64-linux-gnu", sizeof(multiarch_tuple));
    } else if (strcmp(target_arch, RUNEPKG_ARCH_ARM64) == 0 || strcmp(target_arch, "aarch64") == 0) {
        runepkg_util_safe_strncpy(multiarch_tuple, "aarch64-linux-gnu", sizeof(multiarch_tuple));
    } else if (strcmp(target_arch, RUNEPKG_ARCH_RISCV64) == 0) {
        runepkg_util_safe_strncpy(multiarch_tuple, "riscv64-linux-gnu", sizeof(multiarch_tuple));
    } else if (strcmp(target_arch, RUNEPKG_ARCH_ARMHF) == 0 || strcmp(target_arch, "armv7l") == 0) {
        runepkg_util_safe_strncpy(multiarch_tuple, "arm-linux-gnueabihf", sizeof(multiarch_tuple));
    } else if (strcmp(target_arch, RUNEPKG_ARCH_I386) == 0 || strcmp(target_arch, "i686") == 0) {
        runepkg_util_safe_strncpy(multiarch_tuple, "i386-linux-gnu", sizeof(multiarch_tuple));
    } else {
        runepkg_util_safe_strncpy(multiarch_tuple, "x86_64-linux-gnu", sizeof(multiarch_tuple));
    }

    /* 3. Project Multi-Arch FHS Tree */
    snprintf(dir_buf, sizeof(dir_buf), "%s/lib/%s", sysroot_path, multiarch_tuple);
    runepkg_util_create_dir_recursive(dir_buf, 0755);

    snprintf(dir_buf, sizeof(dir_buf), "%s/usr/lib/%s", sysroot_path, multiarch_tuple);
    runepkg_util_create_dir_recursive(dir_buf, 0755);

    snprintf(dir_buf, sizeof(dir_buf), "%s/usr/include/%s", sysroot_path, multiarch_tuple);
    runepkg_util_create_dir_recursive(dir_buf, 0755);

    runepkg_util_log_verbose("[psychic] Pre-flight prophecy check completed successfully for %s.\n", target_arch);
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
    if (!arch) return "amd64";
    if (strcmp(arch, "aarch64") == 0 || strcmp(arch, "arm64") == 0) return "arm64";
    if (strcmp(arch, "x86_64") == 0 || strcmp(arch, "amd64") == 0) return "amd64";
    if (strcmp(arch, "riscv64") == 0) return "riscv64";
    if (strcmp(arch, "armhf") == 0 || strcmp(arch, "armv7l") == 0) return "armhf";
    if (strcmp(arch, "i386") == 0 || strcmp(arch, "i686") == 0) return "i386";
    return arch;
}
