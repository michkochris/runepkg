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

typedef enum {
    RUNEPKG_MA_NONE = 0,
    RUNEPKG_MA_NO,
    RUNEPKG_MA_SAME,
    RUNEPKG_MA_FOREIGN,
    RUNEPKG_MA_ALLOWED
} RunePkgMultiArchMode;

typedef struct {
    char *deb_arch;
    char *gnu_triplet;
} RunePkgTargetSpec;

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
 * @param target_arch Target architecture string (e.g. "arm64" or "arm64:aarch64-linux-gnu").
 * @return 0 on success, -1 on error.
 */
int runepkg_psychic_prophecy_check(const char *sysroot_path, const char *target_arch);

/**
 * @brief Anchors Multi-Arch symlinks (/lib/<gnu_triplet> -> ../usr/lib/<gnu_triplet>) and dynamic linkers.
 */
void runepkg_psychic_anchor_dynamic_linker(const char *sysroot_path, const char *deb_arch, const char *gnu_triplet);

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

/**
 * @brief Frees heap strings inside a RunePkgTargetSpec without freeing the struct itself.
 */
void runepkg_psychic_free_target_spec(RunePkgTargetSpec *spec);

/**
 * @brief Parses raw target_arch ("arm64" or "arm64:aarch64-linux-gnu") into deb_arch and gnu_triplet.
 * @return 0 on success, -1 on invalid input or allocation failure.
 */
int runepkg_psychic_parse_target_arch(const char *raw_target_arch, RunePkgTargetSpec *out_spec);

/**
 * @brief Splits "pkg:qual" into heap-allocated base package name and optional qualifier (NULL if none).
 * @return 0 on success, -1 on invalid input or allocation failure.
 */
int runepkg_psychic_parse_qualifier(const char *pkg_spec, char **out_pkg_name, char **out_qualifier);

/**
 * @brief Parses a Debian Multi-Arch control header value ("same", "foreign", "allowed", "no").
 */
RunePkgMultiArchMode runepkg_psychic_parse_multiarch_mode(const char *ma_field);

/**
 * @brief Resolves the effective architecture required for a dependency given qualifier and Multi-Arch mode.
 */
const char *runepkg_psychic_resolve_dependency_arch(const char *target_arch,
                                                    const char *host_arch,
                                                    const char *qualifier,
                                                    RunePkgMultiArchMode pkg_ma_mode);

/**
 * @brief Checks whether a candidate package stanza satisfies a dependency with a given qualifier.
 * @return 1 if satisfied, 0 if not satisfied.
 */
int runepkg_psychic_is_multiarch_satisfied(const char *candidate_arch,
                                           RunePkgMultiArchMode candidate_ma_mode,
                                           const char *qualifier,
                                           const char *target_arch,
                                           const char *host_arch);

/* --- psychic_matrix.bin Binary Format & Prophecy Declarations --- */

#define PSYCHIC_MATRIX_MAGIC 0x50535943 /* "PSYC" */
#define PSYCHIC_MATRIX_VERSION 1

typedef struct {
    char magic[4];        /* "PSYC" */
    unsigned int version; /* 1 */
    unsigned int entry_count;
    unsigned long total_bytes;
} PsychicMatrixHeader;

/**
 * @brief Generates and serializes psychic_matrix.bin in-place inside target DB directory.
 * @param sysroot_path Sysroot path (e.g. "/mnt/runepkg/aarch64-linux-gnu").
 * @param gnu_triplet Triplet string (e.g. "aarch64-linux-gnu").
 * @return 0 on success, -1 on failure.
 */
int runepkg_psychic_build_matrix_bin(const char *sysroot_path, const char *gnu_triplet);

/**
 * @brief Queries psychic_matrix.bin to check if a header or library file exists inside sysroot.
 * @param sysroot_path Sysroot path.
 * @param file_name Relative file path or header name (e.g. "zlib.h" or "libssl.so").
 * @return 1 if present, 0 if absent/missing, -1 on error.
 */
int runepkg_psychic_query_matrix_bin(const char *sysroot_path, const char *file_name);

/**
 * @brief Prophesies missing Debian package for missing build headers or library links.
 * @param sysroot_path Target sysroot path.
 * @param missing_item Missing header/link (e.g. "<zlib.h>" or "-lz").
 * @param deb_arch Target Debian architecture (e.g. "arm64").
 * @param out_pkg_buf Output buffer for recommended package string.
 * @param buf_sz Size of output buffer.
 * @return 0 on successful prophecy, -1 if no candidate found.
 */
int runepkg_psychic_prophesy_missing_package(const char *sysroot_path,
                                              const char *missing_item,
                                              const char *deb_arch,
                                              char *out_pkg_buf,
                                              size_t buf_sz);

#ifdef __cplusplus
}
#endif

#endif /* RUNEPKG_PSYCHIC_H */
