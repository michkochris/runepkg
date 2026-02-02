/******************************************************************************
 * Filename:    runepkg_cli.c
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Command-line interface for runepkg package manager
 * LICENSE:     GPL v3
 * THIS IS FREE SOFTWARE; YOU CAN REDISTRIBUTE IT AND/OR MODIFY IT UNDER
 * THE TERMS OF THE GNU GENERAL PUBLIC LICENSE AS PUBLISHED BY THE FREE
 * SOFTWARE FOUNDATION; EITHER VERSION 3 OF THE LICENSE, OR (AT YOUR OPTION)
 * ANY LATER VERSION.
 * THIS PROGRAM IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. SEE THE
 * GNU GENERAL PUBLIC LICENSE FOR MORE DETAILS.
 ******************************************************************************/
/* keep this file minimal: only usage() and main() */

#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <limits.h>

#include "runepkg_config.h"
#include "runepkg_pack.h"
#include "runepkg_hash.h"
#include "runepkg_storage.h"
#include "runepkg_util.h"
#include "runepkg_handle.h"

// Global variables
bool g_verbose_mode = false;

// * @brief Prints the program's usage information.
void usage(void) {
    printf("runepkg - The Runar Linux package manager.\n\n");
    printf("Usage:\n");
    printf("  runepkg <COMMAND> [OPTIONS] [ARGUMENTS]\n\n");
    printf("Commands and Options:\n");
    printf("  -i, --install <path-to-package.deb>...  Install one or more .deb files.\n");
    printf("      --install -                         Read .deb paths from stdin.\n");
    printf("      --install @file                     Read .deb paths from a list file.\n");
    printf("  -r, --remove <package-name>             Remove a package.\n");
    printf("      --remove -                          Read package names from stdin.\n");
    printf("  -l, --list                              List all installed packages.\n");
    printf("  -s, --status <package-name>             Show detailed information about a package.\n");
    printf("  -S, --search <query>                    Search for a package by name.\n");
    printf("  -v, --verbose                           Enable verbose output.\n");
    printf("      --version                           Print version information.\n");
    printf("  -h, --help                              Display this help message.\n\n");
    printf("      --print-config                      Print current configuration settings.\n");
    printf("      --print-config-file                 Print path to configuration file in use.\n");
    printf("Note: Commands can be interleaved, e.g., 'runepkg -v -i pkg1.deb -s pkg2 -i pkg3.deb'\n");
}

// --- Main Function ---
int main(int argc, char *argv[]) {
    // Check for verbose and JSON modes first, as they affect all subsequent output.
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_verbose_mode = true;
            break;
        }
    }
    
    runepkg_log_verbose("=== RUNEPKG STARTUP ANALYSIS ===\n");
    runepkg_log_verbose("Command line arguments: %d\n", argc);
    if (g_verbose_mode) {
        for (int i = 0; i < argc; i++) {
            printf("[DEBUG-VV] argv[%d] = '%s'\n", i, argv[i]);
        }
    }
    runepkg_log_verbose("Verbose mode: %s\n", g_verbose_mode ? "ENABLED" : "disabled");

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
        if (strcmp(argv[i], "--print-config") == 0) {
            // Initialize first to load config, then print
            if (runepkg_init() != 0) {
                fprintf(stderr, "ERROR: Failed to initialize runepkg configuration\n");
                return EXIT_FAILURE;
            }
            handle_print_config();
            runepkg_cleanup();
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--print-config-file") == 0) {
            // No need to initialize for this - just find the config file
            handle_print_config_file();
            return EXIT_SUCCESS;
        }
    }

    if (argc < 2) {
        usage();
        return EXIT_FAILURE;
    }
    
    // --- Core Program Flow ---
    // Step 1: Initialize the environment and load the database
    runepkg_log_verbose("Starting runepkg with %d arguments\n", argc);
    if (runepkg_init() != 0) {
        runepkg_log_verbose("Critical error during program initialization. Exiting.\n");
        runepkg_cleanup();
        return EXIT_FAILURE;
    }
    
    // Register the cleanup function to be called on exit.
    atexit(runepkg_cleanup);

    // Step 2: Execute commands based on the interleaved arguments.
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--install") == 0) {
            if (i + 1 < argc) {
                // Loop to handle multiple .deb files
                while (i + 1 < argc) {
                    char *next_arg = argv[i+1];
                    if (strcmp(next_arg, "-") == 0) {
                        i++;
                        handle_install_stdin();
                        break;
                    }
                    if (next_arg[0] == '@') {
                        i++;
                        handle_install_listfile(next_arg + 1);
                        continue;
                    }
                    if (next_arg[0] == '-' || strstr(next_arg, ".deb") == NULL) {
                        break; // Stop if it's a new command switch or not a .deb file
                    }
                    handle_install(next_arg);
                    i++;
                }
            } else {
                handle_install_stdin();
            }
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--remove") == 0) {
            if (i + 1 < argc) {
                while (i + 1 < argc) {
                    char *next_arg = argv[i+1];
                    if (strcmp(next_arg, "-") == 0) {
                        i++;
                        handle_remove_stdin();
                        break;
                    }
                    if (next_arg[0] == '-') {
                        break;
                    }
                    handle_remove(next_arg);
                    i++;
                }
            } else {
                handle_remove_stdin();
            }
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            handle_list();
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--status") == 0) {
            if (i + 1 < argc) {
                handle_status(argv[i+1]);
                i++;
            } else {
                runepkg_log_verbose("Error: -s/--status requires a package name.");
            }
        } else if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--search") == 0) {
            if (i + 1 < argc) {
                handle_search(argv[i+1]);
                i++;
            } else {
                runepkg_log_verbose("Error: -S/--search requires a query.");
            }
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            // Already handled at the start of main
        } else {
            runepkg_log_verbose("Error: Unknown argument or command: %s", argv[i]);
            // For interleaved commands, we should continue processing if possible
            // For now, let's just break on an unknown command
            break;
        }
    }

    return EXIT_SUCCESS;
    // Note: The atexit handler will now call runepkg_cleanup()
}
