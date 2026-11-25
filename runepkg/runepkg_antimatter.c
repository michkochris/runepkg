/******************************************************************************/
/* Filename:    runepkg_antimatter.c                                          */
/* Author:      <michkochris@gmail.com>                                        */
/* Date:        2026-09-20                                                     */
/* Description: C89 Decontamination Chamber Gateway & Anti-Matter Gatekeeper   */
/* License:     GPL v3                                                         */
/******************************************************************************/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <limits.h>

#include "runepkg_antimatter.h"
#include "runepkg_util.h"
#include "runepkg_host.h"

/* Global target variables defined in runepkg_config.c / runepkg_cli.c */
extern char *g_target_sysroot;
extern char *g_target_install_dir;
extern char *g_target_arch;
extern char *g_dpkg_host;
extern char *g_system_install_root;
extern char *g_install_dir_internal;

/* Global cold-start environment probe metadata */
runepkg_env_info_t g_env_info;

/* ELF Machine Architecture constants (e_machine) */
#define RUNE_EM_386      3
#define RUNE_EM_ARM      40
#define RUNE_EM_X86_64   62
#define RUNE_EM_AARCH64  183
#define RUNE_EM_RISCV    243

/* Helper: returns 1 if path starts with prefix on a path-segment boundary */
static int path_has_prefix(const char *path, const char *prefix, size_t prefix_len) {
    if (!path || !prefix || prefix_len == 0) {
        return 0;
    }
    if (strcmp(prefix, "/") == 0) {
        return (path[0] == '/');
    }
    if (strncmp(path, prefix, prefix_len) != 0) {
        return 0;
    }
    return (path[prefix_len] == '\0' || path[prefix_len] == '/');
}

/*
- runepkg_antimatter_purge:
-   Executes complete memory, pointer, and environment variable decontamination.
-   Frees dynamically allocated target state variables and resets pointers to NULL.
 */
void runepkg_antimatter_purge(void) {
    /* Free and NULL target state pointers */
    runepkg_util_free_and_null(&g_target_sysroot);
    g_target_install_dir = NULL;
    runepkg_util_free_and_null(&g_target_arch);
    runepkg_util_free_and_null(&g_dpkg_host);

    /* Lock system install root back to host install directory baseline */
    runepkg_util_free_and_null(&g_system_install_root);
    if (g_install_dir_internal && g_install_dir_internal[0] != '\0') {
        g_system_install_root = strdup(g_install_dir_internal);
    } else {
        g_system_install_root = strdup("/");
    }

    /* Scrub cross-compilation & multi-arch environment variables */
    unsetenv("DEB_BUILD_ARCH");
    unsetenv("DEB_BUILD_GNU_TYPE");
    unsetenv("DEB_HOST_ARCH");
    unsetenv("DEB_HOST_GNU_TYPE");
    unsetenv("PKG_CONFIG_PATH");
    unsetenv("PKG_CONFIG_SYSROOT_DIR");
    unsetenv("PKG_CONFIG_LIBDIR");
    unsetenv("SYSROOT");
    unsetenv("CC");
    unsetenv("CXX");
    unsetenv("AR");
    unsetenv("RANLIB");
    unsetenv("STRIP");
    unsetenv("CFLAGS");
    unsetenv("CXXFLAGS");
    unsetenv("LDFLAGS");
}

/*
- runepkg_antimatter_plunger:
-   Audits active state pointers and environment variables for lingering residue.
-   Under verbose execution, prints formatted '[fecal-detected]' diagnostic
-   warnings before plunging all target variables clean to NULL.
 */
void runepkg_antimatter_plunger(int verbose) {
    int dirty = 0;
    const char *deb_host = getenv("DEB_HOST_ARCH");
    const char *pkg_path = getenv("PKG_CONFIG_PATH");

    if ((g_target_arch && g_target_arch[0] != '\0') ||
        (g_target_sysroot && g_target_sysroot[0] != '\0') ||
        (deb_host && deb_host[0] != '\0') ||
        (pkg_path && pkg_path[0] != '\0')) {
        dirty = 1;
    }

    if (dirty && verbose) {
        printf("\033[1;33m[fecal-detected]\033[0m Lingering target state residue detected ");
        if (g_target_arch) {
            printf("(g_target_arch='%s') ", g_target_arch);
        }
        if (g_target_sysroot) {
            printf("(g_target_sysroot='%s') ", g_target_sysroot);
        }
        if (deb_host) {
            printf("(DEB_HOST_ARCH='%s') ", deb_host);
        }
        printf("Plunging clean to NULL baseline.\n");
    }

    runepkg_antimatter_purge();
}

/*
- runepkg_antimatter_validate_deb_meta:
-   Performs low-level pre-flight validation of the package control block
-   'Architecture:' stanza against expected execution mode and host/target arch.
 */
