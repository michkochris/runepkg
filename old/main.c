#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#include "upkg_config.h"

// Global variables
bool g_verbose_mode = false;

// --- Simple Logging Functions ---

/**
 * @brief Logging function for verbose output
 */
void upkg_log_verbose(const char *format, ...) {
    if (!g_verbose_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("[VERBOSE] ");
    vprintf(format, args);
    va_end(args);
}

// --- Placeholder Function Implementations ---

/**
 * @brief Prints the program's usage information.
 */
void usage(void) {
    printf("upkg - The ulinux package manager.\n\n");
    printf("Usage:\n");
    printf("  upkg <COMMAND> [OPTIONS] [ARGUMENTS]\n\n");
    printf("Commands and Options:\n");
    printf("  -i, --install <path-to-package.deb>...  Install one or more .deb files.\n");
    printf("  -r, --remove <package-name>             Remove a package.\n");
    printf("  -l, --list                              List all installed packages.\n");
    printf("  -s, --status <package-name>             Show detailed information about a package.\n");
    printf("  -S, --search <query>                    Search for a package by name.\n");
    printf("  -v, --verbose                           Enable verbose output.\n");
    printf("  --version                               Print version information.\n");
    printf("  -h, --help                              Display this help message.\n\n");
    printf("Note: Commands can be interleaved, e.g., 'upkg -v -i pkg1.deb -s pkg2 -i pkg3.deb'\n");
}

/**
 * @brief Prints version information.
 */
void handle_version(void) {
    printf("upkg v0.1.0 - The ulinux package manager\n");
    printf("Copyright (c) 2025 upkg (ulinux) All rights reserved.\n");
    printf("Licensed under GPL v3\n");
}

/**
 * @brief Initializes the upkg environment with configuration loading.
 * @return 0 on success, -1 on failure.
 */
int upkg_init(void) {
    upkg_log_verbose("Initializing upkg environment...\n");
    
    // Initialize paths from configuration file
    upkg_init_paths();
    
    upkg_log_verbose("upkg environment initialized successfully.\n");
    return 0; // Success
}

/**
 * @brief Cleans up upkg environment and frees allocated memory.
 */
void upkg_cleanup(void) {
    upkg_log_verbose("Cleaning up upkg environment...\n");
    
    // Clean up configuration paths
    upkg_cleanup_paths();
    
    upkg_log_verbose("upkg cleanup completed.\n");
}

/**
 * @brief Prints error messages with formatting support.
 */
void errormsg(const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    fprintf(stderr, "ERROR: ");
    vfprintf(stderr, format, args);
    
    va_end(args);
}

/**
 * @brief Handles package installation (placeholder).
 */
void handle_install(const char *deb_file_path) {
    upkg_log_verbose("Installing package from: %s\n", deb_file_path);
    printf("Installing package from: %s (placeholder)\n", deb_file_path);
    if (g_control_dir && g_unpack_dir && g_system_install_root) {
        upkg_log_verbose("  Control dir: %s\n", g_control_dir);
        upkg_log_verbose("  Unpack dir: %s\n", g_unpack_dir);
        upkg_log_verbose("  Install root: %s\n", g_system_install_root);
    }
}

/**
 * @brief Handles package removal (placeholder).
 */
void handle_remove(const char *package_name) {
    printf("Removing package: %s (placeholder)\n", package_name);
}

/**
 * @brief Lists installed packages (placeholder).
 */
void handle_list(void) {
    upkg_log_verbose("Listing installed packages...\n");
    printf("Listing installed packages... (placeholder)\n");
    if (g_db_dir) {
        upkg_log_verbose("  Database dir: %s\n", g_db_dir);
    }
}

/**
 * @brief Shows package status (placeholder).
 */
void handle_status(const char *package_name) {
    printf("Showing status for package: %s (placeholder)\n", package_name);
}

/**
 * @brief Searches for packages (placeholder).
 */
void handle_search(const char *query) {
    printf("Searching for packages with query: %s (placeholder)\n", query);
}

// --- Main Function ---
int main(int argc, char *argv[]) {
    // Check for verbose mode first, as it affects all subsequent output.
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_verbose_mode = true;
            break;
        }
    }

    // Handle help or version before initialization
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--version") == 0) {
            handle_version();
            return EXIT_SUCCESS;
        }
    }

    if (argc < 2) {
        usage();
        return EXIT_FAILURE;
    }
    
    // --- Core Program Flow ---
    // Step 1: Initialize the environment and load the database
    upkg_log_verbose("Starting upkg with %d arguments\n", argc);
    if (upkg_init() != 0) {
        errormsg("Critical error during program initialization. Exiting.\n");
        upkg_cleanup();
        return EXIT_FAILURE;
    }
    
    // Register the cleanup function to be called on exit.
    atexit(upkg_cleanup);

    // Step 2: Execute commands based on the interleaved arguments.
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--install") == 0) {
            if (i + 1 < argc) {
                // Loop to handle multiple .deb files
                while (i + 1 < argc) {
                    char *next_arg = argv[i+1];
                    if (next_arg[0] == '-' || strstr(next_arg, ".deb") == NULL) {
                        break; // Stop if it's a new command switch or not a .deb file
                    }
                    handle_install(next_arg);
                    i++;
                }
            } else {
                errormsg("Error: -i/--install requires at least one .deb file argument.");
            }
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--remove") == 0) {
            if (i + 1 < argc) {
                handle_remove(argv[i+1]);
                i++;
            } else {
                errormsg("Error: -r/--remove requires a package name.");
            }
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            handle_list();
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--status") == 0) {
            if (i + 1 < argc) {
                handle_status(argv[i+1]);
                i++;
            } else {
                errormsg("Error: -s/--status requires a package name.");
            }
        } else if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--search") == 0) {
            if (i + 1 < argc) {
                handle_search(argv[i+1]);
                i++;
            } else {
                errormsg("Error: -S/--search requires a query.");
            }
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            // Already handled at the start of main
        } else {
            errormsg("Error: Unknown argument or command: %s", argv[i]);
            // For interleaved commands, we should continue processing if possible
            // For now, let's just break on an unknown command
            break;
        }
    }

    return EXIT_SUCCESS;
    // Note: The atexit handler will now call upkg_cleanup()
}
