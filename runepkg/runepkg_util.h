/******************************************************************************
 * Filename:    runepkg_util.h
 * Author:      <michkochris@gmail.com>
 * Date:        started 01-02-2025
 * Description: Essential utility function declarations for runepkg (runar linux)
 *
 * Copyright (c) 2025 runepkg (runar linux) All rights reserved.
 * GPLV3
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 ******************************************************************************/

#ifndef RUNEPKG_UTIL_H
#define RUNEPKG_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

// Define PATH_MAX if not defined
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// --- Logging Functions ---

/**
 * @brief Logging function for verbose output
 */
void runepkg_util_log_verbose(const char *format, ...);

/**
 * @brief Main verbose logging function used throughout the application
 */
void runepkg_log_verbose(const char *format, ...);

/**
 * @brief Logging function for debug output
 */
void runepkg_util_log_debug(const char *format, ...);

/**
 * @brief Prints error messages with formatting support
 */
void runepkg_util_error(const char *format, ...);

/**
 * @brief Prints security validation messages (successful blocks)
 */
void runepkg_util_security_blocked(const char *format, ...);

// --- Memory Management ---

/**
 * @brief Frees allocated memory pointed to by a char pointer and sets the pointer to NULL.
 * @param ptr A pointer to the char pointer that needs to be freed.
 */
void runepkg_util_free_and_null(char **ptr);

// --- String Manipulation ---

/**
 * @brief Trims leading and trailing whitespace from a string in-place.
 * @param str The string to trim.
 * @return A pointer to the trimmed string, or NULL if the input string is NULL.
 */
char *runepkg_util_trim_whitespace(char *str);

/**
 * @brief Safely copies a source string to a destination buffer, ensuring null-termination.
 * @param dest The destination buffer.
 * @param src The source string.
 * @param n The size of the destination buffer.
 * @return A pointer to the destination string, or NULL if input is invalid.
 */
char *runepkg_util_safe_strncpy(char *dest, const char *src, size_t n);

/**
 * @brief Concatenates a directory path and a filename, handling slashes correctly.
 * @param dir The directory path.
 * @param file The filename.
 * @return A newly allocated string with the full path, or NULL on error.
 * The caller is responsible for freeing the returned string.
 */
char *runepkg_util_concat_path(const char *dir, const char *file);

// --- File System Operations ---

/**
 * @brief Checks if a file or directory exists at the given filepath.
 * @param filepath The path to check.
 * @return 1 if the file exists, 0 if it does not, -1 if an error occurred.
 */
int runepkg_util_file_exists(const char *filepath);

/**
 * @brief Creates a directory recursively with the specified permissions.
 * @param path The path to the directory to create.
 * @param mode The permissions for the created directories (e.g., 0755).
 * @return 0 on success, -1 on failure.
 */
int runepkg_util_create_dir_recursive(const char *path, mode_t mode);

/**
 * @brief Reads the entire content of a file into a dynamically allocated buffer.
 * @param filepath The path to the file.
 * @param len A pointer to a size_t where the length of the read content will be stored.
 * @return A pointer to the null-terminated content buffer, or NULL on error.
 * The caller is responsible for freeing the returned buffer.
 */
char *runepkg_util_read_file_content(const char *filepath, size_t *len);

/**
 * @brief Copies a file from a source path to a destination path, preserving file permissions.
 * @param source_path The full path to the source file.
 * @param destination_path The full path to the destination file.
 * @return 0 on success, -1 on failure.
 */
int runepkg_util_copy_file(const char *source_path, const char *destination_path);

// --- Configuration File Operations ---

/**
 * @brief Reads a specific key-value pair from a configuration file and expands '~' to the home directory.
 * @param filepath The path to the configuration file.
 * @param key The key to look for.
 * @param separator The character separating the key and value (e.g., '=' for config, ':' for control files).
 * @return A dynamically allocated string containing the trimmed and expanded value, or NULL if not found.
 * The caller is responsible for freeing this string.
 */
char *runepkg_util_get_config_value(const char *filepath, const char *key, char separator);

// --- .deb Package Operations ---

/**
 * @brief Extracts a .deb package completely into the specified directory.
 * * This function performs a complete .deb extraction:
 * 1. Creates the destination directory if it doesn't exist
 * 2. Extracts the .deb archive using 'ar' command
 * 3. Finds and extracts both control.tar.* and data.tar.* archives
 * 4. Organizes extracted files into subdirectories
 *
 * @param deb_path The full path to the .deb package file.
 * @param extract_dir The directory where the .deb should be extracted.
 * @return 0 on success, -1 on failure.
 */
int runepkg_util_extract_deb_complete(const char *deb_path, const char *extract_dir);

/**
 * @brief Executes an external command safely in a child process.
 * @param command_path The absolute path to the executable.
 * @param argv An array of null-terminated strings for the arguments.
 * @return 0 on successful command execution, or a non-zero exit status/error code on failure.
 */
int runepkg_util_execute_command(const char *command_path, char *const argv[]);

/**
 * @brief Parses the Depends field from a package control file.
 * @param depends The depends string (e.g., "libc6 (>= 2.2.5), libsomething").
 * @return A null-terminated array of package names, or NULL on error. Caller must free.
 */
char **parse_depends(const char *depends);

/**
 * @brief Calculates the total size of a directory recursively.
 * @param path The path to the directory.
 * @return The total size in bytes, or 0 on error.
 */
off_t runepkg_util_get_dir_size(const char *path);

/**
 * @brief Formats a size in bytes to a human-readable string (KB/MB/GB).
 * @param size_bytes The size in bytes.
 * @param buffer The buffer to store the formatted string.
 * @param buffer_size The size of the buffer.
 * @return A pointer to the buffer containing the formatted string.
 */
char *runepkg_util_format_size(off_t size_bytes, char *buffer, size_t buffer_size);

/**
 * @brief Gets the terminal width, falling back to 80 if unavailable.
 * @return The terminal width in columns.
 */
int runepkg_util_get_terminal_width(void);

/**
 * @brief Prints an array of strings in columnar format to fit the terminal width.
 * @param items Array of strings to print.
 * @param count Number of items in the array.
 */
void runepkg_util_print_columns(const char *items[], int count);

/**
 * @brief Generates package name suggestions based on a search string.
 * @param search_name The name to search for.
 * @param db_dir The database directory to search in.
 * @param suggestions Array to store suggestions (must be pre-allocated).
 * @param max_suggestions Maximum number of suggestions to return.
 * @return Number of suggestions found.
 */
int runepkg_util_get_package_suggestions(const char *search_name, const char *db_dir, char suggestions[][PATH_MAX], int max_suggestions);

#endif // RUNEPKG_UTIL_H