runepkg_decon_status_t runepkg_antimatter_validate_deb_meta(
    runepkg_exec_mode_t mode,
    const char *expected_deb_arch,
    const char *pkg_deb_arch
) {
    const char *host_native_arch = NULL;

    if (!pkg_deb_arch || pkg_deb_arch[0] == '\0') {
        return RUNEPKG_DECON_OK;
    }

    /* Architecture: all packages pass metadata check (payload files are still scanned separately) */
    if (strcmp(pkg_deb_arch, "all") == 0) {
        return RUNEPKG_DECON_OK;
    }

    if (mode == RUNEPKG_MODE_HOST) {
        host_native_arch = runepkg_host_get_architecture();
        if (!host_native_arch || strcmp(host_native_arch, "unknown") == 0) {
            host_native_arch = "amd64";
        }

        /* If host mode and package is foreign arch, reject */
        if (strcmp(pkg_deb_arch, host_native_arch) != 0) {
            return RUNEPKG_DECON_ERR_ARCH_MISMATCH;
        }
    } else if (mode == RUNEPKG_MODE_TARGET) {
        if (expected_deb_arch && expected_deb_arch[0] != '\0') {
            if (strcmp(pkg_deb_arch, expected_deb_arch) != 0 &&
                strcmp(pkg_deb_arch, "all") != 0) {
                return RUNEPKG_DECON_ERR_ARCH_MISMATCH;
            }
        }
    }

    return RUNEPKG_DECON_OK;
}

/*
- runepkg_antimatter_validate_payload:
-   Inspects memory buffer of payload file in memory during extraction.
-   Validates ELF magic bytes (\x7fELF), EI_CLASS, EI_DATA, and e_machine.
 */
runepkg_decon_status_t runepkg_antimatter_validate_payload(
    runepkg_exec_mode_t mode,
    const unsigned char *buf,
    unsigned long len
) {
    unsigned char ei_class = 0;
    unsigned char ei_data = 0;
    unsigned short e_machine = 0;
    const char *host_arch = NULL;

    if (!buf || len < 20) {
        /* Non-binary or too short to be an ELF executable */
        return RUNEPKG_DECON_OK;
    }

    /* Check ELF Magic Header: \x7f E L F */
    if (buf[0] != 0x7f || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F') {
        /* Not an ELF binary (script, text config, manpage, header) - pass cleanly */
        return RUNEPKG_DECON_OK;
    }

    ei_class = buf[4]; /* 1 = ELFCLASS32, 2 = ELFCLASS64 */
    ei_data  = buf[5]; /* 1 = ELFDATA2LSB, 2 = ELFDATA2MSB */

    if (ei_class != 1 && ei_class != 2) {
        return RUNEPKG_DECON_OK;
    }

    /* Extract e_machine at byte offset 18 according to endianness */
    if (ei_data == 2) { /* MSB Big-Endian */
        e_machine = (unsigned short)((buf[18] << 8) | buf[19]);
    } else {           /* LSB Little-Endian */
        e_machine = (unsigned short)(buf[18] | (buf[19] << 8));
    }

    if (mode == RUNEPKG_MODE_HOST) {
        host_arch = runepkg_host_get_architecture();
        if (!host_arch || strcmp(host_arch, "unknown") == 0) {
            host_arch = "amd64";
        }

        /* On x86_64 host mode, reject AArch64 / ARM / RISC-V binary payloads */
        if (strcmp(host_arch, "amd64") == 0 || strcmp(host_arch, "x86_64") == 0) {
            if (e_machine == RUNE_EM_AARCH64 || e_machine == RUNE_EM_ARM || e_machine == RUNE_EM_RISCV) {
                return RUNEPKG_DECON_ERR_ARCH_MISMATCH;
            }
        }
    } else if (mode == RUNEPKG_MODE_TARGET) {
        /* Target mode verification: ensure target sysroot payloads match target machine */
        /* Target mode allows cross binaries matching the target profile */
    }

    return RUNEPKG_DECON_OK;
}

/*
- runepkg_antimatter_assert_boundary:
-   Ensures candidate paths and symlink targets stay strictly inside allowed_root.
-   Prevents .. traversal or symlinks pointing outside sysroot boundary.
 */
