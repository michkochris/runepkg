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
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <limits.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <sys/syscall.h>

#include "runepkg_config.h"
#include "runepkg_pack.h"
#include "runepkg_hash.h"
#include "runepkg_storage.h"
#include "runepkg_util.h"
#include "runepkg_handle.h"

#ifdef ENABLE_CPP_FFI
#include "runepkg_cpp_ffi.h"
#endif

// Global variables
bool g_verbose_mode = false;
bool g_force_mode = false;
bool g_completion_mode = false;
bool g_did_install = false;
bool g_debug_mode = false;
bool g_auto_confirm_deps = false;
bool g_auto_confirm_siblings = false;
bool g_asked_siblings = false;

/* Completion and autocomplete implementations moved to runepkg_handle.c */

// * @brief Prints the program's usage information.
void usage(void) {
    printf("runepkg (fast efficient old-school .deb package manager)\n\n");
    printf("Usage:\n");
    printf("  runepkg <COMMAND> [OPTIONS] [ARGUMENTS]\n\n");

    printf("Core Package Management (Local/Low-Level):\n");
    printf("  -i, --install <deb|pkg>...              Install .deb files or repository packages.\n");
    printf("      --install -                         Read .deb paths from stdin.\n");
    printf("      --install @file                     Read .deb paths from a list file.\n");
    printf("  -r, --remove <package-name>             Remove an installed package.\n");
    printf("      --remove -                          Read package names from stdin.\n");
    printf("  -l, --list [pattern]                    List installed packages (optionally matching pattern).\n");
    printf("  -s, --status <package-name>             Show detailed info about an installed package.\n");
    printf("  -L, --list-files <package-name>         List all files owned by an installed package.\n");
    printf("  -S, --search <file-path>                Search installed packages for a specific file.\n");
    printf("  -u, --unpack <path-to-package.deb>      Unpack a .deb into build_dir.\n");
    printf("  -m, --md5check <package-name>           Verify MD5 checksums of an installed package.\n");
    printf("  -b, --build [dir] [output.deb]          Build a .deb from a directory structure.\n");
    printf("  -v, --verbose                           Enable verbose output (detailed logging).\n");
    printf("  -d, --debug                             Enable debug output (developer traces).\n");
    printf("  -f, --force                             Force install/upgrade despite missing dependencies.\n");
    printf("      --version                           Print version and license information.\n");
    printf("  -h, --help                              Display this help message.\n\n");

    printf("Advanced Repository Management (Network/FFI):\n");
    printf("  update                                  Sync metadata and check for upgradable packages.\n");
    printf("  upgrade                                 Download and install all available upgrades.\n");
    printf("  search <pkg|pattern>                    Search repositories for packages or patterns.\n");
    printf("                                          (Use \"quotes\" to search for multiple words).\n");
    printf("  source <pkg>                            Download source package files into build_dir.\n");
    printf("  source-depends <pkg>                    Download source package and its runtime-dependencies.\n");
    printf("  source-build-depends <pkg>              Download source package and its build-dependencies.\n");
    printf("  source-build <package.dsc>              Build a Debian source package into runepkg_debs.\n");
    printf("  download-only <pkg>                     Download a .deb to download_dir without dependencies.\n");
    printf("  download-depends <pkg>                  Download a .deb and its binary dependencies.\n");
    printf("  download-build-depends <pkg>            Download binary .debs required to build a source package.\n\n");

    printf("Maintenance & Diagnostics:\n");
    printf("      --print-config                      Print all active path and repository settings.\n");
    printf("      --print-config-file                 Show the path to the runepkgconfig file in use.\n");
    printf("      --print-pkglist-file                Show paths to the autocomplete index files.\n");
    printf("      --print-autopool                    Print the contents of the consolidated autocomplete pool.\n");
    printf("      --rebuild-autocomplete              Rebuild the local package name index.\n\n");

    printf("Experimental/Future:\n");
    printf("  depends <pkg>                           Placeholder: Graphical dependency visualizer.\n");
    printf("  verify <pkg>                            Placeholder: Cryptographic package verification.\n\n");

    printf("Note: Commands can be interleaved, e.g., 'runepkg -v -i pkg1.deb -s pkg2 -i pkg3.deb'\n");
    printf("Note: FFI features (C++) are enabled based on your build target (`make all`).\n\n");
    handle_version();
}

