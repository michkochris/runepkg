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
#include "runepkg_storage.h"
#include "runepkg_util.h"
#include "runepkg_handle.h"

// Global variables
bool g_verbose_mode = false;
bool g_force_mode = false;
bool g_did_install = false;

// Completion functions
static int is_completion_trigger(char *argv[]) {
    // Bash sends exactly 4 arguments for completion: program, cmd, partial, prev
    (void)argv; // Suppress unused parameter warning
    return 1; // Always true for now, since argc==4 check is in main
}

// Struct for runepkg_autocomplete.bin header
typedef struct {
    uint32_t magic;       // 0x52554E45 ("RUNE")
    uint32_t version;     // For schema changes
    uint32_t entry_count; // Total packages
    uint32_t strings_size; // Size of string blob
} AutocompleteHeader;

// From self-completing-binary.txt
struct linux_dirent64 {
    uint64_t        d_ino;
    int64_t         d_off;
    unsigned short  d_reclen;
    unsigned char   d_type;
    char            d_name[];
};
// Forward declarations
static void handle_binary_completion(const char *partial, const char *prev);
static int prefix_search_and_print(const char *prefix);
static void complete_deb_files(const char *partial);
static void handle_print_auto_pkgs(void);
static void handle_binary_completion(const char *partial, const char *prev) {
    // Implementation based on self-completing-binary.txt
    if (strcmp(prev, "runepkg") == 0) {
        if (partial[0] == '-') {
            // Completing global flags or short commands
            if (strncmp(partial, "--", 2) == 0) {
                // Long options that match the partial
                const char *long_opts[] = {
                    "--install", "--remove", "--list", "--status", "--list-files", "--search",
                    "--verbose", "--force", "--version", "--help",
                    "--print-config", "--print-config-file", "--print-pkglist-file", "--print-auto-pkgs"
                };
                int num_long = sizeof(long_opts) / sizeof(long_opts[0]);
                for (int i = 0; i < num_long; i++) {
                    if (strncmp(long_opts[i], partial, strlen(partial)) == 0) {
                        printf("%s\n", long_opts[i]);
                    }
                }
            } else {
                // Short options that match the partial
                const char *short_opts[] = {"-i", "-r", "-l", "-s", "-L", "-S", "-v", "-f", "-h"};
                int num_short = sizeof(short_opts) / sizeof(short_opts[0]);
                for (int i = 0; i < num_short; i++) {
                    if (strncmp(short_opts[i], partial, strlen(partial)) == 0) {
                        printf("%s\n", short_opts[i]);
                    }
                }
            }
        } else {
            // Completing sub-commands that match the partial
            const char *sub_cmds[] = {
                "install", "remove", "list", "status", "list-files", "search",
                "download-only", "depends", "verify", "update"
            };
            int num_sub = sizeof(sub_cmds) / sizeof(sub_cmds[0]);
            for (int i = 0; i < num_sub; i++) {
                if (strncmp(sub_cmds[i], partial, strlen(partial)) == 0) {
                    printf("%s\n", sub_cmds[i]);
                }
            }
        }
    } else if (partial[0] == '-') {
        // Completing command-specific flags
        if (strcmp(prev, "install") == 0) {
            printf("--force\n--verbose\n");
        } else if (strcmp(prev, "remove") == 0) {
            printf("--purge\n--verbose\n");
        } else if (strcmp(prev, "list") == 0) {
            // No specific flags for list yet
        } else if (strcmp(prev, "status") == 0) {
            // No specific flags
        } else if (strcmp(prev, "search") == 0) {
            // No specific flags
        } else {
            // Fallback: global flags
            printf("--help\n--version\n--verbose\n--force\n");
        }
    } else if (strcmp(prev, "install") == 0 || strcmp(prev, "-i") == 0) {
        // Completing .deb files
        complete_deb_files(partial);
    } else if (strcmp(prev, "remove") == 0 || strcmp(prev, "-r") == 0) {
        // Completing package names
        prefix_search_and_print(partial);
    } else if (strcmp(prev, "list") == 0 || strcmp(prev, "-l") == 0 ||
               strcmp(prev, "status") == 0 || strcmp(prev, "-s") == 0) {
        // Completing package names
        prefix_search_and_print(partial);
    }
    // Add more logic as needed
}

