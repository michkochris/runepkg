#/******************************************************************************
 * Filename:    runepkg_config.c
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Configuration parsing and path setup helpers for runepkg
 * LICENSE:     GPL v3
 * THIS IS FREE SOFTWARE; YOU CAN REDISTRIBUTE IT AND/OR MODIFY IT UNDER
 * THE TERMS OF THE GNU GENERAL PUBLIC LICENSE AS PUBLISHED BY THE FREE
 * SOFTWARE FOUNDATION; EITHER VERSION 3 OF THE LICENSE, OR (AT YOUR OPTION)
 * ANY LATER VERSION.
 * THIS PROGRAM IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. SEE THE
 * GNU GENERAL PUBLIC LICENSE FOR MORE DETAILS.
 ******************************************************************************/

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
#include <libgen.h>

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
char *g_pkglist_txt_path = NULL;
char *g_pkglist_bin_path = NULL;

// --- External Global Variables ---
extern bool g_verbose_mode; // Defined in main.c

// --- External Function Declarations ---
/* `runepkg_log_verbose` is available as a macro mapping to `runepkg_util_log_verbose` */

// --- Internal Function Declarations ---
void runepkg_config_cleanup(void);

// --- Internal Configuration System Functions ---

// --- Helper function to find the correct configuration file path ---
char *runepkg_get_config_file_path() {
    char *config_file_path = NULL;

    /* 1. Check for environment variable override */
    char *env_config_path = getenv("RUNEPKG_CONFIG_PATH");
    if (env_config_path && runepkg_util_file_exists(env_config_path)) {
        config_file_path = strdup(env_config_path);
        if (!config_file_path) {
            fprintf(stderr, "Error: Memory allocation failed for config path.\n");
            return NULL;
        }
        return config_file_path;
    }

    /* Simplified behavior: Always prefer a configured system-wide file
     * when present. Per-user configuration is intentionally
     * deprecated and not consulted anymore. This avoids per-user/system
     * mixing and keeps behavior consistent for both root and non-root users.
     */
    const char *system_config_path = "/etc/runepkg/runepkgconfig";

    if (runepkg_util_file_exists(system_config_path)) {
        config_file_path = strdup(system_config_path);
        if (!config_file_path) {
            fprintf(stderr, "Error: Memory allocation failed for config path.\n");
            return NULL;
        }
        return config_file_path;
    }

    /* No config file found; caller will fall back to built-in defaults. */
    return NULL;
}

