#include "upkg_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>     // For variadic functions (infomsg, warnmsg, etc.)
#include <unistd.h>     // For access, unlink, rmdir, chdir, geteuid, isatty
#include <sys/stat.h>   // For stat, mkdir, chmod
#include <errno.h>      // For errno
#include <libgen.h>     // For dirname, basename
#include <dirent.h>     // For opendir, readdir
#include <ctype.h>      // For isspace
#include <sys/wait.h>   // For waitpid
#include <stdbool.h>    // For bool type
#include <limits.h>     // For PATH_MAX

// Global variable to control output verbosity. Default is INFO.
int g_log_level = LOG_LEVEL_INFO;

// --- Logging/Messaging Functions ---

/**
 * @brief Helper function to print formatted messages with optional ANSI color codes.
 *
 * This is the core logging function. It checks the provided `level` against the global
 * `g_log_level` before printing anything. It also checks if stdout is a terminal to
 * apply colors, otherwise prints plain text. Ensures messages are flushed immediately.
 *
 * @param level The verbosity level of the message (e.g., LOG_LEVEL_INFO).
 * @param prefix The string prefix for the message (e.g., "[INFO] ").
 * @param color_code ANSI escape code for text color (e.g., "\033[1;34m" for blue).
 * @param format The format string for the message.
 * @param args Variable argument list for the format string.
 */
static void print_message(int level, const char *prefix, const char *color_code, const char *format, va_list args) {
    if (level > g_log_level) {
        return; // Do not print if message level is higher than current log level
    }

    if (isatty(fileno(stdout))) { // Check if stdout is a terminal
        fprintf(stdout, "%s%s", color_code, prefix);
    } else {
        fprintf(stdout, "%s", prefix);
    }
    vfprintf(stdout, format, args);
    if (isatty(fileno(stdout))) {
        fprintf(stdout, "\033[0m\n"); // Reset color
    } else {
        fprintf(stdout, "\n");
    }
    fflush(stdout); // Ensure message is printed immediately
}

/**
 * @brief Prints a debug message to stdout. Only visible in verbose mode.
 * @param format The format string.
 * @param ... Variable arguments.
 */
void dbgmsg(const char *format, ...) {
    va_list args;
    va_start(args, format);
    print_message(LOG_LEVEL_DEBUG, "[DBG] ", "\033[0;35m", format, args); // Magenta for debug
    va_end(args);
}

/**
 * @brief Prints an informational message to stdout.
 * @param format The format string.
 * @param ... Variable arguments.
 */
void infomsg(const char *format, ...) {
    va_list args;
    va_start(args, format);
    print_message(LOG_LEVEL_INFO, "[INFO] ", "\033[1;34m", format, args); // Blue
    va_end(args);
}

/**
 * @brief Prints a warning message to stdout.
 * @param format The format string.
 * @param ... Variable arguments.
 */
void warnmsg(const char *format, ...) {
    va_list args;
    va_start(args, format);
    print_message(LOG_LEVEL_WARN, "[WARN] ", "\033[1;33m", format, args); // Yellow
    va_end(args);
}

/**
 * @brief Prints an error message to stdout.
 * @param format The format string.
 * @param ... Variable arguments.
 */
void errormsg(const char *format, ...) {
    va_list args;
    va_start(args, format);
    print_message(LOG_LEVEL_ERROR, "[ERROR] ", "\033[1;31m", format, args); // Red
    va_end(args);
}

/**
 * @brief Prints a success message to stdout.
 * @param format The format string.
 * @param ... Variable arguments.
 */
void goodmsg(const char *format, ...) {
    va_list args;
    va_start(args, format);
    print_message(LOG_LEVEL_INFO, "[GOOD] ", "\033[1;32m", format, args); // Green
    va_end(args);
}

// --- Memory Management ---

/**
 * @brief Frees allocated memory pointed to by a char pointer and sets the pointer to NULL.
 * @param ptr A pointer to the char pointer that needs to be freed.
 */
void free_and_null(char **ptr) {
    if (ptr != NULL && *ptr != NULL) {
        free(*ptr);
        *ptr = NULL;
    }
}

// --- String Manipulation ---

