#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "runepkg_config.h"
#include "runepkg_pack.h"
#include "runepkg_hash.h"
#include "runepkg_storage.h"
#include "runepkg_util.h"

// Global variables
bool g_verbose_mode = false;
bool g_very_verbose_mode = false;  // ✨ NEW: Very verbose mode for detailed debugging
bool g_json_output_mode = false;   // ✨ NEW: JSON output mode for automation
bool g_both_output_mode = false;   // ✨ NEW: Both human and JSON output simultaneously
// The declaration for runepkg_main_hash_table is now solely in runepkg_hash.h.
// The definition is in runepkg_hash.c.

// --- Placeholder Function Implementations ---

/**
 * @brief Prints the program's usage information.
 */
void usage(void) {
    printf("runepkg - The Runar Linux package manager.\n\n");
    printf("Usage:\n");
    printf("  runepkg <COMMAND> [OPTIONS] [ARGUMENTS]\n\n");
    printf("Commands and Options:\n");
    printf("  -i, --install <path-to-package.deb>...  Install one or more .deb files.\n");
    printf("  -r, --remove <package-name>             Remove a package.\n");
    printf("  -l, --list                              List all installed packages.\n");
    printf("  -s, --status <package-name>             Show detailed information about a package.\n");
    printf("  -S, --search <query>                    Search for a package by name.\n");
    printf("  -v, --verbose                           Enable verbose output.\n");
    printf("  -vv, --very-verbose                     Enable very verbose debugging output.\n");
    printf("      --json                              Output results in JSON format.\n");
    printf("      --both                              Output both human and JSON formats.\n");
    printf("      --version                           Print version information.\n");
    printf("  -h, --help                              Display this help message.\n\n");
    printf("      --print-config                      Print current configuration settings.\n");
    printf("      --print-config-file                 Print path to configuration file in use.\n");
    printf("Note: Commands can be interleaved, e.g., 'runepkg -v -i pkg1.deb -s pkg2 -i pkg3.deb'\n");
}

/**
 * @brief Prints version information.
 */
void handle_version(void) {
    printf("runepkg v0.1.0 - The Runar Linux package manager\n");
    printf("Copyright (c) 2025 runepkg (Runar Linux) All rights reserved.\n");
    printf("Licensed under GPL v3\n");
}

/**
 * @brief Initializes the runepkg environment with configuration loading.
 * @return 0 on success, -1 on failure.
 */
int runepkg_init(void) {
    runepkg_log_verbose("Initializing runepkg...\n");
    
    // Load configuration from file
    if (runepkg_config_load() != 0) {
        printf("Warning: Failed to load configuration. Using defaults where possible.\n");
        // Continue execution - some functionality may still work
    }
    
    // Initialize hash table if not already created
    if (!runepkg_main_hash_table) {
        runepkg_main_hash_table = runepkg_hash_create_table(INITIAL_HASH_TABLE_SIZE);
        if (!runepkg_main_hash_table) {
            fprintf(stderr, "Error: Failed to create hash table for package management.\n");
            return -1;
        } else {
            runepkg_log_verbose("Hash table initialized for package management.\n");
        }
    }
    
    // TODO: Load installed packages into hash table from persistent storage
    
    return 0;
}

/**
 * @brief Cleans up runepkg environment and frees allocated memory.
 */
