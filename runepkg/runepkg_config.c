#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdbool.h>

#include "runepkg_util.h"

// Define PATH_MAX if not defined
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "runepkg_config.h"
#include "runepkg_util.h"

// --- Global Path Variables Definitions ---
char *g_runepkg_base_dir = NULL;
char *g_control_dir = NULL;
char *g_runepkg_db_dir = NULL; // Database directory for persistent storage
char *g_install_dir_internal = NULL;
char *g_system_install_root = NULL;

// --- External Global Variables ---
extern bool g_verbose_mode; // Defined in main.c

// --- External Function Declarations ---
extern void runepkg_log_verbose(const char *format, ...);

// --- Internal Function Declarations ---
void runepkg_config_cleanup(void);

// --- Internal Configuration System Functions ---

// --- Helper function to find the correct configuration file path ---
char *runepkg_get_config_file_path() {
    char *config_file_path = NULL;

    // 1. Check for environment variable override
    char *env_config_path = getenv("RUNEPKG_CONFIG_PATH");
    if (env_config_path && upkg_util_file_exists(env_config_path)) {
        runepkg_log_verbose("Using configuration from RUNEPKG_CONFIG_PATH: %s\n", env_config_path);
        config_file_path = strdup(env_config_path);
        if (!config_file_path) {
            fprintf(stderr, "Error: Memory allocation failed for config path.\n");
            return NULL;
        }
        return config_file_path;
    }

    // 2. Check for system-wide configuration
    const char *system_config_path = "/etc/runepkg/runepkgconfig";
    if (upkg_util_file_exists(system_config_path)) {
        runepkg_log_verbose("Using system-wide configuration: %s\n", system_config_path);
        config_file_path = strdup(system_config_path);
        if (!config_file_path) {
            fprintf(stderr, "Error: Memory allocation failed for config path.\n");
            return NULL;
        }
        return config_file_path;
    }

    // 3. Check for user-specific configuration
    char *home_dir = getenv("HOME");
    if (home_dir) {
        char user_config_path[PATH_MAX];
        snprintf(user_config_path, sizeof(user_config_path), "%s/.runepkgconfig", home_dir);
        if (upkg_util_file_exists(user_config_path)) {
            runepkg_log_verbose("Using user-specific configuration: %s\n", user_config_path);
            config_file_path = strdup(user_config_path);
            if (!config_file_path) {
                fprintf(stderr, "Error: Memory allocation failed for config path.\n");
                return NULL;
            }
            return config_file_path;
        }
    }

    // If no configuration file was found
    fprintf(stderr, "Error: No configuration file found.\n");
    fprintf(stderr, "Looked for: 1. $RUNEPKG_CONFIG_PATH, 2. /etc/runepkg/runepkgconfig, 3. ~/.runepkgconfig\n");
    return NULL;
}

