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

// Define PATH_MAX if not defined
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "upkg_config.h"

// --- Global Path Variables Definitions ---
char *g_upkg_base_dir = NULL;
char *g_control_dir = NULL;
char *g_unpack_dir = NULL;
char *g_db_dir = NULL; // New global variable definition
char *g_install_dir_internal = NULL;
char *g_system_install_root = NULL;

// --- External Global Variables ---
extern bool g_verbose_mode; // Defined in main.c

// --- Internal Function Declarations ---
static int file_exists(const char *filepath);
static char *trim_whitespace(char *str);
static int create_dir_recursive(const char *path, mode_t mode);
static char *get_config_value(const char *filepath, const char *key, char separator);
static void free_and_null(char **ptr);
static void upkg_log_verbose(const char *format, ...);
static void upkg_log_debug(const char *format, ...);

// --- Internal Configuration System Functions ---

/**
 * @brief Logging function for verbose output
 */
static void upkg_log_verbose(const char *format, ...) {
    if (!g_verbose_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("[VERBOSE] ");
    vprintf(format, args);
    va_end(args);
}

/**
 * @brief Logging function for debug output  
 */
static void upkg_log_debug(const char *format, ...) {
    if (!g_verbose_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("[DEBUG] ");
    vprintf(format, args);
    va_end(args);
}

/**
 * @brief Frees allocated memory pointed to by a char pointer and sets the pointer to NULL.
 * @param ptr A pointer to the char pointer that needs to be freed.
 */
static void free_and_null(char **ptr) {
    if (ptr != NULL && *ptr != NULL) {
        free(*ptr);
        *ptr = NULL;
    }
}

/**
 * @brief Checks if a file or directory exists at the given filepath.
 * @param filepath The path to check.
 * @return 1 if the file exists, 0 if it does not, -1 if an error occurred.
 */
static int file_exists(const char *filepath) {
    return (access(filepath, F_OK) == 0);
}

/**
 * @brief Trims leading and trailing whitespace from a string in-place.
 * @param str The string to trim.
 * @return A pointer to the trimmed string, or NULL if the input string is NULL.
 */
static char *trim_whitespace(char *str) {
    if (str == NULL) return NULL;
    char *end;

    // Trim leading space
    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) // All spaces?
        return str;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    // Write new null terminator
    *(end + 1) = 0;

    return str;
}

/**
 * @brief Creates a directory recursively with the specified permissions.
 * @param path The path to the directory to create.
 * @param mode The permissions for the created directories (e.g., 0755).
 * @return 0 on success, -1 on failure.
 */
static int create_dir_recursive(const char *path, mode_t mode) {
    char *temp_path = NULL;
    char *p = NULL;
    size_t len;
    int ret = 0;

    if (!path) {
        upkg_log_debug("create_dir_recursive: NULL path provided.\n");
        return -1;
    }

    temp_path = strdup(path);
    if (!temp_path) {
        perror("strdup failed in create_dir_recursive");
        return -1;
    }

    len = strlen(temp_path);
    if (len > 0 && temp_path[len - 1] == '/') {
        temp_path[len - 1] = '\0'; // Remove trailing slash
    }

    // Handle root directory specifically if path is just "/" or starts with "//"
    if (temp_path[0] == '/' && (len == 1 || (len > 1 && temp_path[1] == '\0'))) {
        free(temp_path);
        return 0; // Root directory always exists
    }

    for (p = temp_path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0'; // Temporarily terminate string
            if (mkdir(temp_path, mode) == -1 && errno != EEXIST) {
                perror("Failed to create directory");
                fprintf(stderr, "Directory: %s\n", temp_path);
                ret = -1;
                break;
            }
            upkg_log_debug("Created directory: %s\n", temp_path);
            *p = '/'; // Restore slash
        }
    }
    if (ret == 0 && mkdir(temp_path, mode) == -1 && errno != EEXIST) {
        perror("Failed to create final directory");
        fprintf(stderr, "Directory: %s\n", temp_path);
        ret = -1;
    }

    free(temp_path);
    return ret;
}

/**
 * @brief Retrieves a value from a configuration file based on a key and separator.
 * @param filepath The path to the configuration file.
 * @param key The key to search for.
 * @param separator The separator character (e.g., '=').
 * @return A dynamically allocated string containing the value, or NULL if not found.
 */
