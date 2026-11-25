/******************************************************************************/
/* Filename:    runepkg_antimatter.h                                          */
/* Author:      <michkochris@gmail.com>                                        */
/* Date:        2026-09-20                                                     */
/* Description: C89 Decontamination Chamber Gateway & Anti-Matter Gatekeeper   */
/* License:     GPL v3                                                         */
/******************************************************************************/

#ifndef RUNEPKG_ANTIMATTER_H
#define RUNEPKG_ANTIMATTER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Execution mode enumeration:
 *   RUNEPKG_MODE_HOST   - Standard host installation / management (target = /)
 *   RUNEPKG_MODE_TARGET - Cross-compilation target execution (--target active)
 */
typedef enum {
    RUNEPKG_MODE_HOST = 0,
    RUNEPKG_MODE_TARGET = 1
} runepkg_exec_mode_t;

/*
 * Decontamination status codes:
 *   RUNEPKG_DECON_OK                 - Operation / validation passed cleanly
 *   RUNEPKG_DECON_ERR_ARCH_MISMATCH  - Low-level architecture mismatch detected
 *   RUNEPKG_DECON_ERR_BOUNDARY       - Path / symlink escape attempt detected
 *   RUNEPKG_DECON_ERR_ENV            - Cross-architecture environment leak
 */
typedef enum {
    RUNEPKG_DECON_OK = 0,
    RUNEPKG_DECON_ERR_ARCH_MISMATCH = 1,
    RUNEPKG_DECON_ERR_BOUNDARY = 2,
    RUNEPKG_DECON_ERR_ENV = 3
} runepkg_decon_status_t;

/*
 * runepkg_antimatter_purge:
 *   Single-function in-memory decontamination routine modeled after the pkg_info
 *   teardown pattern (runepkg_util_free_and_null). Systematic memory, pointer,
 *   and environment variable scrubbing back to a pure host baseline.
 */
void runepkg_antimatter_purge(void);

/*
 * runepkg_antimatter_plunger:
 *   Audits active state profiles for lingering cross-target residue or dirty
 *   pointers (g_target_arch, g_target_sysroot, DEB_HOST_ARCH, PKG_CONFIG_PATH).
 *   If verbose != 0, logs a [fecal-detected] diagnostic warning prior to
 *   plunging all state clean to NULL.
 */
void runepkg_antimatter_plunger(int verbose);

/*
 * runepkg_antimatter_validate_deb_meta:
 *   Performs low-level pre-flight validation of the package control block
 *   'Architecture:' field against the active execution mode and host/target arch.
 */
runepkg_decon_status_t runepkg_antimatter_validate_deb_meta(
    runepkg_exec_mode_t mode,
    const char *expected_deb_arch,
    const char *pkg_deb_arch
);

/*
 * runepkg_antimatter_validate_payload:
 *   Low-level memory inspection of binary ELF headers (\x7fELF, EI_CLASS,
 *   EI_DATA, and e_machine) during staging before writing to destination paths.
 *   Non-ELF files (scripts, configs, text) pass through cleanly.
 */
runepkg_decon_status_t runepkg_antimatter_validate_payload(
    runepkg_exec_mode_t mode,
    const unsigned char *buf,
    unsigned long len
);

/*
 * runepkg_antimatter_assert_boundary:
 *   Ensures that resolved destination paths and symlink targets stay strictly
 *   within the allowed_root (/ or <sysroot>), preventing .. traversals or
 *   symlink escape attempts to host root.
 */
runepkg_decon_status_t runepkg_antimatter_assert_boundary(
    const char *allowed_root,
    const char *candidate_path,
    const char *symlink_target
);

/* Environment Probing Structure for Cold-Start Execution */
typedef struct {
    int is_usr_merged;             /* 1 if /bin, /lib, /lib64 are symlinks to /usr/ */
    char host_glibc_version[32];   /* Probed host glibc version string (e.g. "2.43") */
    char dynamic_linker_path[128]; /* Probed dynamic linker (/lib64/ld-linux-x86-64.so.2) */
    int dpkg_status_present;       /* 1 if host /var/lib/dpkg/status exists */
} runepkg_env_info_t;

extern runepkg_env_info_t g_env_info;

/* Cold-Start Environment Probe: Audits unknown Debian host layout before operations */
runepkg_decon_status_t runepkg_antimatter_probe_environment(
    const char *sysroot,
    runepkg_env_info_t *out_info
);

/* Checks if a package is a protected core runtime/toolchain package */
int runepkg_antimatter_is_core_package(const char *pkg_name);

/* Verifies live-root shield for core packages when sysroot is "/" */
runepkg_decon_status_t runepkg_antimatter_shield_check(
    const char *sysroot,
    const char *pkg_name,
    int override_flag
);

#ifdef __cplusplus
}
#endif

#endif /* RUNEPKG_ANTIMATTER_H */
