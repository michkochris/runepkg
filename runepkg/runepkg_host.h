/******************************************************************************
 * Filename:    runepkg_host.h
 * Author:      <michkochris@gmail.com>
 * Date:        started 01-05-2025
 * Description: Host integration and environment management for runepkg
 *
 * Copyright (c) 2025 runepkg (Runar Linux) All rights reserved.
 * GPLV3
 ******************************************************************************/

#ifndef RUNEPKG_HOST_H
#define RUNEPKG_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "runepkg_portable.h"
#include <time.h>
#include "runepkg_pack.h"

/**
 * @brief Represents a package record on the host system.
 */
typedef struct {
    char name[256];
    char version[64];
    char architecture[32];
    char status[64];      /* e.g., "installed", "removed", "pending" */
    time_t install_time;
    char install_root[4096];
} HostPackageInfo;

/**
 * @brief Initializes the host integration layer.
 * Detects host architecture, OS, and sets up base environment.
 * @return 0 on success, -1 on failure.
 */
int runepkg_host_init(void);

/**
 * @brief Synchronizes the host package database with the current system state.
 * Scans the local storage and host environment for consistency.
 * @return 0 on success, -1 on failure.
 */
int runepkg_host_sync(void);

/**
 * @brief Collects all packages currently installed on the host.
 * @param out_packages Pointer to an array of HostPackageInfo pointers (newly allocated).
 * @param out_count Number of packages found.
 * @return 0 on success, -1 on failure.
 */
int runepkg_host_collect_installed(HostPackageInfo ***out_packages, int *out_count);

/**
 * @brief Registers a package installation with the host system.
 * @param pkg_info Information about the package being installed.
 * @return 0 on success, -1 on failure.
 */
int runepkg_host_register_install(const PkgInfo *pkg_info);

/**
 * @brief Unregisters a package removal from the host system.
 * @param pkg_name Name of the package being removed.
 * @return 0 on success, -1 on failure.
 */
int runepkg_host_unregister_removal(const char *pkg_name);

/**
 * @brief Queries the host for specific package information.
 * @param pkg_name Name of the package to query.
 * @param out_info Pointer to store the result.
 * @return 1 if found, 0 if not, -1 on error.
 */
int runepkg_host_query_package(const char *pkg_name, HostPackageInfo *out_info);

/**
 * @brief Gets the host system architecture (e.g., "amd64", "arm64").
 * @return A constant string representing the architecture.
 */
const char *runepkg_host_get_architecture(void);

/**
 * @brief Checks if the current user has host administrative privileges.
 * @return 1 if root/sudo, 0 otherwise.
 */
int runepkg_host_is_privileged(void);

/**
 * @brief Frees a list of HostPackageInfo pointers returned by collect_installed.
 */
void runepkg_host_free_package_list(HostPackageInfo **packages, int count);

#ifdef __cplusplus
}
#endif

#endif /* RUNEPKG_HOST_H */