/**
 * @brief Safely copies a source string to a destination buffer, ensuring null-termination.
 * @param dest The destination buffer.
 * @param src The source string.
 * @param n The size of the destination buffer.
 * @return A pointer to the destination string, or NULL if input is invalid.
 */
char *safe_strncpy(char *dest, const char *src, size_t n) {
    if (!dest || !src || n == 0) {
        return NULL;
    }
    strncpy(dest, src, n);
    dest[n - 1] = '\0'; // Ensure null-termination
    return dest;
}

/**
 * @brief Trims leading and trailing whitespace from a string in-place.
 * @param str The string to trim.
 * @return A pointer to the trimmed string (which is a pointer within the original buffer),
 * or NULL if the input string is NULL.
 */
char *trim_whitespace(char *str) {
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
 * @brief Concatenates a directory path and a filename, handling slashes correctly.
 * @param dir The directory path.
 * @param file The filename.
 * @return A newly allocated string with the full path, or NULL on error.
 * The caller is responsible for freeing the returned string.
 */
char *concat_path(const char *dir, const char *file) {
    if (!dir || !file) {
        return NULL;
    }

    size_t dir_len = strlen(dir);
    size_t file_len = strlen(file);
    bool needs_slash = (dir_len > 0 && dir[dir_len - 1] != '/' && file[0] != '/');
    size_t total_len = dir_len + file_len + (needs_slash ? 1 : 0) + 1; // +1 for null terminator

    char *full_path = (char *)malloc(total_len);
    if (!full_path) {
        perror("Failed to allocate memory for path concatenation");
        return NULL;
    }

    strcpy(full_path, dir);
    if (needs_slash) {
        strcat(full_path, "/");
    }
    strcat(full_path, file);

    return full_path;
}


// --- File System Operations ---

/**
 * @brief Checks if a file or directory exists at the given filepath.
 * @param filepath The path to check.
 * @return 1 if the file exists, 0 if it does not, -1 if an error occurred (e.g., permissions).
 */
int file_exists(const char *filepath) {
    return (access(filepath, F_OK) == 0);
}

/**
 * @brief Reads the entire content of a file into a dynamically allocated buffer.
 * @param filepath The path to the file.
 * @param len A pointer to a size_t where the length of the read content will be stored.
 * @return A pointer to the null-terminated content buffer, or NULL on error.
 * The caller is responsible for freeing the returned buffer.
 */
char *read_file_content(const char *filepath, size_t *len) {
    FILE *f = fopen(filepath, "rb"); // Open in binary mode for exact size reading
    if (!f) {
        if (len) *len = 0;
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long file_size_long = ftell(f);
    if (file_size_long == -1) {
        perror("ftell error");
        fclose(f);
        if (len) *len = 0;
        return NULL;
    }
    size_t file_size = (size_t)file_size_long;
    fseek(f, 0, SEEK_SET);

    char *buffer = (char *)malloc(file_size + 1); // +1 for null terminator
    if (!buffer) {
        perror("Memory allocation failed for file content");
        fclose(f);
        if (len) *len = 0;
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, file_size, f);
    if (bytes_read != file_size) {
        warnmsg("Warning: Mismatch in expected vs. actual bytes read for %s", filepath);
    }
    buffer[bytes_read] = '\0'; // Null-terminate the content

    fclose(f);
    if (len) *len = bytes_read;
    return buffer;
}

/**
 * @brief Creates a directory recursively, creating parent directories as needed.
 * @param path The path to the directory to create.
 * @param mode The permissions for the created directories (e.g., 0755).
 * @return 0 on success, -1 on failure.
 */
int create_dir_recursive(const char *path, mode_t mode) {
    char *temp_path = NULL;
    char *p = NULL;
    size_t len;
    int ret = 0;

    if (!path) {
        errormsg("create_dir_recursive: NULL path provided.");
        return -1;
    }

    temp_path = strdup(path);
    if (!temp_path) {
        perror("strdup failed in create_dir_recursive");
        return -1;
    }

    len = strlen(temp_path);
    if (len > 0 && temp_path[len - 1] == '/') {
        temp_path[len - 1] = '\0'; // Remove trailing slash for correct dirname behavior
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
            dbgmsg("Created directory: %s", temp_path);
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
 * @brief Deletes a single file.
 * @param filepath The path to the file to delete.
 * @return 0 on success (or if file does not exist), -1 on error.
 */
int delete_file(const char *filepath) {
    if (!filepath) {
        errormsg("delete_file: NULL filepath provided.");
        return -1;
    }
    if (unlink(filepath) == 0) {
        dbgmsg("Deleted file: %s", filepath);
        return 0;
    } else {
        if (errno == ENOENT) {
            dbgmsg("File does not exist, nothing to delete: %s", filepath);
            return 0; // File already doesn't exist, consider it success
        } else {
            perror("Error deleting file");
            fprintf(stderr, "File: %s\n", filepath);
            return -1;
        }
    }
}

/**
 * @brief Recursively deletes a directory and all its contents (equivalent to `rm -rf`).
 * @param path The path to the directory to delete.
 * @return 0 on success (or if directory does not exist), -1 on failure.
 */
int delete_directory(const char *path) {
    if (!path) {
        errormsg("delete_directory: NULL path provided.");
        return -1;
    }

    DIR *dp;
    struct dirent *entry;
    char full_path[PATH_MAX];
    int ret = 0;

    dp = opendir(path);
    if (dp == NULL) {
        if (errno == ENOENT) {
            dbgmsg("Directory does not exist, nothing to delete: %s", path);
            return 0; // Directory already doesn't exist, consider it success
        }
        perror("Error opening directory for deletion");
        fprintf(stderr, "Path: %s\n", path);
        return -1;
    }

    dbgmsg("Recursively deleting directory: %s", path);
    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        dbgmsg("Processing entry for deletion: %s", full_path);

        struct stat st;
        if (lstat(full_path, &st) == -1) { // Use lstat to correctly handle symlinks
            perror("Error statting entry during recursive deletion");
            fprintf(stderr, "Path: %s\n", full_path);
            ret = -1;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (delete_directory(full_path) != 0) { // Recursive call
                ret = -1;
            }
        } else {
            if (delete_file(full_path) != 0) {
                ret = -1;
            }
        }
    }

    closedir(dp);

    if (ret == 0) { // Only attempt rmdir if all contents were successfully deleted
        if (rmdir(path) == 0) {
            dbgmsg("Removed directory: %s", path);
            return 0;
        } else {
            perror("Error removing directory");
            fprintf(stderr, "Directory: %s\n", path);
            return -1;
        }
    }
    return ret;
}

/**
 * @brief Deletes all files and subdirectories within a given directory, but not the directory itself.
 * @param path The path to the directory whose contents are to be deleted.
 * @return 0 on success, -1 on failure.
 */
int delete_directory_contents(const char *path) {
    if (!path) {
        errormsg("delete_directory_contents: NULL path provided.");
        return -1;
    }

    DIR *dp;
    struct dirent *entry;
    char full_path[PATH_MAX];
    int ret = 0;

    dp = opendir(path);
    if (dp == NULL) {
        if (errno == ENOENT) {
            dbgmsg("Directory does not exist, nothing to clear: %s", path);
            return 0; // Directory doesn't exist, nothing to clear. Consider it success.
        }
        perror("Error opening directory for content deletion");
        fprintf(stderr, "Path: %s\n", path);
        return -1;
    }

    infomsg("Clearing contents of directory: %s", path);

    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Construct full path for the current entry
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (lstat(full_path, &st) == -1) { // Use lstat to check symlinks too
            perror("Error statting entry during content deletion");
            fprintf(stderr, "Path: %s\n", full_path);
            ret = -1; // Continue trying to delete others, but mark overall failure
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            // It's a directory, recursively delete it
            if (delete_directory(full_path) != 0) { // Uses the rm -rf based delete_directory
                warnmsg("Failed to recursively delete subdirectory during content cleanup.");
                fprintf(stderr, "Subdirectory: %s\n", full_path);
                ret = -1;
            }
        } else {
            // It's a file (or symlink, etc.), delete it
            if (delete_file(full_path) != 0) {
                warnmsg("Failed to delete file during content cleanup.");
                fprintf(stderr, "File: %s\n", full_path);
                ret = -1;
            }
        }
    }

    closedir(dp);
    if (ret == 0) {
        infomsg("Directory contents cleared successfully.");
    } else {
        warnmsg("Directory contents cleared with some errors.");
    }
    return ret;
}


/**
 * @brief (DEPRECATED for general use) Securely creates an empty file and sets rwx permissions.
 * Primarily for testing or specific legacy needs.
 * @param filename The name of the file to touch.
 * @return 0 on success, -1 on failure.
 */
int secure_touch_shebang_rwx(const char *filename) {
    dbgmsg("OLD_FUNCTION_CALL: secure_touch_shebang_rwx called for '%s'. (Deprecated)\n", filename);
    // Placeholder logic: create an empty file and set executable permissions.
    // This is a minimal implementation, not necessarily robust.
    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("OLD_FUNCTION: secure_touch_shebang_rwx: Failed to create file");
        return -1;
    }
    fclose(f);
    if (chmod(filename, 0755) == -1) {
        perror("OLD_FUNCTION: secure_touch_shebang_rwx: Failed to set permissions");
        return -1;
    }
    return 0; // Placeholder success
}


// --- Configuration File functions ---

/**
 * @brief Reads a specific key-value pair from a configuration file and expands '~' to the home directory.
 *
 * This function parses a file for a line starting with the specified 'key',
 * followed by a separator character and then the 'value'. It trims leading/trailing
 * whitespace from the key and value. If the value starts with '~', it expands it
 * to the user's home directory path.
 *
 * @param filepath The path to the configuration file (e.g., upkgconfig, control file).
 * @param key The key to look for (e.g., "upkg_dir", "Package").
 * @param separator The character separating the key and value (e.g., '=' for config, ':' for control files).
 * @return A dynamically allocated string containing the trimmed and expanded value, or NULL if the key is not found
 * or an error occurs. The caller is responsible for freeing this string.
 */
char *get_config_value(const char *filepath, const char *key, char separator) {
    dbgmsg("Entering get_config_value for key '%s' from file '%s'", key, filepath);

    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        errormsg("Failed to open config file '%s'. Error: %s", filepath, strerror(errno));
        return NULL;
    }

    char line[PATH_MAX * 2];
    char *value = NULL;
    size_t key_len = strlen(key);

    while (fgets(line, sizeof(line), file) != NULL) {
        dbgmsg("Reading line: %s", line);
        char *trimmed_line = trim_whitespace(line);
        if (strlen(trimmed_line) == 0 || trimmed_line[0] == '#') {
            dbgmsg("Skipping empty or comment line.");
            continue;
        }

        if (strncmp(trimmed_line, key, key_len) == 0) {
            dbgmsg("Found line starting with key '%s'.", key);
            char *potential_separator = trimmed_line + key_len;
            while (*potential_separator != '\0' && isspace((unsigned char)*potential_separator)) {
                potential_separator++;
            }

            if (*potential_separator == separator) {
                dbgmsg("Found separator '%c'.", separator);
                char *start_of_value = potential_separator + 1;
                while (*start_of_value != '\0' && isspace((unsigned char)*start_of_value)) {
                    start_of_value++;
                }

                char *raw_value = strdup(start_of_value);
                if (!raw_value) {
                    errormsg("Memory allocation failed for raw_value.");
                    break;
                }

                char *trimmed_value = trim_whitespace(raw_value);
                dbgmsg("Extracted raw value: '%s'", trimmed_value);

                // --- NEW LOGIC: Tilde (~) expansion ---
                if (trimmed_value[0] == '~' && (trimmed_value[1] == '/' || trimmed_value[1] == '\0')) {
                    char *home_dir = getenv("HOME");
                    if (home_dir) {
                        size_t home_len = strlen(home_dir);
                        size_t value_len = strlen(trimmed_value);
                        value = (char *)malloc(home_len + value_len + 1); // +1 for null terminator
                        if (value) {
                            snprintf(value, home_len + value_len + 1, "%s%s", home_dir, trimmed_value + 1);
                            dbgmsg("Expanded '~' to full path: '%s'", value);
                            free(raw_value);
                        } else {
                            errormsg("Memory allocation failed for expanded config value.");
                            free(raw_value);
                            value = NULL;
                        }
                    } else {
                        errormsg("Failed to expand '~': HOME environment variable not set.");
                        free(raw_value);
                        value = NULL;
                    }
                } else {
                    dbgmsg("No '~' expansion needed.");
                    value = raw_value;
                }
                break;
            }
        }
    }

    fclose(file);
    dbgmsg("Exiting get_config_value. Result: %s", value ? value : "NULL");
    return value;
}