// Placeholder for binary index search (from self-completing-binary.txt)
static int prefix_search_and_print(const char *prefix) {
    // Check if index needs rebuild
    char index_path[PATH_MAX];
    if (!g_runepkg_db_dir) return 0;
    snprintf(index_path, sizeof(index_path), "%s/runepkg_autocomplete.bin", g_runepkg_db_dir);

    struct stat index_st, dir_st;
    int index_exists = (stat(index_path, &index_st) == 0);
    int dir_stat = stat(g_runepkg_db_dir, &dir_st);

    if (dir_stat == 0 && (!index_exists || dir_st.st_mtime > index_st.st_mtime)) {
        // Index is stale or missing, rebuild synchronously
        runepkg_storage_build_autocomplete_index();
    }

    int fd = open(index_path, O_RDONLY);
    if (fd < 0) return 0;  // No index file

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return 0;
    }

    void *mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return 0;
    }

    AutocompleteHeader *hdr = (AutocompleteHeader *)mapped;
    if (hdr->magic != 0x52554E45) {  // "RUNE"
        munmap(mapped, st.st_size);
        close(fd);
        return 0;
    }

    uint32_t *offsets = (uint32_t *)((char *)mapped + sizeof(AutocompleteHeader));
    char *names = (char *)mapped + sizeof(AutocompleteHeader) + hdr->entry_count * sizeof(uint32_t);

    // Binary search for first prefix match
    int low = 0, high = hdr->entry_count - 1;
    int first_match = -1;
    while (low <= high) {
        int mid = low + (high - low) / 2;
        char *current_name = names + offsets[mid];
        int cmp = strcmp(prefix, current_name);
        if (cmp == 0) {
            first_match = mid;
            high = mid - 1;  // Find earliest match
        } else if (cmp < 0) {
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }

    if (first_match == -1 && low < (int)hdr->entry_count) {
        char *name = names + offsets[low];
        if (strncmp(prefix, name, strlen(prefix)) == 0) {
            first_match = low;
        }
    }

    // Print all subsequent matches
    if (first_match != -1) {
        for (int i = first_match; i < (int)hdr->entry_count; i++) {
            char *name = names + offsets[i];
            if (strncmp(prefix, name, strlen(prefix)) != 0) break;
            printf("%s\n", name);
        }
    }

    munmap(mapped, st.st_size);
    close(fd);
    return (first_match != -1) ? 1 : 0;
}

// Complete .deb files in current directory and debs/ matching partial
static void complete_deb_files(const char *partial) {
    const char *dirs[] = {".", "debs"};
    for (int d = 0; d < 2; d++) {
        DIR *dir = opendir(dirs[d]);
        if (!dir) continue;

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                const char *name = entry->d_name;
                size_t len = strlen(name);
                if (len > 4 && strcmp(name + len - 4, ".deb") == 0) {
                    char full[PATH_MAX];
                    if (strcmp(dirs[d], ".") == 0) {
                        strcpy(full, name);
                    } else {
                        snprintf(full, sizeof(full), "%s/%s", dirs[d], name);
                    }
                    if (strncmp(full, partial, strlen(partial)) == 0) {
                        printf("%s\n", full);
                    }
                }
            }
        }
        closedir(dir);
    }
}

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
    printf("      --list <pattern>                    List installed packages matching pattern.\n");
    printf("  -s, --status <package-name>             Show detailed information about a package.\n");
    printf("  -L, --list-files <package-name>         List files for a package.\n");
    printf("  -S, --search <file-path>                Search for packages containing files matching path.\n");
    printf("  -v, --verbose                           Enable verbose output.\n");
    printf("  -f, --force                             Force install even if dependencies are missing.\n");
    printf("      --version                           Print version information.\n");
    printf("  -h, --help                              Display this help message.\n\n");
    printf("      --print-config                      Print current configuration settings.\n");
    printf("      --print-config-file                 Print path to configuration file in use.\n");
    printf("      --print-pkglist-file                Print paths to autocomplete files.\n");
    printf("      --print-auto-pkgs                   Print contents of autocomplete index.\n");
    printf("Note: Commands can be interleaved, e.g., 'runepkg -v -i pkg1.deb -s pkg2 -i pkg3.deb'\n");
    printf("\nPlaceholder Commands (silly fun for future features):\n");
    printf("  search <pattern>                       Placeholder: Searches packages with silly magic.\n");
    printf("  download-only <pkg>                    Placeholder: Downloads but skips install, teehee.\n");
    printf("  depends <pkg>                          Placeholder: Shows deps in a goofy way.\n");
    printf("  verify <pkg>                           Placeholder: Verifies package with funny checks.\n");
    printf("  update                                Placeholder: Updates system with wacky updates.\n");
    printf("\nNote: runepkg must be compiled with 'make all' including C++ FFI support.\n");
}