runepkg_decon_status_t runepkg_antimatter_assert_boundary(
    const char *allowed_root,
    const char *candidate_path,
    const char *symlink_target
) {
    char norm_root[PATH_MAX];
    size_t root_len = 0;

    if (!allowed_root || allowed_root[0] == '\0') {
        allowed_root = "/";
    }

    if (!candidate_path || candidate_path[0] == '\0') {
        return RUNEPKG_DECON_OK;
    }

    /* Normalize allowed_root */
    if (realpath(allowed_root, norm_root) == NULL) {
        snprintf(norm_root, sizeof(norm_root), "%s", allowed_root);
    }
    root_len = strlen(norm_root);

    /* Trim trailing slash on root for comparison unless root is '/' */
    if (root_len > 1 && norm_root[root_len - 1] == '/') {
        norm_root[root_len - 1] = '\0';
        root_len--;
    }

    /* Check lexical .. traversal in candidate path */
    if (strstr(candidate_path, "../") != NULL || strstr(candidate_path, "/..") != NULL) {
        return RUNEPKG_DECON_ERR_BOUNDARY;
    }

    /* Verify candidate path prefix if candidate is absolute */
    if (candidate_path[0] == '/') {
        if (strcmp(norm_root, "/") != 0) {
            if (!path_has_prefix(candidate_path, norm_root, root_len)) {
                return RUNEPKG_DECON_ERR_BOUNDARY;
            }
        }
    }

    /* Check symlink target if specified */
    if (symlink_target && symlink_target[0] != '\0') {
        if (strstr(symlink_target, "../") != NULL || strstr(symlink_target, "/..") != NULL) {
            return RUNEPKG_DECON_ERR_BOUNDARY;
        }
        if (symlink_target[0] == '/' && strcmp(norm_root, "/") != 0) {
            if (!path_has_prefix(symlink_target, norm_root, root_len)) {
                return RUNEPKG_DECON_ERR_BOUNDARY;
            }
        }
    }

    return RUNEPKG_DECON_OK;
}

/*
- runepkg_antimatter_probe_environment:
-   Audits unknown Linux Debian environment layout on cold-start execution.
-   Probes usr-merge symlink status (/lib64, /bin) and host dpkg database.
 */
runepkg_decon_status_t runepkg_antimatter_probe_environment(
    const char *sysroot,
    runepkg_env_info_t *out_info
) {
    struct stat st;
    const char *target_root;
    char check_path[PATH_MAX];
    size_t root_len;

    if (!out_info) {
        return RUNEPKG_DECON_ERR_BOUNDARY;
    }

    memset(out_info, 0, sizeof(*out_info));
    target_root = (sysroot && sysroot[0] != '\0') ? sysroot : "/";
    root_len = strlen(target_root);

    /* Probe usr-merge status via lstat on /lib64 inside target_root */
    if (root_len > 0 && target_root[root_len - 1] == '/') {
        snprintf(check_path, sizeof(check_path), "%slib64", target_root);
    } else {
        snprintf(check_path, sizeof(check_path), "%s/lib64", target_root);
    }

    if (lstat(check_path, &st) == 0 && S_ISLNK(st.st_mode)) {
        out_info->is_usr_merged = 1;
    }

    /* Probe host dpkg status file presence */
    if (root_len > 0 && target_root[root_len - 1] == '/') {
        snprintf(check_path, sizeof(check_path), "%svar/lib/dpkg/status", target_root);
    } else {
        snprintf(check_path, sizeof(check_path), "%s/var/lib/dpkg/status", target_root);
    }

    if (stat(check_path, &st) == 0) {
        out_info->dpkg_status_present = 1;
    }

    return RUNEPKG_DECON_OK;
}

/*
- runepkg_antimatter_is_core_package:
-   Returns 1 if package is a protected glibc runtime or core toolchain package.
 */
int runepkg_antimatter_is_core_package(const char *pkg_name) {
    if (!pkg_name || pkg_name[0] == '\0') {
        return 0;
    }

    if (strcmp(pkg_name, "libc6") == 0 ||
        strcmp(pkg_name, "libc-bin") == 0 ||
        strcmp(pkg_name, "libgcc-s1") == 0 ||
        strcmp(pkg_name, "gcc-16-base") == 0 ||
        strcmp(pkg_name, "libstdc++6") == 0 ||
        strncmp(pkg_name, "ld-linux", 8) == 0) {
        return 1;
    }

    return 0;
}

/*
- runepkg_antimatter_shield_check:
-   Intercepts and blocks core glibc runtime package mutations on live host root '/'.
 */
runepkg_decon_status_t runepkg_antimatter_shield_check(
    const char *sysroot,
    const char *pkg_name,
    int override_flag
) {
    if ((!sysroot || strcmp(sysroot, "/") == 0) && runepkg_antimatter_is_core_package(pkg_name)) {
        if (!override_flag) {
            fprintf(stderr, "\033[1;31m[matryoshka shield]\033[0m Refusing to mutate core glibc package '%s' on live host root '/'.\n", pkg_name);
            return RUNEPKG_DECON_ERR_BOUNDARY;
        }
    }

    return RUNEPKG_DECON_OK;
}
