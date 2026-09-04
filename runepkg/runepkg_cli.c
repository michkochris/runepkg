/******************************************************************************
 * Filename:    runepkg_cli.c
 * Author:      <michkochris@gmail.com>
 * Date:        2026-08-26
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
#include <unistd.h>
#include <limits.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <dirent.h>

#include "runepkg_portable.h"
#include "runepkg_config.h"
#include "runepkg_state.h"
#include "runepkg_pack.h"
#include "runepkg_hash.h"
#include "runepkg_storage.h"
#include "runepkg_util.h"
#include "runepkg_handle.h"
#include "runepkg_completion.h"
#include "runepkg_host.h"

#ifdef ENABLE_CPP_FFI
#include "runepkg_cpp_ffi.h"
#endif

/* Global variables */
bool g_verbose_mode = false;
bool g_force_mode = false;
bool g_completion_mode = false;
bool g_did_install = false;
bool g_debug_mode = false;
bool g_auto_confirm_deps = false;
bool g_auto_confirm_siblings = false;
bool g_asked_siblings = false;

extern void handle_print_autopool(void);

void usage(void) {
    printf("runepkg (fast efficient old-school .deb package manager)\n\n");
    printf("Usage:\n");
    printf("  runepkg <COMMAND> [OPTIONS] [ARGUMENTS]\n\n");

    printf("Core Package Management (Local/Low-Level):\n");
    printf("  sync                                    Synchronize host package database with system state.\n");
    printf("  -i, --install <deb|pkg>...              Install .deb files or repository packages.\n");
    printf("      --install -                         Read .deb paths from stdin.\n");
    printf("      --install @file                     Read .deb paths from a list file.\n");
    printf("  -u, --unpack <package.deb> [dest_dir]   Unpack a .deb into control_dir workspace;\n");
    printf("                                          optionally extract install files to destination directory.\n");
    printf("  -r, --remove <package-name>             Remove an installed package.\n");
    printf("      --remove -                          Read package names from stdin.\n");
    printf("      --remove @file                      Read package names from a list file.\n");
    printf("  -l, --list [pattern]                    List installed packages (optionally matching pattern).\n");
    printf("  -s, --status <package-name>             Show detailed info about an installed package.\n");
    printf("  -L, --list-files <package-name>         List all files owned by an installed package.\n");
    printf("  -S, --search <file-path>                Search installed packages for a specific file.\n");
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
    printf("  info <pkg>...                           Show repository information for a package.\n");
    printf("  depends, resolve-tree <pkg>             Recursive ASCII tree visualization of target depends.\n\n");

    printf("Download Utilities:\n");
    printf("  download-only <pkg>...                  Download a .deb to download_dir without dependencies.\n");
    printf("  download-depends <pkg>...               Download a .deb and its binary dependencies.\n");
    printf("  download-build-depends <pkg>...         Download binary .debs required to build a source package.\n\n");

    printf("Source Package Operations (Network/FFI):\n");
    printf("  source <pkg>...                         Download and extract source package files into build_dir.\n");
    printf("  source-depends <pkg>...                 Download source package files and runtime dependencies.\n");
    printf("  source-build-depends <pkg>...           Download source package files and build-dependency .debs.\n\n");

    printf("Maintenance & Diagnostics:\n");
    printf("      --print-config                      Print all active path and repository settings.\n");
    printf("      --print-config-file                 Show the path to the runepkgconfig file in use.\n");
    printf("      --print-pkglist-file                Show paths to the autocomplete index files.\n");
    printf("      --print-autopool                    Print the contents of the consolidated autocomplete pool.\n");
    printf("      --rebuild-autocomplete              Rebuild the local package name index.\n");
    printf("  transactions [list|inspect <ts|log>]   Audit FSM execution logs, inspect journals, or recover crashed runs.\n");
    printf("                                          Accepts a timestamp or absolute path to a .log file.\n");
    printf("                                          Guarantees atomic state and system integrity by tracking\n");
    printf("                                          transactional boundaries from start to finish.\n");
    printf("  verify <pkg>                            Cryptographic package verification using GPG.\n\n");

    handle_version();
}