// --- Main Function ---
int main(int argc, char *argv[]) {
    // Completion mode check - must be first
    if (argc == 4 && is_completion_trigger(argv)) {
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
        if (strcmp(argv[i], "--print-auto-pkgs") == 0) {
            // Initialize to load config, then print autocomplete index
            if (runepkg_init() != 0) {
                fprintf(stderr, "ERROR: Failed to initialize runepkg configuration\n");
                return EXIT_FAILURE;
            }
            handle_print_auto_pkgs();
            runepkg_cleanup();
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--print-config-file") == 0) {
            // No need to initialize for this - just find the config file
            handle_print_config_file();
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--print-pkglist-file") == 0) {
            // Initialize first to load config, then print pkglist paths
            if (runepkg_init() != 0) {
                fprintf(stderr, "ERROR: Failed to initialize runepkg configuration\n");
                return EXIT_FAILURE;
            }
            handle_print_pkglist_file();
            runepkg_cleanup();
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
    // atexit(runepkg_cleanup);

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
                    int ret = handle_install(next_arg);
                    if (ret == 0) {
                        g_did_install = true;
                    }
                    i++;
                }
            } else {
                handle_install_stdin();
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
                    } else if (ret != 0 && failed_count < 100) {
                        // Package not found
                        failed_packages[failed_count++] = strdup(next_arg);
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
                int ret = handle_status(argv[i+1]);
                if (ret == -2) {
                    // Suggestions shown, no status displayed - do nothing
                    // The suggestions were already displayed by handle_status
                } else if (ret != 0) {
                    // Error occurred
                    runepkg_log_verbose("Error: Failed to get status for package '%s'.", argv[i+1]);
                }
                i++;
            } else {
                runepkg_log_verbose("Error: -s/--status requires a package name.");
            }
        } else if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--search") == 0) {
            if (i + 1 < argc) {
                handle_search(argv[i+1]);
                i++;
            } else {
                runepkg_log_verbose("Error: -S/--search requires a file path pattern.");
            }
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            // Already handled at the start of main
        } else if (strcmp(argv[i], "search") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
                printf("Silly placeholder: Searching for '%s' with magical unicorns and sparkles!\n", argv[i+1]);
                i++;
            } else {
                printf("Silly placeholder: Search command needs a pattern, like 'runepkg search firefox'!\n");
            }
        } else if (strcmp(argv[i], "download-only") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
                printf("Silly placeholder: Downloading '%s' but not installing, because we're cheeky rebels!\n", argv[i+1]);
                i++;
            } else {
                printf("Silly placeholder: Download-only needs a package name!\n");
            }
        } else if (strcmp(argv[i], "depends") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
                printf("Silly placeholder: Dependencies for '%s' include rainbows, sunshine, and extra cheese!\n", argv[i+1]);
                i++;
            } else {
                printf("Silly placeholder: Depends needs a package name!\n");
            }
        } else if (strcmp(argv[i], "verify") == 0) {
            if (i + 1 < argc && argv[i+1][0] != '-') {
                printf("Silly placeholder: Verifying '%s' with funny checksums, giggles, and a wink!\n", argv[i+1]);
                i++;
            } else {
                printf("Silly placeholder: Verify needs a package name!\n");
            }
        } else if (strcmp(argv[i], "update") == 0) {
            printf("Silly placeholder: Updating the system with confetti, balloons, and virtual hugs!\n");
        } else {
            runepkg_log_verbose("Error: Unknown argument or command: %s", argv[i]);
            // For interleaved commands, we should continue processing if possible
            // For now, let's just break on an unknown command
            break;
        }
    }

    // handle_update_pkglist();  // Removed to avoid excessive updates
    runepkg_cleanup();
    return EXIT_SUCCESS;
    // Note: Cleanup called manually
}

void handle_print_auto_pkgs(void) {
    // Print header like -l
    print_package_data_header();
    printf("Listing installed packages...\n");

    char index_path[PATH_MAX];
    if (!g_runepkg_db_dir) {
        printf("Error: runepkg database directory not configured.\n");
        return;
    }
    snprintf(index_path, sizeof(index_path), "%s/runepkg_autocomplete.bin", g_runepkg_db_dir);

    int fd = open(index_path, O_RDONLY);
    if (fd < 0) {
        printf("Error: Autocomplete index not found: %s\n", index_path);
        return;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        printf("Error: Cannot stat index file.\n");
        close(fd);
        return;
    }

    void *mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        printf("Error: Cannot mmap index file.\n");
        close(fd);
        return;
    }

    AutocompleteHeader *hdr = (AutocompleteHeader *)mapped;
    if (hdr->magic != 0x52554E45) {
        printf("Error: Invalid index file magic.\n");
        munmap(mapped, st.st_size);
        close(fd);
        return;
    }

    uint32_t *offsets = (uint32_t *)((char *)mapped + sizeof(AutocompleteHeader));
    char *names = (char *)mapped + sizeof(AutocompleteHeader) + hdr->entry_count * sizeof(uint32_t);

    // Collect packages
    char *packages[1024];
    uint32_t count = hdr->entry_count;
    size_t max_len = 0;
    for (uint32_t i = 0; i < count && i < 1024; i++) {
        packages[i] = names + offsets[i];
        size_t len = strlen(packages[i]);
        if (len > max_len) max_len = len;
    }

    if (count == 0) {
        munmap(mapped, st.st_size);
        close(fd);
        return;
    }

    // Get terminal width
    struct winsize w;
    int width = 80; // default
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        width = w.ws_col;
    }

    // Calculate number of columns
    int col_width = max_len + 2; // +2 for spacing
    int cols = width / col_width;
    if (cols < 1) cols = 1;

    // Print in columns
    int rows = (count + cols - 1) / cols; // ceil(count / cols)
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = r * cols + c;
            if ((uint32_t)idx < count) {
                printf("%-*s", (int)col_width, packages[idx]);
            }
        }
        printf("\n");
    }

    munmap(mapped, st.st_size);
    close(fd);
}