/**
 * @brief Executes an external command safely in a child process.
 *
 * This function forks a new process and executes the specified command.
 * It waits for the child process to complete and reports its exit status or signal.
 *
 * @param command_path The absolute path to the executable (e.g., "/usr/bin/ar").
 * @param argv An array of null-terminated strings for the arguments (argv[0] is command name).
 * @return 0 on successful command execution, or a non-zero exit status/error code on failure.
 */
int execute_command_safely(const char *command_path, char *const argv[]) {
    dbgmsg("Executing command: %s", command_path);
    pid_t pid = fork();

    if (pid == -1) {
        perror("Failed to fork process");
        return -1;
    } else if (pid == 0) { // Child process
        execv(command_path, argv);
        // If execv returns, an error occurred
        perror("Failed to execute command");
        _exit(1); // Exit child process with error status
    } else { // Parent process
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("Failed to wait for child process");
            return -1;
        }
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == 0) {
                dbgmsg("Command '%s' succeeded.", command_path);
                return 0; // Command succeeded
            } else {
                errormsg("Command exited with non-zero status: %d", WEXITSTATUS(status));
                fprintf(stderr, "  Command: %s\n", command_path);
                return WEXITSTATUS(status);
            }
        } else if (WIFSIGNALED(status)) {
            errormsg("Command terminated by signal: %d", WTERMSIG(status));
            fprintf(stderr, "  Command: %s\n", command_path);
            return -1;
        }
    }
    return -1; // Should not reach here if fork/exec/wait logic is sound
}


