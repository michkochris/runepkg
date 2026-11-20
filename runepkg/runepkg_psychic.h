/******************************************************************************
 * Filename:    runepkg_psychic.h
 * Author:      <michkochris@gmail.com>
 * Date:        2026-09-04
 * Description: The Psychic Debian Subsystem for runepkg - Multi-Arch prophecy,
 *              scaffolding pre-flight checks, and virtual package telepathy.
 * LICENSE:     GPL v3
 ******************************************************************************/

#ifndef RUNEPKG_PSYCHIC_H
#define RUNEPKG_PSYCHIC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Target Architecture Identifiers */
#define RUNEPKG_ARCH_AMD64   "amd64"
#define RUNEPKG_ARCH_ARM64   "arm64"
#define RUNEPKG_ARCH_RISCV64 "riscv64"
#define RUNEPKG_ARCH_ARMHF   "armhf"
#define RUNEPKG_ARCH_I386    "i386"

/**
 * @brief Detect or determine the target architecture for a sysroot / alternate root.
 * @param sysroot_path Target install root path.
 * @param arch_buf Output buffer for architecture string.
 * @param buf_sz Size of output buffer.
 * @return 0 on success, -1 on error.
 */
int runepkg_psychic_detect_target_arch(const char *sysroot_path, char *arch_buf, size_t buf_sz);

/**
 * @brief Pre-flight Scaffolding Prophecy: Projects Multi-Arch FHS tree and links dynamic linkers.
 * @param sysroot_path Target install root path (`target_install_dir`).
 * @param target_arch Target architecture string (e.g. "arm64").
 * @return 0 on success, -1 on error.
 */
int runepkg_psychic_prophecy_check(const char *sysroot_path, const char *target_arch);

/**
 * @brief Virtual Package Telepathy: Resolves Debian virtual package names to concrete implementations.
 * @param virtual_pkg Virtual package name (e.g. "default-mta", "libc-dev").
 * @param concrete_buf Output buffer for concrete package name.
 * @param buf_sz Size of output buffer.
 * @return 0 if resolved, -1 if unknown.
 */
int runepkg_psychic_resolve_virtual(const char *virtual_pkg, char *concrete_buf, size_t buf_sz);

/**
 * @brief Normalizes arch string (e.g. "aarch64" -> "arm64", "x86_64" -> "amd64") for Debian repo queries.
 */
const char *runepkg_psychic_normalize_deb_arch(const char *arch);

#ifdef __cplusplus
}
#endif

#endif /* RUNEPKG_PSYCHIC_H */