void runepkg_cleanup(void) {
    runepkg_log_verbose("Cleaning up runepkg environment...\n");
    
    // Clean up hash table if it exists
    if (runepkg_main_hash_table) {
        runepkg_hash_destroy_table(runepkg_main_hash_table);
        runepkg_main_hash_table = NULL;
    }
    
    // Clean up configuration paths
    runepkg_config_cleanup();
    
    runepkg_log_verbose("runepkg cleanup completed.\n");
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
 * @brief Prints very verbose debugging messages with formatting support.
 */
void runepkg_log_very_verbose(const char *format, ...) {
    if (g_very_verbose_mode) {
        va_list args;
        va_start(args, format);
        
        printf("[DEBUG-VV] ");
        vprintf(format, args);
        va_end(args);
        fflush(stdout);  // Ensure immediate output for debugging
    }
}

/**
 * @brief Outputs JSON-formatted installation report
 */
void output_json_install_report(const char *deb_file_path, const PkgInfo *pkg_info, 
                                int result, double install_time_seconds) {
    if (!g_json_output_mode && !g_both_output_mode) return;
    
    time_t now = time(NULL);
    
    if (g_both_output_mode) {
        printf("\n=== JSON OUTPUT ===\n");
    }
    
    printf("{\n");
    printf("  \"runepkg_version\": \"0.1.0\",\n");
    printf("  \"operation\": \"install\",\n");
    printf("  \"timestamp\": %ld,\n", now);
    printf("  \"target_file\": \"%s\",\n", deb_file_path ? deb_file_path : "null");
    printf("  \"install_result\": {\n");
    printf("    \"success\": %s,\n", result == 0 ? "true" : "false");
    printf("    \"exit_code\": %d,\n", result);
    printf("    \"duration_seconds\": %.6f\n", install_time_seconds);
    printf("  },\n");
    
    if (result == 0 && pkg_info) {
        printf("  \"package_info\": {\n");
        printf("    \"name\": \"%s\",\n", pkg_info->package_name ? pkg_info->package_name : "null");
        printf("    \"version\": \"%s\",\n", pkg_info->version ? pkg_info->version : "null");
        printf("    \"architecture\": \"%s\",\n", pkg_info->architecture ? pkg_info->architecture : "null");
        printf("    \"maintainer\": \"%s\",\n", pkg_info->maintainer ? pkg_info->maintainer : "null");
        printf("    \"description\": \"%s\",\n", pkg_info->description ? pkg_info->description : "null");
        printf("    \"file_count\": %d,\n", pkg_info->file_count);
        printf("    \"installed_size\": \"%s\",\n", pkg_info->installed_size ? pkg_info->installed_size : "null");
        printf("    \"section\": \"%s\",\n", pkg_info->section ? pkg_info->section : "null");
        printf("    \"priority\": \"%s\",\n", pkg_info->priority ? pkg_info->priority : "null");
        printf("    \"homepage\": \"%s\"\n", pkg_info->homepage ? pkg_info->homepage : "null");
        printf("  },\n");
    } else {
        printf("  \"package_info\": null,\n");
    }
    
    printf("  \"system_info\": {\n");
    printf("    \"control_dir\": \"%s\",\n", g_control_dir ? g_control_dir : "null");
    printf("    \"install_root\": \"%s\",\n", g_system_install_root ? g_system_install_root : "null");
    printf("    \"database_dir\": \"%s\",\n", g_runepkg_db_dir ? g_runepkg_db_dir : "null");
    printf("    \"hash_table_initialized\": %s\n", runepkg_main_hash_table ? "true" : "false");
    printf("  },\n");
    
    printf("  \"debug_info\": {\n");
    printf("    \"verbose_mode\": %s,\n", g_verbose_mode ? "true" : "false");
    printf("    \"very_verbose_mode\": %s,\n", g_very_verbose_mode ? "true" : "false");
    printf("    \"json_output_mode\": %s,\n", g_json_output_mode ? "true" : "false");
    printf("    \"both_output_mode\": %s\n", g_both_output_mode ? "true" : "false");
    printf("  }\n");
    printf("}\n");
    
    if (g_both_output_mode) {
        printf("=== END JSON OUTPUT ===\n\n");
    }
}

/**
 * @brief Outputs JSON-formatted error report
 */
void output_json_error_report(const char *operation, const char *error_message, int error_code) {
    if (!g_json_output_mode && !g_both_output_mode) return;
    
    time_t now = time(NULL);
    
    if (g_both_output_mode) {
        printf("\n=== JSON ERROR OUTPUT ===\n");
    }
    
    printf("{\n");
    printf("  \"runepkg_version\": \"0.1.0\",\n");
    printf("  \"operation\": \"%s\",\n", operation ? operation : "unknown");
    printf("  \"timestamp\": %ld,\n", now);
    printf("  \"error\": {\n");
    printf("    \"code\": %d,\n", error_code);
    printf("    \"message\": \"%s\",\n", error_message ? error_message : "Unknown error");
    printf("    \"type\": \"operation_failure\"\n");
    printf("  },\n");
    printf("  \"system_info\": {\n");
    printf("    \"control_dir\": \"%s\",\n", g_control_dir ? g_control_dir : "null");
    printf("    \"database_dir\": \"%s\"\n", g_runepkg_db_dir ? g_runepkg_db_dir : "null");
    printf("  }\n");
    printf("}\n");
    
    if (g_both_output_mode) {
        printf("=== END JSON ERROR OUTPUT ===\n\n");
    }
}

/**
 * @brief Handles package installation with info collection and display.
 */
void handle_install(const char *deb_file_path) {
    // Start timing for performance measurement
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    runepkg_log_verbose("Installing package from: %s\n", deb_file_path);
    runepkg_log_very_verbose("=== DEEP INSTALL ANALYSIS START ===\n");
    runepkg_log_very_verbose("Target .deb file: %s\n", deb_file_path);
    runepkg_log_very_verbose("File size check: ");
    
    // Very verbose: Check file stats
    if (g_very_verbose_mode) {
        struct stat file_stat;
        if (stat(deb_file_path, &file_stat) == 0) {
            printf("%ld bytes\n", file_stat.st_size);
            runepkg_log_very_verbose("File permissions: %o\n", file_stat.st_mode & 0777);
            runepkg_log_very_verbose("File last modified: %s", ctime(&file_stat.st_mtime));
        } else {
            printf("FAILED to stat file\n");
        }
    }
    
    if (!g_json_output_mode && !g_both_output_mode) {
        printf("Installing package from: %s\n", deb_file_path);
    }
    
    if (!g_control_dir) {
        runepkg_log_very_verbose("ERROR: g_control_dir is NULL - configuration not loaded properly\n");
        if (!g_json_output_mode && !g_both_output_mode) {
            printf("Error: Control directory not configured. Please check your runepkg configuration.\n");
        }
        output_json_error_report("install", "Control directory not configured", 1);
        return;
    }
    
    runepkg_log_very_verbose("Using control directory: %s\n", g_control_dir);
    runepkg_log_very_verbose("Hash table status: %s\n", runepkg_main_hash_table ? "INITIALIZED" : "NULL");
    
    // Initialize package info structure
    PkgInfo pkg_info;
    runepkg_pack_init_package_info(&pkg_info);
    runepkg_log_very_verbose("PkgInfo structure initialized at memory address: %p\n", (void*)&pkg_info);
    
    // Extract package and collect information
    if (!g_json_output_mode && !g_both_output_mode) {
        printf("\nExtracting package and collecting information...\n");
    }
    runepkg_log_very_verbose("=== PACKAGE EXTRACTION PHASE ===\n");
    runepkg_log_very_verbose("Calling runepkg_pack_extract_and_collect_info()...\n");
    
    int result = runepkg_pack_extract_and_collect_info(deb_file_path, g_control_dir, &pkg_info);
    
    runepkg_log_very_verbose("Extract function returned: %d (%s)\n", result, result == 0 ? "SUCCESS" : "FAILURE");
    
    if (result == 0) {
        if (!g_json_output_mode && !g_both_output_mode) {
            printf("Package extraction successful!\n\n");
        }
        runepkg_log_very_verbose("=== PACKAGE PROCESSING SUCCESS PATH ===\n");
        runepkg_log_very_verbose("Package name extracted: '%s'\n", pkg_info.package_name ? pkg_info.package_name : "(NULL)");
        runepkg_log_very_verbose("Package version extracted: '%s'\n", pkg_info.version ? pkg_info.version : "(NULL)");
        runepkg_log_very_verbose("Package architecture: '%s'\n", pkg_info.architecture ? pkg_info.architecture : "(NULL)");
        runepkg_log_very_verbose("File count in package: %d\n", pkg_info.file_count);
        
        // FIRST: Print the collected package information
        printf("=== FIRST: Package Info Collected into Struct ===\n");
        runepkg_log_very_verbose("=== STRUCT MEMORY ANALYSIS ===\n");
        if (g_very_verbose_mode) {
            printf("[DEBUG-VV] PkgInfo memory layout:\n");
            printf("[DEBUG-VV]   package_name ptr: %p -> %s\n", (void*)pkg_info.package_name, pkg_info.package_name ? pkg_info.package_name : "(NULL)");
            printf("[DEBUG-VV]   version ptr: %p -> %s\n", (void*)pkg_info.version, pkg_info.version ? pkg_info.version : "(NULL)");
            printf("[DEBUG-VV]   file_list ptr: %p (count: %d)\n", (void*)pkg_info.file_list, pkg_info.file_count);
        }
        runepkg_pack_print_package_info(&pkg_info);
        
        // Add package to hash table if table exists (using the unified PkgInfo struct)
        if (runepkg_main_hash_table) {
            runepkg_log_very_verbose("=== HASH TABLE OPERATION ===\n");
            runepkg_log_very_verbose("Hash table exists, attempting to add package...\n");
            runepkg_log_very_verbose("Hash table address: %p\n", (void*)runepkg_main_hash_table);
            
            if (runepkg_hash_add_package(runepkg_main_hash_table, &pkg_info) == 0) {
                printf("Package successfully added to internal database.\n\n");
                runepkg_log_very_verbose("Hash add operation: SUCCESS\n");
                
                // SECOND: Search and print from hash table to verify integrity
                runepkg_log_very_verbose("=== HASH TABLE VERIFICATION ===\n");
                runepkg_log_very_verbose("Searching for package '%s' in hash table...\n", pkg_info.package_name);
                
                PkgInfo *stored_pkg = runepkg_hash_search(runepkg_main_hash_table, pkg_info.package_name);
                if (stored_pkg) {
                    runepkg_log_very_verbose("Hash search: SUCCESS - package found at address %p\n", (void*)stored_pkg);
                    runepkg_log_very_verbose("Stored package name matches: %s\n", 
                        (stored_pkg->package_name && pkg_info.package_name && strcmp(stored_pkg->package_name, pkg_info.package_name) == 0) ? "YES" : "NO");
                    
                    printf("=== SECOND: Package Info after being added to Hash ===\n");
                    runepkg_hash_print_package_info(stored_pkg);
                } else {
                    runepkg_log_very_verbose("Hash search: FAILED - package not found!\n");
                    printf("Warning: Package not found in hash table after adding.\n");
                }
            } else {
                runepkg_log_very_verbose("Hash add operation: FAILED\n");
                printf("Warning: Failed to add package to internal database.\n");
            }
        } else {
            runepkg_log_very_verbose("Hash table is NULL - skipping hash operations\n");
        }

        // THIRD: Add to persistent storage
        if (pkg_info.package_name && pkg_info.version) {
            printf("\n=== THIRD: Adding to Persistent Storage ===\n");
            runepkg_log_very_verbose("=== PERSISTENT STORAGE OPERATION ===\n");
            runepkg_log_very_verbose("Package name for storage: '%s'\n", pkg_info.package_name);
            runepkg_log_very_verbose("Package version for storage: '%s'\n", pkg_info.version);
            runepkg_log_very_verbose("Database directory: %s\n", g_runepkg_db_dir ? g_runepkg_db_dir : "(NULL)");
            
            // Create package directory in persistent storage
            runepkg_log_very_verbose("Creating package directory in persistent storage...\n");
            if (runepkg_storage_create_package_directory(pkg_info.package_name, pkg_info.version) == 0) {
                runepkg_log_very_verbose("Package directory creation: SUCCESS\n");
                
                // Write package info to persistent storage
                runepkg_log_very_verbose("Writing package info to persistent storage...\n");
                if (runepkg_storage_write_package_info(pkg_info.package_name, pkg_info.version, &pkg_info) == 0) {
                    printf("Package successfully added to persistent storage.\n");
                    runepkg_log_very_verbose("Package info write: SUCCESS\n");
                    
                    // Verify by reading back and printing
                    printf("\n=== Package Info after being added to Persistent Storage ===\n");
                    runepkg_log_very_verbose("=== STORAGE VERIFICATION ===\n");
                    runepkg_log_very_verbose("Reading back package info from persistent storage...\n");
                    if (runepkg_storage_print_package_info(pkg_info.package_name, pkg_info.version) != 0) {
                        runepkg_log_very_verbose("Storage verification: FAILED\n");
                        printf("Warning: Failed to read back package info from persistent storage.\n");
                    } else {
                        runepkg_log_very_verbose("Storage verification: SUCCESS\n");
                    }
                } else {
                    runepkg_log_very_verbose("Package info write: FAILED\n");
                    printf("Warning: Failed to write package info to persistent storage.\n");
                }
            } else {
                runepkg_log_very_verbose("Package directory creation: FAILED\n");
                printf("Warning: Failed to create package directory in persistent storage.\n");
            }
        } else {
            runepkg_log_very_verbose("=== STORAGE SKIP ===\n");
            runepkg_log_very_verbose("Skipping persistent storage - missing data:\n");
            runepkg_log_very_verbose("  package_name: %s\n", pkg_info.package_name ? "present" : "MISSING");
            runepkg_log_very_verbose("  version: %s\n", pkg_info.version ? "present" : "MISSING");
            printf("Warning: Cannot add to persistent storage - missing package name or version.\n");
        }
        
        if (g_verbose_mode && g_system_install_root) {
            printf("\nInstallation Configuration:\n");
            printf("=========================\n");
            printf("  Control dir: %s\n", g_control_dir);
            printf("  Install root: %s\n", g_system_install_root);
            printf("\n");
        }
        
        printf("Package information collection completed successfully.\n");
        printf("Note: Actual installation logic is not yet implemented.\n");
    } else {
        printf("Error: Failed to extract package or collect information.\n");
        runepkg_log_very_verbose("=== EXTRACTION FAILURE ANALYSIS ===\n");
        runepkg_log_very_verbose("Extract function failed with code: %d\n", result);
        runepkg_log_very_verbose("Possible causes:\n");
        runepkg_log_very_verbose("  - File not a valid .deb archive\n");
        runepkg_log_very_verbose("  - Insufficient permissions\n");
        runepkg_log_very_verbose("  - Corrupted package file\n");
        runepkg_log_very_verbose("  - Control directory issues\n");
    }
    
    // Clean up allocated memory
    runepkg_log_very_verbose("=== MEMORY CLEANUP ANALYSIS ===\n");
    runepkg_log_very_verbose("Starting package info cleanup...\n");
    runepkg_log_very_verbose("Package info before cleanup:\n");
    if (g_very_verbose_mode) {
        printf("[DEBUG-VV]   package_name: %s (ptr: %p)\n", 
               pkg_info.package_name ? pkg_info.package_name : "(NULL)", 
               (void*)pkg_info.package_name);
        printf("[DEBUG-VV]   version: %s (ptr: %p)\n", 
               pkg_info.version ? pkg_info.version : "(NULL)", 
               (void*)pkg_info.version);
        printf("[DEBUG-VV]   file_list: %p (count: %d)\n", 
               (void*)pkg_info.file_list, pkg_info.file_count);
    }
    
    runepkg_pack_free_package_info(&pkg_info);
    runepkg_log_very_verbose("Package info cleanup completed\n");
    
    // 🐛 INTENTIONAL FLAW 1: Memory Management - Buffer Overflow
    runepkg_log_very_verbose("=== INTENTIONAL FLAW TESTING ===\n");
    runepkg_log_very_verbose("Testing buffer overflow vulnerability...\n");
    char buffer[50];  // Small buffer
    if (pkg_info.package_name && strlen(pkg_info.package_name) > 0) {
        runepkg_log_very_verbose("Package name length: %zu characters\n", strlen(pkg_info.package_name));
        runepkg_log_very_verbose("Buffer size: 50 bytes\n");
        runepkg_log_very_verbose("Prefix length: 19 characters\n");
        runepkg_log_very_verbose("Available space: 30 characters\n");
        
        strcpy(buffer, "Package processed: ");  // 19 chars + null = 20 chars used
        strcat(buffer, pkg_info.package_name);  // ⚠️ BUFFER OVERFLOW if package name > 30 chars!
        printf("DEBUG: %s\n", buffer);
        runepkg_log_very_verbose("Buffer overflow test completed (may have overflowed!)\n");
    }
    
    // 🐛 INTENTIONAL FLAW 2: Performance - Memory Leak + CPU Waste
    runepkg_log_very_verbose("Testing performance degradation vulnerability...\n");
    runepkg_log_very_verbose("Allocating 100 blocks of 1KB each (100KB total)...\n");
    for (int leak_test = 0; leak_test < 100; leak_test++) {
        char *leaked_memory = malloc(1024);  // ⚠️ NEVER FREED - MEMORY LEAK!
        if (leaked_memory) {
            strcpy(leaked_memory, "This memory will never be freed");
            runepkg_log_very_verbose("Allocated block %d at address %p\n", leak_test, (void*)leaked_memory);
            
            // Intentional CPU waste
            runepkg_log_very_verbose("Performing 10,000 pointless CPU cycles on block %d...\n", leak_test);
            for (int i = 0; i < 10000; i++) {
                leaked_memory[0] = (char)(i % 256);  // ⚠️ POINTLESS CPU CYCLES
            }
        } else {
            runepkg_log_very_verbose("Block %d allocation FAILED\n", leak_test);
        }
    }
    runepkg_log_very_verbose("Performance degradation test completed (100KB leaked + 1M CPU cycles wasted)\n");
    
    // 🐛 INTENTIONAL FLAW 3: Security - Path Traversal Vulnerability  
    runepkg_log_very_verbose("Testing path traversal security vulnerability...\n");
    char malicious_path[256];
    snprintf(malicious_path, sizeof(malicious_path), "/tmp/../../../etc/passwd");
    runepkg_log_very_verbose("Malicious path constructed: %s\n", malicious_path);
    runepkg_log_very_verbose("Attempting to open sensitive system file...\n");
    
    FILE *security_test = fopen(malicious_path, "r");  // ⚠️ PATH TRAVERSAL ATTEMPT!
    if (security_test) {
        runepkg_log_very_verbose("Path traversal: SUCCESS - sensitive file opened!\n");
        runepkg_log_very_verbose("Security vulnerability confirmed - system file accessible\n");
        printf("DEBUG: Successfully opened potentially sensitive file\n");
        fclose(security_test);
    } else {
        runepkg_log_very_verbose("Path traversal: FAILED - access denied or file not found\n");
    }
    
    runepkg_log_very_verbose("=== DEEP INSTALL ANALYSIS END ===\n");
    
    // Calculate installation time
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double install_time = (end_time.tv_sec - start_time.tv_sec) + 
                         (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
    
    runepkg_log_very_verbose("Total installation time: %.6f seconds\n", install_time);
    
    // Output JSON report if requested
    output_json_install_report(deb_file_path, result == 0 ? &pkg_info : NULL, result, install_time);
    
    if (!g_json_output_mode && !g_both_output_mode) {
        if (result == 0) {
            printf("\nInstallation completed successfully in %.3f seconds.\n", install_time);
        } else {
            printf("\nInstallation failed after %.3f seconds.\n", install_time);
        }
    }
}

/**
 * @brief Handles package removal (placeholder).
 */
void handle_remove(const char *package_name) {
    printf("Removing package: %s (placeholder)\n", package_name);
}

/**
 * @brief Lists installed packages.
 */
void handle_list(void) {
    runepkg_log_verbose("Listing installed packages...\n");
    printf("Listing installed packages...\n");
    
    // List from persistent storage
    if (runepkg_storage_list_packages() != 0) {
        printf("Warning: Failed to list packages from persistent storage.\n");
    }
    
    if (g_runepkg_db_dir) {
        runepkg_log_verbose("  Database dir: %s\n", g_runepkg_db_dir);
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

/**
 * @brief Prints the current configuration values.
 */
void handle_print_config(void) {
    printf("runepkg Configuration:\n");
    printf("=====================\n");
    if (g_runepkg_base_dir) {
        printf("  Base Directory:     %s\n", g_runepkg_base_dir);
    } else {
        printf("  Base Directory:     (not set)\n");
    }
    if (g_control_dir) {
        printf("  Control Directory:  %s\n", g_control_dir);
    } else {
        printf("  Control Directory:  (not set)\n");
    }
    if (g_install_dir_internal) {
        printf("  Install Directory:  %s\n", g_install_dir_internal);
    } else {
        printf("  Install Directory:  (not set)\n");
    }
    if (g_system_install_root) {
        printf("  System Install Root: %s\n", g_system_install_root);
    } else {
        printf("  System Install Root: (not set)\n");
    }
     if (g_runepkg_db_dir) {
        printf("  Database Directory: %s\n", g_runepkg_db_dir);
    } else {
        printf("  Database Directory: (not set)\n");
    }
}

/**
 * @brief Prints the path to the configuration file in use.
 */
void handle_print_config_file(void) {
    char *config_path = runepkg_get_config_file_path();
    if (config_path) {
        printf("Configuration file in use: %s\n", config_path);
        free(config_path);
    } else {
        printf("No configuration file found.\n");
        printf("Searched locations:\n");
        printf("  1. $RUNEPKG_CONFIG_PATH environment variable\n");
        printf("  2. /etc/runepkg/runepkgconfig (system-wide)\n");
        printf("  3. ~/.runepkgconfig (user-specific)\n");
    }
}

// --- Main Function ---
int main(int argc, char *argv[]) {
    // Check for verbose and JSON modes first, as they affect all subsequent output.
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_verbose_mode = true;
        } else if (strcmp(argv[i], "-vv") == 0 || strcmp(argv[i], "--very-verbose") == 0) {
            g_verbose_mode = true;      // Very verbose implies verbose
            g_very_verbose_mode = true;
            break;
        } else if (strcmp(argv[i], "--json") == 0) {
            g_json_output_mode = true;
        } else if (strcmp(argv[i], "--both") == 0) {
            g_both_output_mode = true;
        }
    }
    
    runepkg_log_very_verbose("=== RUNEPKG STARTUP ANALYSIS ===\n");
    runepkg_log_very_verbose("Command line arguments: %d\n", argc);
    if (g_very_verbose_mode) {
        for (int i = 0; i < argc; i++) {
            printf("[DEBUG-VV] argv[%d] = '%s'\n", i, argv[i]);
        }
    }
    runepkg_log_very_verbose("Verbose mode: %s\n", g_verbose_mode ? "ENABLED" : "disabled");
    runepkg_log_very_verbose("Very verbose mode: %s\n", g_very_verbose_mode ? "ENABLED" : "disabled");

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
        errormsg("Critical error during program initialization. Exiting.\n");
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
        } else if (strcmp(argv[i], "-vv") == 0 || strcmp(argv[i], "--very-verbose") == 0) {
            // Already handled at the start of main  
        } else if (strcmp(argv[i], "--json") == 0) {
            // Already handled at the start of main
        } else if (strcmp(argv[i], "--both") == 0) {
            // Already handled at the start of main
        } else {
            errormsg("Error: Unknown argument or command: %s", argv[i]);
            // For interleaved commands, we should continue processing if possible
            // For now, let's just break on an unknown command
            break;
        }
    }

    return EXIT_SUCCESS;
    // Note: The atexit handler will now call runepkg_cleanup()
}