// --- .deb Package Specific Operations ---

/**
 * @brief Extracts the main components (debian-binary, control.tar.*, data.tar.*) from a .deb package.
 *
 * Uses the 'ar' utility to extract members of the .deb archive into the specified
 * destination directory. The function temporarily changes the working directory.
 *
 * @param deb_path The full path to the .deb package file.
 * @param destination_dir The directory where the .deb components should be extracted.
 * @return 0 on success, -1 on failure.
 */
int extract_deb(const char *deb_path, const char *destination_dir) {
    infomsg("Extracting .deb file '%s' to '%s'...", deb_path, destination_dir);

    // Ensure destination directory exists, creating it if necessary.
    if (create_dir_recursive(destination_dir, 0755) != 0) {
        errormsg("Failed to create destination directory for .deb extraction.");
        return -1;
    }

    // Save current working directory to return to it later.
    char current_dir[PATH_MAX];
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        perror("getcwd failed");
        errormsg("Failed to get current working directory.");
        return -1;
    }

    // Change to the destination directory where 'ar' will extract files.
    if (chdir(destination_dir) != 0) {
        perror("Failed to change directory for .deb extraction");
        errormsg("Could not change to '%s'.", destination_dir);
        return -1;
    }

    char *ar_path = "/usr/bin/ar"; // Standard path for 'ar' utility.

    // Arguments for 'ar -x <deb_file>'.
    char *argv_ar[] = {
        "ar",
        "-x",
        (char *)deb_path, // Cast to char* because argv expects non-const pointers.
        NULL
    };

    // Execute the 'ar' command.
    int result = execute_command_safely(ar_path, argv_ar);

    // Change back to the original directory.
    if (chdir(current_dir) != 0) {
        perror("Failed to change back to original directory");
        warnmsg("Continuing, but directory state is unexpected.");
    }

    if (result != 0) {
        errormsg("Failed to execute 'ar' for .deb extraction.");
        return -1;
    }

    goodmsg(".deb components extracted successfully.");
    return 0;
}

