#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <getopt.h> // Still needed for getopt_long, although the main loop is manual

// Project-specific headers
#include "upkg_lib.h"
#include "upkg_config.h"
#include "upkg_hash.h"
#include "upkg_struct.h"
#include "upkg_exec.h"
//#include "upkg_cli.h" non existant...

// --- Global Variables ---
// These are declared in upkg_config.c and upkg_hash.c
// extern HashTable *upkg_main_hash_table;
// extern bool g_verbose_mode;
// extern char *g_db_dir;
// extern char *g_system_install_root;
// ... other global paths

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
 * @brief Initializes the upkg environment.
 * Sets up configuration, creates directories, and initializes the hash table.
 * @return 0 on success, -1 on failure.
 */
int upkg_init(void) {
    upkg_log_verbose("Initializing upkg environment...\n");

    upkg_init_paths();
    upkg_log_verbose("Paths initialized.\n");

    if (mkdir_recursive(g_db_dir) != 0) {
        errormsg("Error: Could not create database directory '%s': %s\n", g_db_dir, strerror(errno));
        return -1;
    }
    upkg_log_verbose("Database directory '%s' exists.\n", g_db_dir);

    upkg_main_hash_table = create_hash_table(INITIAL_HASH_TABLE_SIZE);
    if (!upkg_main_hash_table) {
        errormsg("Error: Failed to create global hash table.\n");
        return -1;
    }

    if (load_package_database() != 0) {
        warnmsg("Failed to load package database from disk. Starting with an empty database.\n");
    }

    upkg_log_verbose("upkg initialization complete.\n");
    return 0;
}

/**
 * @brief Loads the package database from binary files on disk into the hash table.
 * @return 0 on success, -1 on failure.
 */
int load_package_database(void) {
    upkg_log_verbose("Loading package database from '%s'...\n", g_db_dir);

    if (!upkg_main_hash_table) {
        errormsg("Error: Hash table not initialized. Cannot load database.\n");
        return -1;
    }

    char **file_list = NULL;
    int file_count = get_files_in_dir(g_db_dir, &file_list);

    if (file_count < 0) {
        errormsg("Error: Failed to scan database directory '%s'.\n", g_db_dir);
        return -1;
    }

    if (file_count == 0) {
        infomsg("No existing packages found in database.\n");
        free_file_list(file_list, 0); // Free the empty array
        return 0;
    }

    for (int i = 0; i < file_count; ++i) {
        char *pkgname = strdup(file_list[i]);
        size_t len = strlen(pkgname);
        if (len > 5 && strcmp(pkgname + len - 5, ".info") == 0) {
            pkgname[len - 5] = '\0';
            Pkginfo *loaded_pkg = load_pkginfo(pkgname);
            if (loaded_pkg != NULL) {
                upkg_log_verbose("Loaded package '%s' from disk.\n", loaded_pkg->pkgname);
                add_package_deep_copy(upkg_main_hash_table, loaded_pkg);
            } else {
                upkg_log_debug("Warning: Failed to load package info for '%s'. Skipping.\n", pkgname);
            }
        }
        free(pkgname);
    }
    
    free_file_list(file_list, file_count);
    goodmsg("Finished loading %zu packages into hash table.\n", upkg_main_hash_table->count);
    
    return 0;
}


/**
 * @brief Frees all allocated resources before the program exits.
 * Saves the hash table to disk before destroying it.
 */
void upkg_cleanup(void) {
    upkg_log_verbose("Cleaning up upkg environment...\n");

    infomsg("Saving package database to disk...");
    if (upkg_main_hash_table) {
        for (size_t i = 0; i < upkg_main_hash_table->size; ++i) {
            Node *current = upkg_main_hash_table->buckets[i];
            while (current != NULL) {
                if (save_pkginfo(&(current->data)) != 0) {
                    warnmsg("Failed to save package '%s' to disk during cleanup.", current->data.pkgname);
                }
                current = current->next;
            }
        }
    }
    
    if (upkg_main_hash_table) {
        destroy_hash_table(upkg_main_hash_table);
    }

    upkg_cleanup_paths();
    
    goodmsg("Cleanup complete. All resources freed.\n");
}


// --- Command Handlers ---
// (These remain the same as the previous version, as their internal logic is correct.
// They are called by the new main() loop instead of the getopt_long loop.)

