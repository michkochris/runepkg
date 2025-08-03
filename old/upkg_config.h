#ifndef UPKG_CONFIG_H
#define UPKG_CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <limits.h> // For PATH_MAX

// Define PATH_MAX if not defined
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// --- Global Path Variables Declarations ---
extern char *g_upkg_base_dir;
extern char *g_control_dir;
extern char *g_unpack_dir;
extern char *g_db_dir; // New declaration for the database directory
extern char *g_install_dir_internal;
extern char *g_system_install_root;

// --- Function Prototypes for Configuration Management ---

/**
 * @brief Initializes upkg by loading configuration and creating necessary directories.
 *
 * This is a high-level function that encapsulates the logic for loading the
 * cascading configuration file and then creating all the directories specified
 * by the paths in that configuration.
 */
void upkg_init_paths();

/**
 * @brief Loads essential upkg path configurations from a cascading configuration file.
 *
 * This function determines the configuration file path by checking:
 * 1. The UPKG_CONFIG_PATH environment variable.
 * 2. The system-wide location at /etc/upkg/upkgconfig.
 * 3. The user-specific location at ~/.upkgconfig.
 * Once a file is found, it reads the critical path variables (upkg_dir, control_dir, etc.)
 * from that file and populates the global path variables.
 *
 * @return 0 on successful loading of all critical paths, -1 on failure.
 */
int load_upkg_config();

/**
 * @brief Cleans up and frees all memory allocated for the global path variables.
 *
 * This function should be called during program exit to prevent memory leaks.
 */
void upkg_cleanup_paths();

#endif // UPKG_CONFIG_H
