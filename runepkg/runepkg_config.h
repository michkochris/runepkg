#/******************************************************************************
 * Filename:    runepkg_config.h
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Configuration declarations for runepkg
 * LICENSE:     GPL v3
 * THIS IS FREE SOFTWARE; YOU CAN REDISTRIBUTE IT AND/OR MODIFY IT UNDER
 * THE TERMS OF THE GNU GENERAL PUBLIC LICENSE AS PUBLISHED BY THE FREE
 * SOFTWARE FOUNDATION; EITHER VERSION 3 OF THE LICENSE, OR (AT YOUR OPTION)
 * ANY LATER VERSION.
 * THIS PROGRAM IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. SEE THE
 * GNU GENERAL PUBLIC LICENSE FOR MORE DETAILS.
 ******************************************************************************/

#ifndef RUNEPKG_CONFIG_H
#define RUNEPKG_CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <limits.h> // For PATH_MAX

// Define PATH_MAX if not defined
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// --- Global Path Variables Declarations ---
extern char *g_runepkg_base_dir;
extern char *g_control_dir;
extern char *g_runepkg_db_dir; // Database directory for persistent storage
extern char *g_install_dir_internal;
extern char *g_system_install_root;
extern char *g_pkglist_txt_path;
extern char *g_pkglist_bin_path;

// --- Function Prototypes for Configuration Management ---

/**
 * @brief Initializes runepkg by loading configuration and creating necessary directories.
 *
 * This is a high-level function that encapsulates the logic for loading the
 * cascading configuration file and then creating all the directories specified
 * by the paths in that configuration.
 */
void runepkg_init_paths();

/**
 * @brief Loads essential runepkg path configurations from a cascading configuration file.
 *
 * This function determines the configuration file path by checking:
 * 1. The RUNEPKG_CONFIG_PATH environment variable.
 * 2. The system-wide location at /etc/runepkg/runepkgconfig.
 * 3. The user-specific location at ~/.runepkgconfig.
 * Once a file is found, it reads the critical path variables (runepkg_dir, control_dir, etc.)
 * from that file and populates the global path variables.
 *
 * @return 0 on successful loading of all critical paths, -1 on failure.
 */
int runepkg_config_load();

/**
 * @brief Cleans up and frees all memory allocated for the global path variables.
 *
 * This function should be called during program exit to prevent memory leaks.
 */
void runepkg_config_cleanup();

/**
 * @brief Gets the path to the configuration file currently in use.
 *
 * This function determines which configuration file is being used by checking:
 * 1. The RUNEPKG_CONFIG_PATH environment variable.
 * 2. The system-wide location at /etc/runepkg/runepkgconfig.
 * 3. The user-specific location at ~/.runepkgconfig.
 *
 * @return A dynamically allocated string containing the config file path,
 *         or NULL if no config file is found. The caller is responsible for freeing the returned string.
 */
char *runepkg_get_config_file_path();

#endif // RUNEPKG_CONFIG_H