static char *get_config_value(const char *filepath, const char *key, char separator) {
    upkg_log_debug("Entering get_config_value for key '%s' from file '%s'\n", key, filepath);

    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        upkg_log_debug("Failed to open config file '%s'. Error: %s\n", filepath, strerror(errno));
        return NULL;
    }

    char line[PATH_MAX * 2];
    char *value = NULL;
    size_t key_len = strlen(key);

    while (fgets(line, sizeof(line), file) != NULL) {
        upkg_log_debug("Reading line: %s", line);
        char *trimmed_line = trim_whitespace(line);
        if (strlen(trimmed_line) == 0 || trimmed_line[0] == '#') {
            upkg_log_debug("Skipping empty or comment line.\n");
            continue;
        }

        if (strncmp(trimmed_line, key, key_len) == 0) {
            upkg_log_debug("Found line starting with key '%s'.\n", key);
            char *potential_separator = trimmed_line + key_len;
            while (*potential_separator != '\0' && isspace((unsigned char)*potential_separator)) {
                potential_separator++;
            }

            if (*potential_separator == separator) {
                upkg_log_debug("Found separator '%c'.\n", separator);
                char *start_of_value = potential_separator + 1;
                while (*start_of_value != '\0' && isspace((unsigned char)*start_of_value)) {
                    start_of_value++;
                }

                char *raw_value = strdup(start_of_value);
                if (!raw_value) {
                    upkg_log_debug("Memory allocation failed for raw_value.\n");
                    break;
                }

                char *trimmed_value = trim_whitespace(raw_value);
                upkg_log_debug("Extracted raw value: '%s'\n", trimmed_value);

                // Tilde (~) expansion
                if (trimmed_value[0] == '~' && (trimmed_value[1] == '/' || trimmed_value[1] == '\0')) {
                    char *home_dir = getenv("HOME");
                    if (home_dir) {
                        size_t home_len = strlen(home_dir);
                        size_t value_len = strlen(trimmed_value);
                        value = (char *)malloc(home_len + value_len + 1);
                        if (value) {
                            snprintf(value, home_len + value_len + 1, "%s%s", home_dir, trimmed_value + 1);
                            upkg_log_debug("Expanded '~' to full path: '%s'\n", value);
                            free(raw_value);
                        } else {
                            upkg_log_debug("Memory allocation failed for expanded config value.\n");
                            free(raw_value);
                            value = NULL;
                        }
                    } else {
                        upkg_log_debug("Failed to expand '~': HOME environment variable not set.\n");
                        free(raw_value);
                        value = NULL;
                    }
                } else {
                    upkg_log_debug("No '~' expansion needed.\n");
                    value = raw_value;
                }
                break;
            }
        }
    }

    fclose(file);
    upkg_log_debug("Exiting get_config_value. Result: %s\n", value ? value : "NULL");
    return value;
}

// --- Helper function to find the correct configuration file path ---
static char *get_config_path() {
    char *config_file_path = NULL;

    // 1. Check for environment variable override
    char *env_config_path = getenv("UPKG_CONFIG_PATH");
    if (env_config_path && file_exists(env_config_path)) {
        upkg_log_verbose("Using configuration from UPKG_CONFIG_PATH: %s\n", env_config_path);
        config_file_path = strdup(env_config_path);
        if (!config_file_path) {
            upkg_log_debug("Error: Memory allocation failed for config path.\n");
            return NULL;
        }
        return config_file_path;
    }

    // 2. Check for system-wide configuration
    const char *system_config_path = "/etc/upkg/upkgconfig";
    if (file_exists(system_config_path)) {
        upkg_log_verbose("Using system-wide configuration: %s\n", system_config_path);
        config_file_path = strdup(system_config_path);
        if (!config_file_path) {
            upkg_log_debug("Error: Memory allocation failed for config path.\n");
            return NULL;
        }
        return config_file_path;
    }

    // 3. Check for user-specific configuration
    char *home_dir = getenv("HOME");
    if (home_dir) {
        char user_config_path[PATH_MAX];
        snprintf(user_config_path, sizeof(user_config_path), "%s/.upkgconfig", home_dir);
        if (file_exists(user_config_path)) {
            upkg_log_verbose("Using user-specific configuration: %s\n", user_config_path);
            config_file_path = strdup(user_config_path);
            if (!config_file_path) {
                upkg_log_debug("Error: Memory allocation failed for config path.\n");
                return NULL;
            }
            return config_file_path;
        }
    }

    // If no configuration file was found
    upkg_log_debug("Error: No configuration file found.\n");
    upkg_log_debug("Looked for: 1. $UPKG_CONFIG_PATH, 2. /etc/upkg/upkgconfig, 3. ~/.upkgconfig\n");
    return NULL;
}