int runepkg_config_load() {
    char *config_file_path = runepkg_get_config_file_path();
    if (!config_file_path) {
        // Error message already printed by runepkg_get_config_file_path
        return -1;
    }

    // Free any existing global path variables to prevent leaks on re-entry (if applicable)
    runepkg_config_cleanup();

    // Retrieve the directory paths from the determined config file.
    runepkg_log_verbose("Loading configuration values from '%s'...\n", config_file_path);
    g_runepkg_base_dir = upkg_util_get_config_value(config_file_path, "runepkg_dir", '=');
    if (!g_runepkg_base_dir) {
        fprintf(stderr, "Error: Failed to read 'runepkg_dir' from config file. This is critical.\n");
        upkg_util_free_and_null(&config_file_path);
        runepkg_config_cleanup(); // Clean up anything partially allocated
        return -1;
    }

    g_control_dir = upkg_util_get_config_value(config_file_path, "control_dir", '=');
    if (!g_control_dir) {
        fprintf(stderr, "Error: Failed to read 'control_dir' from config file. This is critical.\n");
        upkg_util_free_and_null(&config_file_path);
        runepkg_config_cleanup();
        return -1;
    }

    g_runepkg_db_dir = upkg_util_get_config_value(config_file_path, "runepkg_db", '='); // New value
    if (!g_runepkg_db_dir) {
        fprintf(stderr, "Error: Failed to read 'runepkg_db' from config file. This is critical.\n");
        upkg_util_free_and_null(&config_file_path);
        runepkg_config_cleanup();
        return -1;
    }

    g_install_dir_internal = upkg_util_get_config_value(config_file_path, "install_dir", '=');
    if (!g_install_dir_internal) {
        fprintf(stderr, "Error: Failed to read 'install_dir' from config file. This is critical.\n");
        upkg_util_free_and_null(&config_file_path);
        runepkg_config_cleanup();
        return -1;
    }

    // Assign g_system_install_root from install_dir config value.
    g_system_install_root = strdup(g_install_dir_internal);
    if (!g_system_install_root) {
        fprintf(stderr, "Error: Failed to duplicate 'install_dir' for g_system_install_root.\n");
        upkg_util_free_and_null(&config_file_path);
        runepkg_config_cleanup();
        return -1;
    }
    
    upkg_util_free_and_null(&config_file_path); // Free the path string after use

    runepkg_log_verbose("Configuration loaded successfully:\n");
    runepkg_log_verbose("  runepkg_base_dir: %s\n", g_runepkg_base_dir);
    runepkg_log_verbose("  control_dir: %s\n", g_control_dir);
    runepkg_log_verbose("  runepkg_db_dir: %s\n", g_runepkg_db_dir); // New log message
    runepkg_log_verbose("  install_dir_internal (record keeping): %s\n", g_install_dir_internal);
    runepkg_log_verbose("  system_install_root (actual target): %s\n", g_system_install_root);

    return 0;
}

void runepkg_config_cleanup() {
    runepkg_log_verbose("Cleaning up global path variables...\n");
    upkg_util_free_and_null(&g_runepkg_base_dir);
    upkg_util_free_and_null(&g_control_dir);
    upkg_util_free_and_null(&g_runepkg_db_dir); // New cleanup call
    upkg_util_free_and_null(&g_install_dir_internal);
    upkg_util_free_and_null(&g_system_install_root);
}

void runepkg_init_paths() {
    // NEW LOGIC: Load paths from runepkgconfig
    runepkg_log_verbose("Initializing runepkg paths from config...\n");
    if (runepkg_config_load() != 0) {
        fprintf(stderr, "Error: Failed to load runepkg configuration. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    // Now, create the directories based on the loaded config paths
    // Check for NULL pointers before calling create_dir_recursive
    if (!g_runepkg_base_dir || !g_control_dir || !g_runepkg_db_dir || !g_install_dir_internal) {
        fprintf(stderr, "Error: One or more critical path variables are NULL after config load. Cannot create directories. Exiting.\n");
        runepkg_config_cleanup(); // Clean up anything that might have been allocated
        exit(EXIT_FAILURE);
    }

    runepkg_log_verbose("Creating necessary runepkg directories...\n");
    if (upkg_util_create_dir_recursive(g_control_dir, 0755) != 0 ||
        upkg_util_create_dir_recursive(g_runepkg_db_dir, 0755) != 0 || // New directory creation
        upkg_util_create_dir_recursive(g_install_dir_internal, 0755) != 0) {
        fprintf(stderr, "Error: Failed to create necessary runepkg directories based on config. Exiting.\n");
        runepkg_config_cleanup();
        exit(EXIT_FAILURE);
    }

    runepkg_log_verbose("runepkg directories initialized from config:\n");
    runepkg_log_verbose("  Base: %s\n", g_runepkg_base_dir);
    runepkg_log_verbose("  Control: %s\n", g_control_dir);
    runepkg_log_verbose("  Database: %s\n", g_runepkg_db_dir); // New log message
    runepkg_log_verbose("  Internal Install Records: %s\n", g_install_dir_internal);
    runepkg_log_verbose("  System Root (actual install target): %s\n", g_system_install_root);
}