void handle_install(const char *deb_file_path) {
    if (!deb_file_path) {
        errormsg("handle_install: NULL deb_file_path provided.");
        return;
    }

    infomsg("Processing .deb file: %s", deb_file_path);

    if (!file_exists(deb_file_path)) {
        errormsg("Error: .deb file not found at %s.", deb_file_path);
        return;
    }

    upkg_log_verbose("Cleaning temporary directories...");
    delete_directory_contents(g_control_dir);
    delete_directory_contents(g_unpack_dir);
    
    mkdir_recursive(g_control_dir);
    mkdir_recursive(g_unpack_dir);

    infomsg("Extracting .deb file components to control directory: %s", g_control_dir);
    if (extract_deb(deb_file_path, g_control_dir) != 0) {
        errormsg("Failed to extract .deb file components.");
        return;
    }

    char *control_archive_path = NULL;
    char *data_archive_path = NULL;
    if (find_deb_archive_members(g_control_dir, &control_archive_path, &data_archive_path) != 0) {
        errormsg("Could not find control.tar.* or data.tar.* archives in control directory.");
        free_and_null(&control_archive_path);
        free_and_null(&data_archive_path);
        return;
    }

    infomsg("Extracting control archive to %s...", g_control_dir);
    if (extract_tar_archive(control_archive_path, g_control_dir) != 0) {
        errormsg("Failed to extract control archive.");
        free_and_null(&control_archive_path);
        free_and_null(&data_archive_path);
        return;
    }

    infomsg("Extracting data archive to %s...", g_unpack_dir);
    if (extract_tar_archive(data_archive_path, g_unpack_dir) != 0) {
        errormsg("Failed to extract data archive.");
        free_and_null(&control_archive_path);
        free_and_null(&data_archive_path);
        return;
    }
    
    goodmsg("Successfully extracted .deb package components and archives.");
    free_and_null(&control_archive_path);
    free_and_null(&data_archive_path);

    infomsg("Gathering package information...");
    Pkginfo new_pkg_info;
    memset(&new_pkg_info, 0, sizeof(Pkginfo));

    create_fully_populated_pkginfo(g_control_dir, g_unpack_dir, &new_pkg_info);

    if (new_pkg_info.pkgname[0] == '\0') {
        errormsg("Failed to gather essential package information. Aborting installation.");
        free_pkginfo_members(&new_pkg_info);
        return;
    }
    
    infomsg("Package '%s' detected. Running installation scripts.", new_pkg_info.pkgname);

    if (check_dependencies(&new_pkg_info) != 0) {
        errormsg("Failed to resolve dependencies for '%s'. Aborting installation.", new_pkg_info.pkgname);
        free_pkginfo_members(&new_pkg_info);
        return;
    }

    if (new_pkg_info.preinst) {
        infomsg("Executing preinst script...");
        if (execute_script_from_memory(new_pkg_info.preinst, new_pkg_info.preinst_len) != 0) {
            errormsg("Preinst script for '%s' failed. Aborting.", new_pkg_info.pkgname);
            free_pkginfo_members(&new_pkg_info);
            return;
        }
    }
    
    infomsg("Installing files to system root: %s", g_system_install_root);
    if (install_files(&new_pkg_info, g_unpack_dir, g_system_install_root) != 0) {
        errormsg("Failed to install package files for '%s'. Aborting.", new_pkg_info.pkgname);
        free_pkginfo_members(&new_pkg_info);
        return;
    }

    if (add_package_deep_copy(upkg_main_hash_table, &new_pkg_info) != 0) {
        errormsg("Failed to add package information to the hash table. Aborting installation.");
        return;
    }
    
    Pkginfo *installed_pkg = search(upkg_main_hash_table, new_pkg_info.pkgname);
    if (!installed_pkg || save_pkginfo(installed_pkg) != 0) {
        errormsg("Warning: Package '%s' installed, but failed to save info to disk.", new_pkg_info.pkgname);
    }
    
    if (installed_pkg->postinst) {
        infomsg("Executing postinst script...");
        if (execute_script_from_memory(installed_pkg->postinst, installed_pkg->postinst_len) != 0) {
            warnmsg("Postinst script for '%s' failed. Post-installation steps may be incomplete.", installed_pkg->pkgname);
        }
    }
    
    infomsg("Cleaning up temporary directories...");
    delete_directory_contents(g_control_dir);
    delete_directory_contents(g_unpack_dir);
    
    goodmsg("Finished installing package '%s'!", installed_pkg->pkgname);
}

void handle_remove(const char *package_name) {
    Pkginfo *pkg_to_remove = search(upkg_main_hash_table, package_name);
    if (!pkg_to_remove) {
        printf("Package '%s' is not installed.\n", package_name);
        return;
    }
    
    infomsg("Starting removal for package '%s'...", package_name);

    if (pkg_to_remove->prerm) {
        infomsg("Executing prerm script...");
        if (execute_script_from_memory(pkg_to_remove->prerm, pkg_to_remove->prerm_len) != 0) {
            warnmsg("Prerm script for '%s' failed. Aborting removal.", package_name);
            return;
        }
    }
    
    infomsg("Removing files for '%s'...", package_name);
    if (remove_files(pkg_to_remove, g_system_install_root) != 0) {
        errormsg("Failed to remove all files for '%s'. Continuing to clean up database.", package_name);
    }

    char *pkginfo_path = get_pkginfo_path(package_name);
    if (pkginfo_path) {
        if (unlink(pkginfo_path) != 0) {
            warnmsg("Warning: Failed to remove binary file for '%s': %s", package_name, strerror(errno));
        } else {
            upkg_log_verbose("Removed binary file: %s\n", pkginfo_path);
        }
        free_and_null(&pkginfo_path);
    }

    removepkg(upkg_main_hash_table, package_name);
    
    if (pkg_to_remove->postrm) {
        infomsg("Executing postrm script...");
        if (execute_script_from_memory(pkg_to_remove->postrm, pkg_to_remove->postrm_len) != 0) {
            warnmsg("Postrm script for '%s' failed. Post-removal steps may be incomplete.", package_name);
        }
    }
    
    goodmsg("Package '%s' successfully removed.\n", package_name);
}

void handle_list(void) {
    if (!upkg_main_hash_table || upkg_main_hash_table->count == 0) {
        printf("No packages are currently installed.\n");
        return;
    }
    infomsg("Listing installed packages:");
    list(upkg_main_hash_table);
}

void handle_status(const char *package_name) {
    upkg_log_verbose("Showing status for package '%s'.\n", package_name);
    if (!upkg_main_hash_table) {
        printf("Package database is not initialized. Cannot show status.\n");
        return;
    }
    status_search(upkg_main_hash_table, package_name);
}

void handle_search(const char *query) {
    upkg_log_verbose("Searching for packages with query '%s'.\n", query);
    if (!upkg_main_hash_table) {
        printf("Package database is not initialized. Cannot search.\n");
        return;
    }
    print_suggestions(upkg_main_hash_table, query);
}

void handle_version() {
    infomsg("upkg v0.1.0");
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