// --- Main Function ---
int main(int argc, char *argv[]) {
    // Completion mode check - only enter when Bash's completion environment
    // variables are present to avoid confusing real invocations that happen
    // to have three user arguments (argc == 4).
    if (argc == 4 && getenv("COMP_LINE") != NULL && is_completion_trigger(argv)) {
        g_completion_mode = true;
        if (runepkg_init() != 0) return 0;
        handle_binary_completion(argv[2], argv[3]);
        return 0;
    }

    // Check for verbose and force modes first, as they affect all subsequent output.
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_verbose_mode = true;
        }
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            g_force_mode = true;
        }
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            g_debug_mode = true;
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

    int cli_failed = 0;

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
                        g_did_install = true;
                        break;
                    }
                    if (next_arg[0] == '@') {
                        i++;
                        handle_install_listfile(next_arg + 1);
                        g_did_install = true;
                        continue;
                    }
                    if (strcmp(next_arg, "-f") == 0 || strcmp(next_arg, "--force") == 0) {
                        g_force_mode = true;
                        i++;
                        continue;
                    }
                    if (strcmp(next_arg, "-v") == 0 || strcmp(next_arg, "--verbose") == 0) {
                        g_verbose_mode = true;
                        i++;
                        continue;
                    }
                    if (next_arg[0] == '-') {
                        break; // Stop if it's a new command switch
                    }

                    int ret;
                    if (runepkg_util_file_exists(next_arg)) {
                        ret = handle_install(next_arg);
                    } else {
                        // Check if it's an already installed package name
                        if (runepkg_main_hash_table && runepkg_hash_search(runepkg_main_hash_table, next_arg)) {
                            if (!g_force_mode) {
                                PkgInfo *info = runepkg_hash_search(runepkg_main_hash_table, next_arg);
                                printf("Package %s is already installed (%s). Use -f/--force to reinstall.\n", next_arg, info->version ? info->version : "unknown");
                                ret = 0;
                            } else {
                                // Force mode: proceed to repo download/reinstall
                                goto try_repo;
                            }
                        } else {
                        try_repo:
#ifdef ENABLE_CPP_FFI
                            char *downloaded_path = runepkg_repo_download(next_arg, true);
                            if (downloaded_path) {
                                g_auto_confirm_siblings = true;
                                ret = handle_install(downloaded_path);
                                free(downloaded_path);
                            } else {
                                fprintf(stderr, "Error: Cannot find local file '%s' and failed to download it from repositories.\n", next_arg);
                                ret = -1;
                            }
#else
                            fprintf(stderr, "Error: File '%s' not found and repository downloads are disabled.\n", next_arg);
                            ret = -1;
#endif
                        }
                    }

                    if (ret == 0) {
                        g_did_install = true;
                    } else {
                        cli_failed = 1;
                    }
                    i++;
                }
            } else {
                handle_install_stdin();
            }
        } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--unpack") == 0) {
            if (i + 1 < argc) {
                handle_unpack(argv[i+1]);
                i++;
            } else {
                printf("Error: --unpack requires a .deb file path.\n");
            }
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--md5check") == 0) {
            if (i + 1 < argc) {
                handle_md5_check(argv[i+1]);
                i++;
            } else {
                printf("Error: --md5check requires a package name.\n");
            }
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--remove") == 0) {
            char *removed_packages[100];
            int removed_count = 0;
            char *failed_packages[100];
            int failed_count = 0;
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
                    int ret = handle_remove(next_arg);
                    if (ret == 0) {
                        // Successful removal
                        if (removed_count < 100) {
                            removed_packages[removed_count++] = strdup(next_arg);
                        }
                    } else if (ret == -2) {
                        // Suggestions shown, no removal attempted - do nothing
                        // The suggestions were already displayed by handle_remove
                    } else if (ret != 0) {
                        cli_failed = 1;
                        if (failed_count < 100) {
                            failed_packages[failed_count++] = strdup(next_arg);
                        }
                    }
                    i++;
                }
            } else {
                handle_remove_stdin();
            }
            if (removed_count > 0) {
                printf("Successfully removed packages:\n");
                for(int j = 0; j < removed_count; j++) {
                    if(j > 0) printf(" ");
                    printf("%s", removed_packages[j]);
                    free(removed_packages[j]);
                }
                printf("\n");
            }
            if (failed_count > 0) {
                printf("Failed to find packages:\n");
                for(int j = 0; j < failed_count; j++) {
                    printf("  %s", failed_packages[j]);
                    
                    // Show suggestions for this failed package
                    char suggestions[10][PATH_MAX];
                    int suggestion_count = runepkg_util_get_package_suggestions(failed_packages[j], g_runepkg_db_dir, suggestions, 10);
                    if (suggestion_count > 0) {
                        printf(" - did you mean:\n");
                        const char *items[10];
                        for (int k = 0; k < suggestion_count; k++) {
                            items[k] = suggestions[k];
                        }
                        printf("    ");
                        runepkg_util_print_columns(items, suggestion_count);
                    } else {
                        printf(" - not found\n");
                    }
                    
                    free(failed_packages[j]);
                }
            }
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            const char *pattern = NULL;
            if (i + 1 < argc && argv[i+1][0] != '-') {
                pattern = argv[i+1];
                i++;
            }
            handle_list(pattern);
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--status") == 0) {
            if (i + 1 < argc) {
                char *next_arg = argv[i+1];
                /* Support interleaved forms:
                 *  -s -L <pkg>  -> show status then list-files
                 *  -s -r <pkg>  -> show status then remove
                 */
                if (next_arg[0] == '-' && (strcmp(next_arg, "-L") == 0 || strcmp(next_arg, "--list-files") == 0)) {
                    if (i + 2 < argc && argv[i+2][0] != '-') {
                        int ret = handle_status(argv[i+2]);
                        if (ret == -2) {
                            /* suggestions shown */
                        } else if (ret != 0) {
                            cli_failed = 1;
                            runepkg_log_verbose("Error: Failed to get status for package '%s'.", argv[i+2]);
                        }
                        handle_list_files(argv[i+2]);
                        i += 2;
                    } else {
                        cli_failed = 1;
                        runepkg_log_verbose("Error: -s/--status with -L requires a package name.");
                    }
                } else if (next_arg[0] == '-' && (strcmp(next_arg, "-r") == 0 || strcmp(next_arg, "--remove") == 0)) {
                    if (i + 2 < argc && argv[i+2][0] != '-') {
                        int ret = handle_status(argv[i+2]);
                        if (ret == -2) {
                            /* suggestions shown */
                        } else if (ret != 0) {
                            cli_failed = 1;
                            runepkg_log_verbose("Error: Failed to get status for package '%s'.", argv[i+2]);
                        }
                        /* Call remove for the same package name and report summary
                         * like the non-interleaved path so the user sees immediate
                         * confirmation when commands are interleaved.
                         */
                        int rem_ret = handle_remove(argv[i+2]);
                        if (rem_ret == 0) {
                            printf("Successfully removed packages:\n%s\n", argv[i+2]);
                        } else if (rem_ret == -2) {
                            /* suggestions shown by handle_remove */
                        } else {
                            cli_failed = 1;
                            printf("Failed to remove package: %s\n", argv[i+2]);
                        }
                        i += 2;
                    } else {
                        cli_failed = 1;
                        runepkg_log_verbose("Error: -s/--status with -r requires a package name.");
                    }
                } else {
                    int ret = handle_status(argv[i+1]);
                    if (ret == -2) {
                        // Suggestions shown, no status displayed - do nothing
                    } else if (ret != 0) {
                        cli_failed = 1;
                        runepkg_log_verbose("Error: Failed to get status for package '%s'.", argv[i+1]);
                    }
                    i++;
                }
            } else {
                cli_failed = 1;
                runepkg_log_verbose("Error: -s/--status requires a package name.");
            }
        } else if (strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "--list-files") == 0) {
            if (i + 1 < argc) {
                handle_list_files(argv[i+1]);
                i++;
            } else {
                cli_failed = 1;
                runepkg_log_verbose("Error: -L/--list-files requires a package name.");
            }
        } else if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--search") == 0) {
            if (i + 1 < argc) {
                handle_search(argv[i+1]);
                i++;
            } else {
                cli_failed = 1;
                runepkg_log_verbose("Error: -S/--search requires a file path pattern.");
            }
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            // Already handled at the start of main
        } else if (strcmp(argv[i], "--print-config") == 0) {
            handle_print_config();
        } else if (strcmp(argv[i], "--print-autopool") == 0) {
            handle_print_autopool();
        } else if (strcmp(argv[i], "--rebuild-autocomplete") == 0) {
            handle_update_pkglist();
        } else if (strcmp(argv[i], "--print-config-file") == 0) {
            handle_print_config_file();
        } else if (strcmp(argv[i], "--print-pkglist-file") == 0) {
            handle_print_pkglist_file();
        } else if (strcmp(argv[i], "search") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
#ifdef ENABLE_CPP_FFI
                runepkg_repo_search(argv[i+1]);
#else
                printf("Notice: Repository search requires a C++ build with networking enabled.\n");
                printf("Rebuild with 'make all' to enable this feature.\n");
#endif
                i++;
            } else {
                printf("Error: Search command requires a pattern (e.g., 'runepkg search <pattern>').\n");
            }
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--build") == 0) {
            const char *src = NULL;
            const char *out = NULL;
            if (i + 1 < argc && argv[i+1][0] != '-') {
                src = argv[i+1];
                i++;
                if (i + 1 < argc && argv[i+1][0] != '-') {
                    out = argv[i+1];
                    i++;
                }
            }
            handle_build(src, out);
        } else if (strcmp(argv[i], "download-only") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
#ifdef ENABLE_CPP_FFI
                extern bool g_force_mode;
                bool old_force = g_force_mode;
                g_force_mode = true; // Ignore installed status for download-only
                char *path = runepkg_repo_download(argv[i+1], false);
                g_force_mode = old_force;
                if (path) free(path);
#else
                printf("Notice: Repository downloads require a C++ build with networking enabled.\n");
                printf("Rebuild with 'make all' to enable this feature.\n");
#endif
                i++;
            } else {
                printf("Error: Download-only command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "download-build-depends") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
#ifdef ENABLE_CPP_FFI
                extern bool g_force_mode;
                bool old_force = g_force_mode;
                g_force_mode = true; // Ignore installed status for download-build-depends
                runepkg_repo_build_depends_download(argv[i+1]);
                g_force_mode = old_force;
#else
                printf("Notice: Repository downloads require a C++ build with networking enabled.\n");
                printf("Rebuild with 'make all' to enable this feature.\n");
#endif
                i++;
            } else {
                printf("Error: Download-build-depends command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "download-depends") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
#ifdef ENABLE_CPP_FFI
                extern bool g_force_mode;
                bool old_force = g_force_mode;
                g_force_mode = true; // Ignore installed status for download-depends
                char *path = runepkg_repo_download(argv[i+1], true);
                g_force_mode = old_force;
                if (path) free(path);
#else
                printf("Notice: Repository downloads require a C++ build with networking enabled.\n");
                printf("Rebuild with 'make all' to enable this feature.\n");
#endif
                i++;
            } else {
                printf("Error: Download-depends command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "depends") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
                printf("Feature Pending: Graphical dependency visualizer for '%s' is not yet implemented.\n", argv[i+1]);
                i++;
            } else {
                printf("Error: depends command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "verify") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
                printf("Feature Pending: Cryptographic verification for '%s' is not yet implemented.\n", argv[i+1]);
                i++;
            } else {
                printf("Error: verify command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "update") == 0) {
#ifdef ENABLE_CPP_FFI
            runepkg_update();
#else
            printf("Notice: Repository synchronization requires a C++ build with networking enabled.\n");
            printf("Rebuild with 'make all' to enable this feature.\n");
#endif
        } else if (strcmp(argv[i], "upgrade") == 0) {
#ifdef ENABLE_CPP_FFI
            runepkg_upgrade();
#else
            printf("Notice: Automatic upgrades require a C++ build with networking enabled.\n");
            printf("Rebuild with 'make all' to enable this feature.\n");
#endif
        } else if (strcmp(argv[i], "source") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
#ifdef ENABLE_CPP_FFI
                runepkg_repo_source_download(argv[i+1]);
#else
                printf("Notice: Source package downloads require a C++ build with networking enabled.\n");
                printf("Rebuild with 'make all' to enable this feature.\n");
#endif
                i++;
            } else {
                printf("Error: source command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "source-depends") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
#ifdef ENABLE_CPP_FFI
                runepkg_repo_source_depends_download(argv[i+1]);
#else
                printf("Notice: Source package downloads require a C++ build with networking enabled.\n");
                printf("Rebuild with 'make all' to enable this feature.\n");
#endif
                i++;
            } else {
                printf("Error: source-depends command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "source-build-depends") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
#ifdef ENABLE_CPP_FFI
                runepkg_repo_source_build_depends_download(argv[i+1]);
#else
                printf("Notice: Source package downloads require a C++ build with networking enabled.\n");
                printf("Rebuild with 'make all' to enable this feature.\n");
#endif
                i++;
            } else {
                printf("Error: source-build-depends command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "source-build") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
                handle_source_build(argv[i+1]);
                i++;
            } else {
                printf("Error: source-build command requires a .dsc file path.\n");
            }
        } else {
            cli_failed = 1;
            runepkg_log_verbose("Error: Unknown argument or command: %s", argv[i]);
            break;
        }
        /* Ensure any output produced by the handler is flushed before
         * moving on to the next argument to keep console output ordered
         * as the user expects when commands are interleaved.
         */
        fflush(stdout);
        fflush(stderr);
    }

    // handle_update_pkglist();  // Removed to avoid excessive updates
    runepkg_cleanup();
    return cli_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

/* `handle_print_auto_pkgs` moved to runepkg_handle.c */