int load_upkg_config() {
    char *config_file_path = get_config_path();
    if (!config_file_path) {
        // Error message already printed by get_config_path
        return -1;
    }

    // Free any existing global path variables to prevent leaks on re-entry (if applicable)
    upkg_cleanup_paths();

    // Retrieve the directory paths from the determined config file.
    upkg_log_verbose("Loading configuration values from '%s'...\n", config_file_path);
    g_upkg_base_dir = get_config_value(config_file_path, "upkg_dir", '=');
    if (!g_upkg_base_dir) {
        upkg_log_debug("Error: Failed to read 'upkg_dir' from config file. This is critical.\n");
        free_and_null(&config_file_path);
        upkg_cleanup_paths(); // Clean up anything partially allocated
        return -1;
    }

    g_control_dir = get_config_value(config_file_path, "control_dir", '=');
    if (!g_control_dir) {
        upkg_log_debug("Error: Failed to read 'control_dir' from config file. This is critical.\n");
        free_and_null(&config_file_path);
        upkg_cleanup_paths();
        return -1;
    }

    g_unpack_dir = get_config_value(config_file_path, "unpack_dir", '=');
    if (!g_unpack_dir) {
        upkg_log_debug("Error: Failed to read 'unpack_dir' from config file. This is critical.\n");
        free_and_null(&config_file_path);
        upkg_cleanup_paths();
        return -1;
    }

    g_db_dir = get_config_value(config_file_path, "db_dir", '='); // New value
    if (!g_db_dir) {
        upkg_log_debug("Error: Failed to read 'db_dir' from config file. This is critical.\n");
        free_and_null(&config_file_path);
        upkg_cleanup_paths();
        return -1;
    }

    g_install_dir_internal = get_config_value(config_file_path, "install_dir", '=');
    if (!g_install_dir_internal) {
        upkg_log_debug("Error: Failed to read 'install_dir' from config file. This is critical.\n");
        free_and_null(&config_file_path);
        upkg_cleanup_paths();
        return -1;
    }

    // Assign g_system_install_root from install_dir config value.
    g_system_install_root = strdup(g_install_dir_internal);
    if (!g_system_install_root) {
        upkg_log_debug("Error: Failed to duplicate 'install_dir' for g_system_install_root.\n");
        free_and_null(&config_file_path);
        upkg_cleanup_paths();
        return -1;
    }
    
    free_and_null(&config_file_path); // Free the path string after use

    upkg_log_verbose("Configuration loaded successfully:\n");
    upkg_log_verbose("  upkg_base_dir: %s\n", g_upkg_base_dir);
    upkg_log_verbose("  control_dir: %s\n", g_control_dir);
    upkg_log_verbose("  unpack_dir: %s\n", g_unpack_dir);
    upkg_log_verbose("  db_dir: %s\n", g_db_dir); // New log message
    upkg_log_verbose("  install_dir_internal (record keeping): %s\n", g_install_dir_internal);
    upkg_log_verbose("  system_install_root (actual target): %s\n", g_system_install_root);

    return 0;
}

void upkg_cleanup_paths() {
    upkg_log_verbose("Cleaning up global path variables...\n");
    free_and_null(&g_upkg_base_dir);
    free_and_null(&g_control_dir);
    free_and_null(&g_unpack_dir);
    free_and_null(&g_db_dir); // New cleanup call
    free_and_null(&g_install_dir_internal);
    free_and_null(&g_system_install_root);
}

void upkg_init_paths() {
    // NEW LOGIC: Load paths from upkgconfig
    upkg_log_verbose("Initializing upkg paths from config...\n");
    if (load_upkg_config() != 0) {
        upkg_log_debug("Error: Failed to load upkg configuration. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    // Now, create the directories based on the loaded config paths
    // Check for NULL pointers before calling create_dir_recursive
    if (!g_upkg_base_dir || !g_control_dir || !g_unpack_dir || !g_db_dir || !g_install_dir_internal) {
        upkg_log_debug("Error: One or more critical path variables are NULL after config load. Cannot create directories. Exiting.\n");
        upkg_cleanup_paths(); // Clean up anything that might have been allocated
        exit(EXIT_FAILURE);
    }

    upkg_log_verbose("Creating necessary upkg directories...\n");
    if (create_dir_recursive(g_control_dir, 0755) != 0 ||
        create_dir_recursive(g_unpack_dir, 0755) != 0 ||
        create_dir_recursive(g_db_dir, 0755) != 0 || // New directory creation
        create_dir_recursive(g_install_dir_internal, 0755) != 0) {
        upkg_log_debug("Error: Failed to create necessary upkg directories based on config. Exiting.\n");
        upkg_cleanup_paths();
        exit(EXIT_FAILURE);
    }

    upkg_log_verbose("upkg directories initialized from config:\n");
    upkg_log_verbose("  Base: %s\n", g_upkg_base_dir);
    upkg_log_verbose("  Control: %s\n", g_control_dir);
    upkg_log_verbose("  Unpack: %s\n", g_unpack_dir);
    upkg_log_verbose("  Database: %s\n", g_db_dir); // New log message
    upkg_log_verbose("  Internal Install Records: %s\n", g_install_dir_internal);
    upkg_log_verbose("  System Root (actual install target): %s\n", g_system_install_root);
}