int runepkg_config_load() {
    char *config_file_path = runepkg_get_config_file_path();
    if (!config_file_path) {
        // Use defaults
        char *home = getenv("HOME");
        if (!home) {
            fprintf(stderr, "Error: HOME environment variable not set. Cannot load default configuration.\n");
            return -1;
        }
        runepkg_log_verbose("No configuration file found. Using default paths.\n");
        g_runepkg_base_dir = runepkg_util_concat_path(home, "runepkg_dir");
        if (!g_runepkg_base_dir) {
            fprintf(stderr, "Error: Failed to create default runepkg_dir path.\n");
            return -1;
        }
        g_control_dir = runepkg_util_concat_path(home, "runepkg_dir/control_dir");
        if (!g_control_dir) {
            fprintf(stderr, "Error: Failed to create default control_dir path.\n");
            runepkg_config_cleanup();
            return -1;
        }
        g_runepkg_db_dir = runepkg_util_concat_path(home, "runepkg_dir/runepkg_db");
        if (!g_runepkg_db_dir) {
            fprintf(stderr, "Error: Failed to create default runepkg_db path.\n");
            runepkg_config_cleanup();
            return -1;
        }
        g_install_dir_internal = runepkg_util_concat_path(home, "runepkg_dir/install_dir");
        if (!g_install_dir_internal) {
            fprintf(stderr, "Error: Failed to create default install_dir path.\n");
            runepkg_config_cleanup();
            return -1;
        }
        g_system_install_root = strdup(g_install_dir_internal);
        if (!g_system_install_root) {
            fprintf(stderr, "Error: Failed to duplicate install_dir for g_system_install_root.\n");
            runepkg_config_cleanup();
            return -1;
        }
        // Set pkglist paths to runepkg_dir
        g_pkglist_txt_path = runepkg_util_concat_path(g_runepkg_base_dir, "runepkg_autocomplete.txt");
        if (!g_pkglist_txt_path) {
            fprintf(stderr, "Error: Failed to create runepkg_autocomplete.txt path.\n");
            runepkg_config_cleanup();
            return -1;
        }
        g_pkglist_bin_path = runepkg_util_concat_path(g_runepkg_base_dir, "runepkg_autocomplete.bin");
        if (!g_pkglist_bin_path) {
            fprintf(stderr, "Error: Failed to create pkglist.bin path.\n");
            runepkg_config_cleanup();
            return -1;
        }
        /* Directories will be created by runepkg_init_paths() later.
         * Avoid creating them here to prevent duplicate verbose/debug logs. */
    } else {
        // Free any existing global path variables to prevent leaks on re-entry (if applicable)
        runepkg_config_cleanup();

        // Retrieve the directory paths from the determined config file.
        runepkg_log_verbose("Loading configuration values from '%s'...\n", config_file_path);
        g_runepkg_base_dir = runepkg_util_get_config_value(config_file_path, "runepkg_dir", '=');
        if (!g_runepkg_base_dir) {
            fprintf(stderr, "Error: Failed to read 'runepkg_dir' from config file. This is critical.\n");
            runepkg_util_free_and_null(&config_file_path);
            runepkg_config_cleanup(); // Clean up anything partially allocated
            return -1;
        }

        g_control_dir = runepkg_util_get_config_value(config_file_path, "control_dir", '=');
        if (!g_control_dir) {
            fprintf(stderr, "Error: Failed to read 'control_dir' from config file. This is critical.\n");
            runepkg_util_free_and_null(&config_file_path);
            runepkg_config_cleanup();
            return -1;
        }

        g_runepkg_db_dir = runepkg_util_get_config_value(config_file_path, "runepkg_db", '='); // New value
        if (!g_runepkg_db_dir) {
            fprintf(stderr, "Error: Failed to read 'runepkg_db' from config file. This is critical.\n");
            runepkg_util_free_and_null(&config_file_path);
            runepkg_config_cleanup();
            return -1;
        }

        g_install_dir_internal = runepkg_util_get_config_value(config_file_path, "install_dir", '=');
        if (!g_install_dir_internal) {
            fprintf(stderr, "Error: Failed to read 'install_dir' from config file. This is critical.\n");
            runepkg_util_free_and_null(&config_file_path);
            runepkg_config_cleanup();
            return -1;
        }

        // Assign g_system_install_root from install_dir config value.
        g_system_install_root = strdup(g_install_dir_internal);
        if (!g_system_install_root) {
            fprintf(stderr, "Error: Failed to duplicate 'install_dir' for g_system_install_root.\n");
            runepkg_util_free_and_null(&config_file_path);
            runepkg_config_cleanup();
            return -1;
        }

        // Set pkglist paths based on runepkg_dir
        g_pkglist_txt_path = runepkg_util_concat_path(g_runepkg_base_dir, "runepkg_autocomplete.txt");
        if (!g_pkglist_txt_path) {
            fprintf(stderr, "Error: Failed to create runepkg_autocomplete.txt path.\n");
            runepkg_util_free_and_null(&config_file_path);
            runepkg_config_cleanup();
            return -1;
        }
        g_pkglist_bin_path = runepkg_util_concat_path(g_runepkg_base_dir, "runepkg_autocomplete.bin");
        if (!g_pkglist_bin_path) {
            fprintf(stderr, "Error: Failed to create runepkg_autocomplete.bin path.\n");
            runepkg_util_free_and_null(&config_file_path);
            runepkg_config_cleanup();
            return -1;
        }

        // Now, create the directories based on the loaded config paths
        // Check for NULL pointers before calling create_dir_recursive
        if (!g_runepkg_base_dir || !g_control_dir || !g_runepkg_db_dir || !g_install_dir_internal) {
            fprintf(stderr, "Error: One or more critical path variables are NULL after config load. Cannot create directories. Exiting.\n");
            runepkg_util_free_and_null(&config_file_path);
            runepkg_config_cleanup();
            return -1;
        }

        /* Directories will be created by runepkg_init_paths() later.
         * Avoid creating them here to prevent duplicate verbose/debug logs. */
        /* keep config_file_path until after verbose summary so we can report source */
    }

    /* Concise summary for verbose mode: one-line summary instead of
     * multiple repeated lines. If detailed inspection is needed, -v
     * still enables internal verbose logs elsewhere. */
    if (g_verbose_mode) {
        if (config_file_path) {
            runepkg_log_verbose("Configuration loaded from %s; base=%s, control=%s, db=%s, install=%s\n",
                               config_file_path,
                               g_runepkg_base_dir ? g_runepkg_base_dir : "(null)",
                               g_control_dir ? g_control_dir : "(null)",
                               g_runepkg_db_dir ? g_runepkg_db_dir : "(null)",
                               g_install_dir_internal ? g_install_dir_internal : "(null)");
        } else {
            runepkg_log_verbose("Configuration loaded using defaults; base=%s, control=%s, db=%s, install=%s\n",
                               g_runepkg_base_dir ? g_runepkg_base_dir : "(null)",
                               g_control_dir ? g_control_dir : "(null)",
                               g_runepkg_db_dir ? g_runepkg_db_dir : "(null)",
                               g_install_dir_internal ? g_install_dir_internal : "(null)");
        }
        runepkg_log_verbose("Autocomplete files: txt=%s bin=%s\n",
                           g_pkglist_txt_path ? g_pkglist_txt_path : "(null)",
                           g_pkglist_bin_path ? g_pkglist_bin_path : "(null)");
    }

    /* free config file path now that we've reported it */
    runepkg_util_free_and_null(&config_file_path);

    return 0;
}

