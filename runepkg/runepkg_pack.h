/******************************************************************************
- Filename:    runepkg_pack.h
- Author:      <michkochris@gmail.com>
- Date:        2025-01-02
- Description: Package archive parsing structures, deb info metadata types, and extraction header declarations
- LICENSE:     GPLv3
-
- GPLv3 is the platinum shield of open source software and freedom.
- Turning code into a permanent public asset that protects builders and users
- from patent warfare and locked-down hardware.
-
 ******************************************************************************/

#ifndef RUNEPKG_PACK_H
#define RUNEPKG_PACK_H

#include "runepkg_portable.h"
#include <stdio.h>
#include <stdlib.h>
#include "runepkg_hash.h" /* We now include the header with the unified PkgInfo struct */

/* Define PATH_MAX if not defined */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Recommended Internal Struct for pkginfo.bin Header (from self-completing-binary.txt) */
typedef struct {
    uint32_t magic;      /* 0x52554E45 ("RUNE") */
    char pkgname[64];    /* Fixed width for zero-offset seeking */
    char version[32];    /* Fixed width */
    uint64_t data_start; /* Offset to actual package payload */
} PkgHeader;

/* --- Function Prototypes --- */

/**
 * @brief Initializes a package info structure with NULL values.
 * @param pkg_info Pointer to the package info structure to initialize.
 */
void runepkg_pack_init_package_info(PkgInfo *pkg_info);

/**
 * @brief Frees all allocated memory in a package info structure.
 * @param pkg_info Pointer to the package info structure to free.
 */
void runepkg_pack_free_package_info(PkgInfo *pkg_info);

/**
 * @brief Extracts a .deb package and collects package information.
 * * This function performs the complete workflow:
 * 1. Extracts the .deb file to the control directory
 * 2. Parses the control file to collect package metadata
 * 3. Populates the package info structure
 *
 * @param deb_path The full path to the .deb package file.
 * @param control_dir The directory where the package should be extracted (from config).
 * @param pkg_info Pointer to package info structure to populate.
 * @return 0 on success, -1 on failure.
 */
int runepkg_pack_extract_and_collect_info(const char *deb_path, const char *control_dir, PkgInfo *pkg_info);

/**
 * @brief If cleanup_extract is enabled in config, remove this package's extraction tree under control_dir.
 */
void runepkg_pack_cleanup_extraction_workspace(const PkgInfo *pkg_info);

/**
 * @brief Parses a control file and extracts package information.
 * @param control_file_path The path to the control file.
 * @param pkg_info Pointer to package info structure to populate.
 * @return 0 on success, -1 on failure.
 */
int runepkg_pack_parse_control_file(const char *control_file_path, PkgInfo *pkg_info);

/**
 * @brief Prints package information in a readable format.
 * @param pkg_info Pointer to the package info structure to display.
 */
void runepkg_pack_print_package_info(const PkgInfo *pkg_info);

/**
 * @brief Creates a unique extraction directory name based on package info.
 * @param base_dir The base directory where extraction should occur.
 * @param deb_filename The name of the .deb file.
 * @return A dynamically allocated string with the full extraction path, or NULL on error.
 * The caller is responsible for freeing the returned string.
 */
char *runepkg_pack_create_extraction_path(const char *base_dir, const char *deb_filename);

/**
 * @brief Collects a list of all files contained in the package data directory.
 * @param data_dir_path The path to the extracted data directory.
 * @param pkg_info Pointer to package info structure to populate with file list.
 * @return 0 on success, -1 on failure.
 */
int runepkg_pack_collect_file_list(const char *data_dir_path, PkgInfo *pkg_info);

#endif /* RUNEPKG_PACK_H */