/**
 * @brief Scans a .deb extraction directory to find the specific names of control.tar.* and data.tar.* archives.
 *
 * Debian packages contain control and data archives which can have various compression suffixes
 * (e.g., .tar.gz, .tar.xz). This function identifies their full filenames.
 *
 * @param deb_extract_dir The directory where the .deb components were extracted.
 * @param control_archive_path A pointer to a char* that will store the dynamically allocated
 * full path to the control archive. Caller must free this memory.
 * @param data_archive_path A pointer to a char* that will store the dynamically allocated
 * full path to the data archive. Caller must free this memory.
 * @return 0 on success (both archives found), -1 on failure.
 */
int find_deb_archive_members(const char *deb_extract_dir, char **control_archive_path, char **data_archive_path) {
    DIR *dp;
    struct dirent *entry;
    int found_control = 0;
    int found_data = 0;

    *control_archive_path = NULL; // Initialize pointers to NULL
    *data_archive_path = NULL;

    dp = opendir(deb_extract_dir);
    if (dp == NULL) {
        perror("Error opening deb extract directory");
        return -1;
    }

    // Iterate through directory entries to find control.tar.* and data.tar.*
    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue; // Skip current and parent directory entries
        }

        if (strncmp(entry->d_name, "control.tar.", 12) == 0) {
            *control_archive_path = concat_path(deb_extract_dir, entry->d_name);
            found_control = 1;
        } else if (strncmp(entry->d_name, "data.tar.", 9) == 0) {
            *data_archive_path = concat_path(deb_extract_dir, entry->d_name);
            found_data = 1;
        }
        if (found_control && found_data) {
            break; // Both found, exit loop early for efficiency
        }
    }
    closedir(dp); // Close the directory stream

    // Check if both archives were successfully found.
    if (!found_control || !found_data) {
        errormsg("Could not find both control.tar.* and data.tar.* archives.");
        free_and_null(control_archive_path); // Free any partially allocated paths on failure
        free_and_null(data_archive_path);
        return -1;
    }

    infomsg("Found control archive: %s", basename(*control_archive_path));
    infomsg("Found data archive: %s", basename(*data_archive_path));
    return 0;
}