void runepkg_config_cleanup() {
    runepkg_util_free_and_null(&g_runepkg_base_dir);
    runepkg_util_free_and_null(&g_control_dir);
    runepkg_util_free_and_null(&g_runepkg_db_dir); // New cleanup call
    runepkg_util_free_and_null(&g_install_dir_internal);
    runepkg_util_free_and_null(&g_system_install_root);
    runepkg_util_free_and_null(&g_pkglist_txt_path);
    runepkg_util_free_and_null(&g_pkglist_bin_path);
}

void runepkg_init_paths() {
    // NEW LOGIC: Load paths from runepkgconfig
    /* Initialization summary (concise) */
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

    /* Create configured runepkg directories once and report concise results.
     * For each configured directory, print a single debug line if it was created. */
    bool created_base = false, created_control = false, created_db = false, created_install = false;

    if (!runepkg_util_file_exists(g_runepkg_base_dir)) {
        if (runepkg_util_create_dir_recursive(g_runepkg_base_dir, 0755) == 0) created_base = true;
        else { fprintf(stderr, "Error: Failed to create base directory %s\n", g_runepkg_base_dir); runepkg_config_cleanup(); exit(EXIT_FAILURE); }
    }

    if (!runepkg_util_file_exists(g_control_dir)) {
        if (runepkg_util_create_dir_recursive(g_control_dir, 0755) == 0) created_control = true;
        else { fprintf(stderr, "Error: Failed to create control directory %s\n", g_control_dir); runepkg_config_cleanup(); exit(EXIT_FAILURE); }
    }

    if (!runepkg_util_file_exists(g_runepkg_db_dir)) {
        if (runepkg_util_create_dir_recursive(g_runepkg_db_dir, 0755) == 0) created_db = true;
        else { fprintf(stderr, "Error: Failed to create db directory %s\n", g_runepkg_db_dir); runepkg_config_cleanup(); exit(EXIT_FAILURE); }
    }

    if (!runepkg_util_file_exists(g_install_dir_internal)) {
        if (runepkg_util_create_dir_recursive(g_install_dir_internal, 0755) == 0) created_install = true;
        else { fprintf(stderr, "Error: Failed to create install directory %s\n", g_install_dir_internal); runepkg_config_cleanup(); exit(EXIT_FAILURE); }
    }

    if (g_verbose_mode) {
        if (created_base) runepkg_util_log_debug("Created runepkg_dir: %s\n", g_runepkg_base_dir);
        if (created_control) runepkg_util_log_debug("Created control_dir: %s\n", g_control_dir);
        if (created_db) runepkg_util_log_debug("Created runepkg_db: %s\n", g_runepkg_db_dir);
        if (created_install) runepkg_util_log_debug("Created install_dir: %s\n", g_install_dir_internal);

        runepkg_log_verbose("runepkg directories initialized: base=%s control=%s db=%s install=%s root=%s\n",
                           g_runepkg_base_dir ? g_runepkg_base_dir : "(null)",
                           g_control_dir ? g_control_dir : "(null)",
                           g_runepkg_db_dir ? g_runepkg_db_dir : "(null)",
                           g_install_dir_internal ? g_install_dir_internal : "(null)",
                           g_system_install_root ? g_system_install_root : "(null)");
    }
}