int main(int argc, char *argv[]) {
    int i;
    int cli_failed = 0;

    if (argc == 4 && getenv("COMP_LINE") != NULL && is_completion_trigger(argv)) {
        g_completion_mode = true;
        if (runepkg_init() != 0) return 0;
        handle_binary_completion(argv[2], argv[3]);
        return 0;
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_verbose_mode = true;
            continue;
        }
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            g_force_mode = true;
            continue;
        }
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            g_debug_mode = true;
            continue;
        }
    }
    
    runepkg_log_verbose("=== RUNEPKG STARTUP ANALYSIS ===\n");
    runepkg_log_verbose("Command line arguments: %d\n", argc);
    if (g_verbose_mode) {
        for (i = 0; i < argc; i++) {
            printf("[DEBUG-VV] argv[%d] = '%s'\n", i, argv[i]);
        }
    }
    runepkg_log_verbose("Verbose mode: %s\n", g_verbose_mode ? "ENABLED" : "disabled");

    for (i = 1; i < argc; ++i) {
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
    
    runepkg_log_verbose("Starting runepkg with %d arguments\n", argc);
    if (runepkg_init() != 0) {
        runepkg_log_verbose("Critical error during program initialization. Exiting.\n");
        runepkg_cleanup();
        return EXIT_FAILURE;
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--install") == 0 || strcmp(argv[i], "install") == 0) {
#ifdef ENABLE_CPP_FFI
            const char *repo_pkgs[1024];
            int repo_pkg_count = 0;
#endif

            if (i + 1 < argc) {
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
                        break;
                    }

                    if (runepkg_util_file_exists(next_arg)) {
                        if (handle_install(next_arg) == 0) {
                            g_did_install = true;
                        } else {
                            cli_failed = 1;
                        }
                    } else {
                        if (runepkg_main_hash_table && runepkg_hash_search(runepkg_main_hash_table, next_arg) && !g_force_mode) {
                            PkgInfo *info = runepkg_hash_search(runepkg_main_hash_table, next_arg);
                            printf("Package %s is already installed (%s). Use -f/--force to reinstall.\n", next_arg, info->version ? info->version : "unknown");
                        } else {
#ifdef ENABLE_CPP_FFI
                            if (runepkg_repo_package_exists(next_arg)) {
                                if (repo_pkg_count < 1024) {
                                    repo_pkgs[repo_pkg_count++] = next_arg;
                                }
                            } else {
                                if (strchr(next_arg, '.') || strchr(next_arg, '/')) {
                                    fprintf(stderr, "\033[1;31mError:\033[0m File '%s' not found.\n", next_arg);
                                    cli_failed = 1;
                                } else {
                                    if (repo_pkg_count < 1024) {
                                        repo_pkgs[repo_pkg_count++] = next_arg;
                                    }
                                }
                            }
#else
                            fprintf(stderr, "Error: File '%s' not found.\n", next_arg);
                            cli_failed = 1;
#endif
                        }
                    }
                    i++;
                }

#ifdef ENABLE_CPP_FFI
                if (repo_pkg_count > 0) {
                    if (runepkg_repo_install_multiple(repo_pkgs, repo_pkg_count) == 0) {
                        g_did_install = true;
                    } else {
                        cli_failed = 1;
                    }
                }
#endif
            } else {
                if (isatty(STDIN_FILENO)) {
                    fprintf(stderr, "\033[1;31mError:\033[0m install command requires a package name or '-' for stdin.\n");
                    cli_failed = 1;
                } else {
                    handle_install_stdin();
                }
            }
        } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--unpack") == 0 || strcmp(argv[i], "unpack") == 0) {
            if (i + 1 < argc) {
                const char *deb_file = argv[i+1];
                const char *dest_dir = NULL;
                if (i + 2 < argc && argv[i+2][0] != '-') {
                    dest_dir = argv[i+2];
                    i += 2;
                } else {
                    i += 1;
                }
                if (handle_unpack(deb_file, dest_dir) != 0) {
                    cli_failed = 1;
                }
            } else {
                printf("Error: --unpack requires a .deb file path.\n");
                cli_failed = 1;
            }
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--md5check") == 0) {
            if (i + 1 < argc) {
                handle_md5_check(argv[i+1]);
                i++;
            } else {
                printf("Error: --md5check requires a package name.\n");
            }
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--remove") == 0 || strcmp(argv[i], "remove") == 0) {
            char *removed_packages[100];
            int removed_count = 0;
            char *failed_packages[100];
            int failed_count = 0;
            int j;

            if (i + 1 < argc) {
                while (i + 1 < argc) {
                    char *next_arg = argv[i+1];
                    if (strcmp(next_arg, "-") == 0) {
                        i++;
                        handle_remove_stdin();
                        break;
                    }
                    if (next_arg[0] == '@') {
                        i++;
                        handle_remove_listfile(next_arg + 1);
                        break;
                    }
                    if (next_arg[0] == '-') {
                        break;
                    }
                    {
                        int ret = handle_remove(next_arg);
                        if (ret == 0) {
                            if (removed_count < 100) {
                                removed_packages[removed_count++] = strdup(next_arg);
                            }
                        } else if (ret == -2) {
                        } else if (ret != 0) {
                            cli_failed = 1;
                            if (failed_count < 100) {
                                failed_packages[failed_count++] = strdup(next_arg);
                            }
                        }
                    }
                    i++;
                }
            } else {
                if (isatty(STDIN_FILENO)) {
                    fprintf(stderr, "\033[1;31mError:\033[0m remove command requires a package name or '-' for stdin.\n");
                    cli_failed = 1;
                } else {
                    handle_remove_stdin();
                }
            }
            if (removed_count > 0) {
                printf("Successfully removed packages:\n");
                for(j = 0; j < removed_count; j++) {
                    if(j > 0) printf(" ");
                    printf("%s", removed_packages[j]);
                    free(removed_packages[j]);
                }
                printf("\n");
            }
            if (failed_count > 0) {
                printf("Failed to find packages:\n");
                for(j = 0; j < failed_count; j++) {
                    char suggestions[10][PATH_MAX];
                    int suggestion_count;

                    printf("'%s' not installed... did you mean?\n\n", failed_packages[j]);
                    
                    suggestion_count = runepkg_util_get_package_suggestions(failed_packages[j], g_runepkg_db_dir, suggestions, 10);
                    if (suggestion_count > 0) {
                        const char *items[10];
                        int k;
                        for (k = 0; k < suggestion_count; k++) {
                            items[k] = suggestions[k];
                        }
                        runepkg_util_print_columns(items, suggestion_count, "    ");
                    }
                    
                    free(failed_packages[j]);
                }
            }
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0 || strcmp(argv[i], "list") == 0) {
            const char *pattern = NULL;
            if (i + 1 < argc && argv[i+1][0] != '-') {
                pattern = argv[i+1];
                i++;
            }
            handle_list(pattern);
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--status") == 0 || strcmp(argv[i], "status") == 0) {
            if (i + 1 < argc) {
                char *next_arg = argv[i+1];
                if (next_arg[0] == '-' && (strcmp(next_arg, "-L") == 0 || strcmp(next_arg, "--list-files") == 0)) {
                    if (i + 2 < argc && argv[i+2][0] != '-') {
                        int ret = handle_status(argv[i+2]);
                        if (ret != -2 && ret != 0) {
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
                        int rem_ret;
                        if (ret != -2 && ret != 0) {
                            cli_failed = 1;
                            runepkg_log_verbose("Error: Failed to get status for package '%s'.", argv[i+2]);
                        }
                        rem_ret = handle_remove(argv[i+2]);
                        if (rem_ret == 0) {
                            printf("Successfully removed packages:\n%s\n", argv[i+2]);
                        } else if (rem_ret != -2) {
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
                    if (ret != -2 && ret != 0) {
                        cli_failed = 1;
                        runepkg_log_verbose("Error: Failed to get status for package '%s'.", argv[i+1]);
                    }
                    i++;
                }
            } else {
                cli_failed = 1;
                runepkg_log_verbose("Error: -s/--status requires a package name.");
            }
        } else if (strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "--list-files") == 0 || strcmp(argv[i], "list-files") == 0) {
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
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--build") == 0 || strcmp(argv[i], "build") == 0) {
            const char *src_arg = ".";
            const char *out = NULL;
            const char *src = NULL;

            if (i + 1 < argc && argv[i+1][0] != '-') {
                src_arg = argv[i+1];
                i++;
                if (i + 1 < argc && argv[i+1][0] != '-') {
                    out = argv[i+1];
                    i++;
                }
            }

            src = runepkg_util_resolve_build_target(src_arg, g_build_dir);

            if (src) {
                if (runepkg_util_is_directory(src)) {
                    handle_build(src, out);
                } else {
                    fprintf(stderr, "\033[1;31mError:\033[0m '%s' is not a directory. Local build requires a directory.\n", src);
                    cli_failed = 1;
                }
            } else {
                handle_build(NULL, NULL);
            }
        } else if (strcmp(argv[i], "download-only") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
#ifdef ENABLE_CPP_FFI
                const char *pkgs[1024]; int pkg_count = 0;
                while (i + 1 < argc && argv[i+1][0] != '-') {
                    if (pkg_count < 1024) pkgs[pkg_count++] = argv[i+1];
                    i++;
                }
                if (pkg_count > 0) {
                    bool old_force = g_force_mode; g_force_mode = true;
                    if (runepkg_repo_download_multiple(pkgs, pkg_count, false) != 0) cli_failed = 1;
                    g_force_mode = old_force;
                }
#else
                printf("Notice: Repository downloads require a C++ build with networking enabled.\n");
                printf("Rebuild with 'make all' to enable this feature.\n");
                while (i + 1 < argc && argv[i+1][0] != '-') i++;
#endif
            } else {
                printf("Error: Download-only command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "download-build-depends") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
#ifdef ENABLE_CPP_FFI
                const char *pkgs[1024]; int pkg_count = 0;
                while (i + 1 < argc && argv[i+1][0] != '-') {
                    if (pkg_count < 1024) pkgs[pkg_count++] = argv[i+1];
                    i++;
                }
                if (pkg_count > 0) {
                    bool old_force = g_force_mode; g_force_mode = true;
                    if (runepkg_repo_build_depends_download_multiple(pkgs, pkg_count) != 0) cli_failed = 1;
                    g_force_mode = old_force;
                }
#else
                printf("Notice: Repository downloads require a C++ build with networking enabled.\n");
                printf("Rebuild with 'make all' to enable this feature.\n");
                while (i + 1 < argc && argv[i+1][0] != '-') i++;
#endif
            } else {
                printf("Error: Download-build-depends command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "download-depends") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
#ifdef ENABLE_CPP_FFI
                const char *pkgs[1024]; int pkg_count = 0;
                while (i + 1 < argc && argv[i+1][0] != '-') {
                    if (pkg_count < 1024) pkgs[pkg_count++] = argv[i+1];
                    i++;
                }
                if (pkg_count > 0) {
                    bool old_force = g_force_mode; g_force_mode = true;
                    if (runepkg_repo_download_multiple(pkgs, pkg_count, true) != 0) cli_failed = 1;
                    g_force_mode = old_force;
                }
#else
                printf("Notice: Repository downloads require a C++ build with networking enabled.\n");
                printf("Rebuild with 'make all' to enable this feature.\n");
                while (i + 1 < argc && argv[i+1][0] != '-') i++;
#endif
            } else {
                printf("Error: Download-depends command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "source") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
#ifdef ENABLE_CPP_FFI
                const char *pkgs[1024]; int pkg_count = 0;
                while (i + 1 < argc && argv[i+1][0] != '-') {
                    if (pkg_count < 1024) pkgs[pkg_count++] = argv[i+1];
                    i++;
                }
                if (pkg_count > 0) {
                    bool old_force = g_force_mode; g_force_mode = true;
                    if (runepkg_repo_source_download_multiple(pkgs, pkg_count) != 0) cli_failed = 1;
                    g_force_mode = old_force;
                }
#else
                printf("Notice: Source package downloading requires a C++ build with networking enabled.\n");
                printf("Rebuild with 'make all' to enable this feature.\n");
                while (i + 1 < argc && argv[i+1][0] != '-') i++;
#endif
            } else {
                printf("Error: source command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "source-depends") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
#ifdef ENABLE_CPP_FFI
                const char *pkgs[1024]; int pkg_count = 0;
                while (i + 1 < argc && argv[i+1][0] != '-') {
                    if (pkg_count < 1024) pkgs[pkg_count++] = argv[i+1];
                    i++;
                }
                if (pkg_count > 0) {
                    bool old_force = g_force_mode; g_force_mode = true;
                    if (runepkg_repo_source_depends_download_multiple(pkgs, pkg_count) != 0) cli_failed = 1;
                    g_force_mode = old_force;
                }
#else
                printf("Notice: Source package downloading requires a C++ build with networking enabled.\n");
                printf("Rebuild with 'make all' to enable this feature.\n");
                while (i + 1 < argc && argv[i+1][0] != '-') i++;
#endif
            } else {
                printf("Error: source-depends command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "source-build-depends") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
#ifdef ENABLE_CPP_FFI
                const char *pkgs[1024]; int pkg_count = 0;
                while (i + 1 < argc && argv[i+1][0] != '-') {
                    if (pkg_count < 1024) pkgs[pkg_count++] = argv[i+1];
                    i++;
                }
                if (pkg_count > 0) {
                    bool old_force = g_force_mode; g_force_mode = true;
                    if (runepkg_repo_source_build_depends_download_multiple(pkgs, pkg_count) != 0) cli_failed = 1;
                    g_force_mode = old_force;
                }
#else
                printf("Notice: Source package downloading requires a C++ build with networking enabled.\n");
                printf("Rebuild with 'make all' to enable this feature.\n");
                while (i + 1 < argc && argv[i+1][0] != '-') i++;
#endif
            } else {
                printf("Error: source-build-depends command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "depends") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
                while (i + 1 < argc && argv[i+1][0] != '-') {
                    handle_resolve_tree(argv[++i]);
                }
            } else {
                printf("Error: depends command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "verify") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
                while (i + 1 < argc && argv[i+1][0] != '-') {
                    handle_verify_package(argv[i+1]);
                    i++;
                }
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
        } else if (strcmp(argv[i], "sync") == 0) {
            TransactionContext tx_ctx;
            if (runepkg_fsm_init(&tx_ctx, "sync", "1.0") == 0) {
                step_prepare(&tx_ctx);
                runepkg_fsm_transition(&tx_ctx, RUNEPKG_STATE_COMMITTING);
            }
            if (runepkg_host_sync() == 0) {
                printf("Host synchronization successful.\n");
                step_commit(&tx_ctx);
                runepkg_fsm_transition(&tx_ctx, RUNEPKG_STATE_CLEANUP);
            } else {
                cli_failed = 1;
                printf("Host synchronization failed.\n");
                runepkg_fsm_transition(&tx_ctx, RUNEPKG_STATE_ROLLBACK);
            }
            step_cleanup(&tx_ctx);
        } else if (strcmp(argv[i], "transactions") == 0) {
            if (i + 1 < argc && strcmp(argv[i+1], "list") == 0) {
                runepkg_fsm_list_transactions();
                i++;
            } else if (i + 1 < argc && strcmp(argv[i+1], "inspect") == 0) {
                if (i + 2 < argc) {
                    runepkg_fsm_inspect_transaction(argv[i+2]);
                    i += 2;
                } else {
                    printf("Error: transactions inspect requires a log filename or timestamp.\n");
                    cli_failed = 1;
                    i++;
                }
            } else {
                int rec = runepkg_fsm_recover_orphaned_transactions();
                printf("Transaction log directory: %s\n", g_log_dir ? g_log_dir : "/var/lib/runepkg_dir/log");
                if (rec > 0) {
                    printf("Audit complete: Recovered and cleaned %d orphaned transaction workspaces.\n", rec);
                } else {
                    printf("Audit complete: System transaction state is clean.\n");
                }
            }
        } else if (strcmp(argv[i], "upgrade") == 0) {
#ifdef ENABLE_CPP_FFI
            runepkg_upgrade();
#else
            printf("Notice: Automatic upgrades require a C++ build with networking enabled.\n");
            printf("Rebuild with 'make all' to enable this feature.\n");
#endif
        } else if (strcmp(argv[i], "info") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
                while (i + 1 < argc && argv[i+1][0] != '-') {
#ifdef ENABLE_CPP_FFI
                    if (runepkg_repo_info(argv[i+1]) != 0) {
                        cli_failed = 1;
                    }
#else
                    printf("Notice: Repository information requires a C++ build with networking enabled.\n");
                    printf("Rebuild with 'make all' to enable this feature.\n");
#endif
                    i++;
                }
            } else {
                printf("Error: info command requires a package name.\n");
            }
        } else if (strcmp(argv[i], "resolve-tree") == 0 || strcmp(argv[i], "depends") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
                while (i + 1 < argc && argv[i+1][0] != '-') {
                    if (handle_resolve_tree(argv[++i]) != 0) cli_failed = 1;
                }
            } else {
                printf("Error: %s requires a package name.\n", argv[i]);
                cli_failed = 1;
            }
        } else {
            cli_failed = 1;
            fprintf(stderr, "\033[1;31mError:\033[0m Unknown argument or command: %s\n", argv[i]);
            break;
        }

        fflush(stdout);
        fflush(stderr);
    }

    runepkg_cleanup();
    return cli_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