/**
 * @brief Extracts the contents of a .tar.* archive (e.g., .tar.gz, .tar.xz) to a destination directory.
 *
 * Uses the 'tar' utility with the '-xf' option to automatically detect compression and extract.
 * The function temporarily changes the working directory.
 *
 * @param archive_path The full path to the .tar.* archive file.
 * @param destination_dir The directory where the archive contents should be extracted.
 * @return 0 on success, -1 on failure.
 */
int extract_tar_archive(const char *archive_path, const char *destination_dir) {
    if (!archive_path || !destination_dir) {
        errormsg("extract_tar_archive: NULL archive_path or destination_dir.");
        return -1;
    }
    // basename expects a mutable string, so cast is used here; consider strdup for robustness.
    infomsg("Extracting tar archive '%s' to '%s'...", basename((char*)archive_path), destination_dir);

    // Verify the archive file actually exists.
    if (!file_exists(archive_path)) {
        errormsg("Tar archive file not found: %s", archive_path);
        return -1;
    }

    // Ensure destination directory exists, creating it if necessary.
    if (create_dir_recursive(destination_dir, 0755) != 0) {
        errormsg("Failed to create destination directory for tar extraction.");
        return -1;
    }

    // Save current working directory to return to it later.
    char current_dir[PATH_MAX];
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        perror("getcwd failed");
        errormsg("Failed to get current working directory.");
        return -1;
    }

    // Change to the destination directory where 'tar' will extract files.
    if (chdir(destination_dir) != 0) {
        perror("Failed to change directory for tar extraction");
        errormsg("Could not change to '%s'.", destination_dir);
        return -1;
    }

    char *tar_path = "/usr/bin/tar"; // Standard path for 'tar' utility.

    // Arguments for 'tar -xf <archive_file>'.
    char *argv_tar[] = {
        "tar",
        "-xf",
        (char *)archive_path, // Cast for execv, as it expects non-const.
        NULL
    };

    // Execute the 'tar' command.
    int result = execute_command_safely(tar_path, argv_tar);

    // Change back to the original directory.
    if (chdir(current_dir) != 0) {
        perror("Failed to change back to original directory");
        warnmsg("Continuing, but directory state is unexpected.");
    }

    if (result != 0) {
        errormsg("Failed to execute 'tar' for archive extraction.");
        return -1;
    }

    goodmsg("Tar archive extracted successfully.");
    return 0;
}

// --- File Installation Helpers ---

/**
 * @brief Copies a file from a source path to a destination path, preserving file permissions.
 *
 * This function performs a byte-by-byte copy and attempts to replicate the source file's
 * permissions (mode) onto the newly created destination file.
 *
 * @param source_path The full path to the source file.
 * @param destination_path The full path to the destination file.
 * @return 0 on success, -1 on failure.
 */
int copy_file(const char *source_path, const char *destination_path) {
    FILE *src, *dest;
    char buffer[4096]; // Buffer for reading/writing chunks
    size_t bytes;
    int ret = 0;

    src = fopen(source_path, "rb"); // Open source file in binary read mode
    if (!src) {
        perror("Error opening source file for copy");
        fprintf(stderr, "Source: %s\n", source_path);
        return -1;
    }

    dest = fopen(destination_path, "wb"); // Open destination file in binary write mode
    if (!dest) {
        perror("Error opening destination file for copy");
        fprintf(stderr, "Destination: %s\n", destination_path);
        fclose(src); // Ensure source file is closed
        return -1;
    }

    // Read from source and write to destination in chunks
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytes, dest) != bytes) {
            perror("Error writing to destination file during copy");
            ret = -1;
            break;
        }
    }

    // Check for read errors on the source file
    if (ferror(src)) {
        perror("Error reading from source file during copy");
        ret = -1;
    }

    fclose(src);  // Close both file streams
    fclose(dest);

    // Preserve permissions: get source permissions and apply to destination
    struct stat st;
    if (stat(source_path, &st) == 0) {
        // Apply only the permission bits (0777 mask) from the source file mode.
        if (chmod(destination_path, st.st_mode & 0777) == -1) {
            perror("Warning: Could not set permissions on copied file");
            // Do not return -1 for this warning, as the file content is already copied.
        }
    } else {
        perror("Warning: Could not get source file permissions for copy");
    }

    return ret;
}

// --- Script Handling Helpers (Writing to file, not executing) ---

/**
 * @brief Writes script content to a specified file and makes it executable.
 *
 * This function creates the necessary parent directories, writes the provided
 * content to the file, and then sets the file's permissions to 0755 (rwxr-xr-x)
 * to make it executable. It does NOT execute the script.
 *
 * @param filepath The full path where the script file should be created.
 * @param content The string content of the script.
 * @return 0 on success, -1 on failure.
 */
int write_script_to_file_and_make_executable(const char *filepath, const char *content) {
    if (!filepath || !content) {
        errormsg("write_script_to_file_and_make_executable: NULL argument.");
        return -1;
    }

    // Duplicate the filepath to allow dirname to modify it safely.
    char *dup_filepath = strdup(filepath);
    if (!dup_filepath) {
        errormsg("Memory allocation failed for dup_filepath.");
        return -1;
    }

    // Get the parent directory path from the duplicated filepath.
    char *parent_dir_buffer = (char *)malloc(strlen(dup_filepath) + 1);
    if (!parent_dir_buffer) {
        errormsg("Memory allocation failed for parent_dir_buffer.");
        free_and_null(&dup_filepath);
        return -1;
    }
    strcpy(parent_dir_buffer, dup_filepath);
    char *parent_dir = dirname(parent_dir_buffer);

    // Ensure parent directory exists for the script, creating recursively if needed.
    if (create_dir_recursive(parent_dir, 0755) != 0) {
        errormsg("Failed to create parent directory for script.");
        free_and_null(&dup_filepath);
        free_and_null(&parent_dir_buffer);
        return -1;
    }
    free_and_null(&dup_filepath);
    free_and_null(&parent_dir_buffer); // Free the buffer used by dirname

    // Open the file for writing.
    FILE *f = fopen(filepath, "w");
    if (!f) {
        perror("Error creating temporary script file");
        fprintf(stderr, "File: %s\n", filepath);
        return -1;
    }

    // Write the script content to the file.
    if (fprintf(f, "%s", content) < 0) {
        perror("Error writing script content to file");
        fclose(f);
        return -1;
    }

    fclose(f); // Close the file after writing.

    // Make the script executable (rwxr-xr-x).
    if (chmod(filepath, 0755) == -1) {
        perror("Error setting executable permissions for temporary script");
        fprintf(stderr, "File: %s\n", filepath);
        return -1;
    }
    return 0; // Success
}
