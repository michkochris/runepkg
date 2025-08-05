/******************************************************************************
 * Filename:    runepkg_util.c
 * Author:      <michkochris@gmail.com>
 * Date:        started 01-02-2025
 * Description: Essential utility functions for runepkg (runar linux)
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

#include "runepkg_util.h"
#include "runepkg_defensive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <stdbool.h>
#include <limits.h>
#include <dirent.h>
#include <libgen.h>

// Define PATH_MAX if not defined
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// External global variable for verbose logging
extern bool g_verbose_mode;

// --- Logging Functions ---

void runepkg_util_log_verbose(const char *format, ...) {
    if (!g_verbose_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("[VERBOSE] ");
    vprintf(format, args);
    va_end(args);
}

// Main verbose logging function used throughout the application
void runepkg_log_verbose(const char *format, ...) {
    if (!g_verbose_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("[VERBOSE] ");
    vprintf(format, args);
    va_end(args);
}

void runepkg_util_log_debug(const char *format, ...) {
    if (!g_verbose_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("[DEBUG] ");
    vprintf(format, args);
    va_end(args);
}

void runepkg_util_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "ERROR: ");
    vfprintf(stderr, format, args);
    va_end(args);
}

// --- Memory Management ---

void runepkg_util_free_and_null(char **ptr) {
    if (ptr != NULL && *ptr != NULL) {
        free(*ptr);
        *ptr = NULL;
    }
}

// --- String Manipulation ---

char *runepkg_util_trim_whitespace(char *str) {
    if (str == NULL) return NULL;
    char *end;

    while (isspace((unsigned char)*str)) str++;

    if (*str == 0)
        return str;

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    *(end + 1) = 0;

    return str;
}

char *runepkg_util_safe_strncpy(char *dest, const char *src, size_t n) {
    if (!dest || !src || n == 0) {
        return NULL;
    }
    strncpy(dest, src, n);
    dest[n - 1] = '\0';
    return dest;
}

char *runepkg_util_concat_path(const char *dir, const char *file) {
    return runepkg_secure_path_concat(dir, file);
}

// --- File System Operations ---

int runepkg_util_file_exists(const char *filepath) {
    return (access(filepath, F_OK) == 0);
}

int runepkg_util_create_dir_recursive(const char *path, mode_t mode) {
    char *temp_path = NULL;
    char *p = NULL;
    size_t len;
    int ret = 0;

    if (!path) {
        runepkg_util_log_debug("create_dir_recursive: NULL path provided.\n");
        return -1;
    }

    temp_path = strdup(path);
    if (!temp_path) {
        perror("strdup failed in create_dir_recursive");
        return -1;
    }

    len = strlen(temp_path);
    if (len > 0 && temp_path[len - 1] == '/') {
        temp_path[len - 1] = '\0';
    }

    if (temp_path[0] == '/' && (len == 1 || (len > 1 && temp_path[1] == '\0'))) {
        free(temp_path);
        return 0;
    }

    for (p = temp_path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(temp_path, mode) == -1 && errno != EEXIST) {
                perror("Failed to create directory");
                fprintf(stderr, "Directory: %s\n", temp_path);
                ret = -1;
                break;
            }
            runepkg_util_log_debug("Created directory: %s\n", temp_path);
            *p = '/';
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

char *runepkg_util_read_file_content(const char *filepath, size_t *len) {
    FILE *f = fopen(filepath, "rb");
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

    char *buffer = (char *)malloc(file_size + 1);
    if (!buffer) {
        runepkg_util_error("Memory allocation failed for file content\n");
        fclose(f);
        if (len) *len = 0;
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, file_size, f);
    if (bytes_read != file_size) {
        runepkg_util_log_verbose("Warning: Mismatch in expected vs. actual bytes read for %s\n", filepath);
    }
    buffer[bytes_read] = '\0';

    fclose(f);
    if (len) *len = bytes_read;
    return buffer;
}

int runepkg_util_copy_file(const char *source_path, const char *destination_path) {
    FILE *src, *dest;
    char buffer[4096];
    size_t bytes;
    int ret = 0;

    src = fopen(source_path, "rb");
    if (!src) {
        perror("Error opening source file for copy");
        fprintf(stderr, "Source: %s\n", source_path);
        return -1;
    }

    dest = fopen(destination_path, "wb");
    if (!dest) {
        perror("Error opening destination file for copy");
        fprintf(stderr, "Destination: %s\n", destination_path);
        fclose(src);
        return -1;
    }

    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytes, dest) != bytes) {
            perror("Error writing to destination file during copy");
            ret = -1;
            break;
        }
    }

    if (ferror(src)) {
        perror("Error reading from source file during copy");
        ret = -1;
    }

    fclose(src);
    fclose(dest);

    struct stat st;
    if (stat(source_path, &st) == 0) {
        if (chmod(destination_path, st.st_mode & 0777) == -1) {
            perror("Warning: Could not set permissions on copied file");
        }
    } else {
        perror("Warning: Could not get source file permissions for copy");
    }

    return ret;
}

// --- Configuration File Operations ---

char *runepkg_util_get_config_value(const char *filepath, const char *key, char separator) {
    runepkg_util_log_debug("Entering get_config_value for key '%s' from file '%s'\n", key, filepath);

    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        runepkg_util_log_debug("Failed to open config file '%s'. Error: %s\n", filepath, strerror(errno));
        return NULL;
    }

    char line[PATH_MAX * 2];
    char *value = NULL;
    size_t key_len = strlen(key);

    while (fgets(line, sizeof(line), file) != NULL) {
        runepkg_util_log_debug("Reading line: %s", line);
        char *trimmed_line = runepkg_util_trim_whitespace(line);
        if (strlen(trimmed_line) == 0 || trimmed_line[0] == '#') {
            runepkg_util_log_debug("Skipping empty or comment line.\n");
            continue;
        }

        if (strncmp(trimmed_line, key, key_len) == 0) {
            runepkg_util_log_debug("Found line starting with key '%s'.\n", key);
            char *potential_separator = trimmed_line + key_len;
            while (*potential_separator != '\0' && isspace((unsigned char)*potential_separator)) {
                potential_separator++;
            }

            if (*potential_separator == separator) {
                runepkg_util_log_debug("Found separator '%c'.\n", separator);
                char *start_of_value = potential_separator + 1;
                while (*start_of_value != '\0' && isspace((unsigned char)*start_of_value)) {
                    start_of_value++;
                }

                char *raw_value = strdup(start_of_value);
                if (!raw_value) {
                    runepkg_util_log_debug("Memory allocation failed for raw_value.\n");
                    break;
                }

                char *trimmed_value = runepkg_util_trim_whitespace(raw_value);
                runepkg_util_log_debug("Extracted raw value: '%s'\n", trimmed_value);

                if (trimmed_value[0] == '~' && (trimmed_value[1] == '/' || trimmed_value[1] == '\0')) {
                    char *home_dir = getenv("HOME");
                    if (home_dir) {
                        size_t home_len = strlen(home_dir);
                        size_t value_len = strlen(trimmed_value);
                        value = (char *)malloc(home_len + value_len + 1);
                        if (value) {
                            snprintf(value, home_len + value_len + 1, "%s%s", home_dir, trimmed_value + 1);
                            runepkg_util_log_debug("Expanded '~' to full path: '%s'\n", value);
                            free(raw_value);
                        } else {
                            runepkg_util_log_debug("Memory allocation failed for expanded config value.\n");
                            free(raw_value);
                            value = NULL;
                        }
                    } else {
                        runepkg_util_log_debug("Failed to expand '~': HOME environment variable not set.\n");
                        free(raw_value);
                        value = NULL;
                    }
                } else {
                    runepkg_util_log_debug("No '~' expansion needed.\n");
                    value = raw_value;
                }
                break;
            }
        }
    }

    fclose(file);
    runepkg_util_log_debug("Exiting get_config_value. Result: %s\n", value ? value : "NULL");
    return value;
}

// --- Command Execution ---

int runepkg_util_execute_command(const char *command_path, char *const argv[]) {
    runepkg_util_log_debug("Executing command: %s\n", command_path);
    pid_t pid = fork();

    if (pid == -1) {
        perror("Failed to fork process");
        return -1;
    } else if (pid == 0) {
        execv(command_path, argv);
        perror("Failed to execute command");
        _exit(1);
    } else {
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("Failed to wait for child process");
            return -1;
        }
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == 0) {
                runepkg_util_log_debug("Command '%s' succeeded.\n", command_path);
                return 0;
            } else {
                runepkg_util_error("Command exited with non-zero status: %d\n", WEXITSTATUS(status));
                fprintf(stderr, "  Command: %s\n", command_path);
                return WEXITSTATUS(status);
            }
        } else if (WIFSIGNALED(status)) {
            runepkg_util_error("Command terminated by signal: %d\n", WTERMSIG(status));
            fprintf(stderr, "  Command: %s\n", command_path);
            return -1;
        }
    }
    return -1;
}

// --- .deb Package Operations ---

static int extract_deb_archive(const char *deb_path, const char *destination_dir) {
    runepkg_util_log_verbose("Extracting .deb file '%s' to '%s'...\n", deb_path, destination_dir);

    if (runepkg_util_create_dir_recursive(destination_dir, 0755) != 0) {
        runepkg_util_error("Failed to create destination directory for .deb extraction.\n");
        return -1;
    }

    char *absolute_deb_path = realpath(deb_path, NULL);
    if (!absolute_deb_path) {
        perror("Failed to resolve absolute path for .deb file");
        runepkg_util_error("Could not resolve absolute path for '%s'.\n", deb_path);
        return -1;
    }

    char current_dir[PATH_MAX];
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        perror("getcwd failed");
        runepkg_util_error("Failed to get current working directory.\n");
        free(absolute_deb_path);
        return -1;
    }

    if (chdir(destination_dir) != 0) {
        perror("Failed to change directory for .deb extraction");
        runepkg_util_error("Could not change to '%s'.\n", destination_dir);
        free(absolute_deb_path);
        return -1;
    }

    char *ar_path = "/usr/bin/ar";

    char *argv_ar[] = {
        "ar",
        "-x",
        absolute_deb_path,
        NULL
    };

    int result = runepkg_util_execute_command(ar_path, argv_ar);

    if (chdir(current_dir) != 0) {
        perror("Failed to change back to original directory");
        runepkg_util_log_verbose("Continuing, but directory state is unexpected.\n");
    }

    free(absolute_deb_path);

    if (result != 0) {
        runepkg_util_error("Failed to execute 'ar' for .deb extraction.\n");
        return -1;
    }

    runepkg_util_log_verbose(".deb components extracted successfully.\n");
    return 0;
}

static int find_tar_archives(const char *deb_extract_dir, char **control_archive_path, char **data_archive_path) {
    DIR *dp;
    struct dirent *entry;
    int found_control = 0;
    int found_data = 0;

    *control_archive_path = NULL;
    *data_archive_path = NULL;

    dp = opendir(deb_extract_dir);
    if (dp == NULL) {
        perror("Error opening deb extract directory");
        return -1;
    }

    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (strncmp(entry->d_name, "control.tar.", 12) == 0) {
            *control_archive_path = runepkg_util_concat_path(deb_extract_dir, entry->d_name);
            found_control = 1;
            runepkg_util_log_verbose("Found control archive: %s\n", entry->d_name);
        } else if (strncmp(entry->d_name, "data.tar.", 9) == 0) {
            *data_archive_path = runepkg_util_concat_path(deb_extract_dir, entry->d_name);
            found_data = 1;
            runepkg_util_log_verbose("Found data archive: %s\n", entry->d_name);
        }

        if (found_control && found_data) {
            break;
        }
    }
    closedir(dp);

    if (!found_control || !found_data) {
        runepkg_util_error("Could not find both control.tar.* and data.tar.* archives.\n");
        runepkg_util_free_and_null(control_archive_path);
        runepkg_util_free_and_null(data_archive_path);
        return -1;
    }

    return 0;
}

static int extract_tar_archive(const char *archive_path, const char *destination_dir) {
    if (!archive_path || !destination_dir) {
        runepkg_util_error("extract_tar_archive: NULL archive_path or destination_dir.\n");
        return -1;
    }

    char *archive_name = basename((char*)archive_path);
    runepkg_util_log_verbose("Extracting tar archive '%s' to '%s'...\n", archive_name, destination_dir);

    if (!runepkg_util_file_exists(archive_path)) {
        runepkg_util_error("Tar archive file not found: %s\n", archive_path);
        return -1;
    }

    if (runepkg_util_create_dir_recursive(destination_dir, 0755) != 0) {
        runepkg_util_error("Failed to create destination directory for tar extraction.\n");
        return -1;
    }

    char current_dir[PATH_MAX];
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        perror("getcwd failed");
        runepkg_util_error("Failed to get current working directory.\n");
        return -1;
    }

    if (chdir(destination_dir) != 0) {
        perror("Failed to change directory for tar extraction");
        runepkg_util_error("Could not change to '%s'.\n", destination_dir);
        return -1;
    }

    char *tar_path = "/usr/bin/tar";

    char *argv_tar[] = {
        "tar",
        "-xf",
        (char *)archive_path,
        NULL
    };

    int result = runepkg_util_execute_command(tar_path, argv_tar);

    if (chdir(current_dir) != 0) {
        perror("Failed to change back to original directory");
        runepkg_util_log_verbose("Continuing, but directory state is unexpected.\n");
    }

    if (result != 0) {
        runepkg_util_error("Failed to execute 'tar' for archive extraction.\n");
        return -1;
    }

    runepkg_util_log_verbose("Tar archive extracted successfully.\n");
    return 0;
}

int runepkg_util_extract_deb_complete(const char *deb_path, const char *extract_dir) {
    if (!deb_path || !extract_dir) {
        runepkg_util_error("extract_deb_complete: NULL deb_path or extract_dir.\n");
        return -1;
    }

    runepkg_util_log_verbose("Starting complete .deb extraction of '%s' to '%s'\n", deb_path, extract_dir);

    if (!runepkg_util_file_exists(deb_path)) {
        runepkg_util_error(".deb file not found: %s\n", deb_path);
        return -1;
    }

    char *temp_dir = runepkg_util_concat_path(extract_dir, "temp_deb_extract");
    if (!temp_dir) {
        runepkg_util_error("Failed to create temporary directory path.\n");
        return -1;
    }

    if (extract_deb_archive(deb_path, temp_dir) != 0) {
        runepkg_util_error("Failed to extract .deb archive.\n");
        runepkg_util_free_and_null(&temp_dir);
        return -1;
    }

    char *control_archive_path = NULL;
    char *data_archive_path = NULL;
    if (find_tar_archives(temp_dir, &control_archive_path, &data_archive_path) != 0) {
        runepkg_util_error("Failed to find tar archives in .deb extraction.\n");
        runepkg_util_free_and_null(&temp_dir);
        return -1;
    }

    char *control_extract_dir = runepkg_util_concat_path(extract_dir, "control");
    char *data_extract_dir = runepkg_util_concat_path(extract_dir, "data");
    
    if (!control_extract_dir || !data_extract_dir) {
        runepkg_util_error("Failed to create extraction directory paths.\n");
        runepkg_util_free_and_null(&temp_dir);
        runepkg_util_free_and_null(&control_archive_path);
        runepkg_util_free_and_null(&data_archive_path);
        runepkg_util_free_and_null(&control_extract_dir);
        runepkg_util_free_and_null(&data_extract_dir);
        return -1;
    }

    if (extract_tar_archive(control_archive_path, control_extract_dir) != 0) {
        runepkg_util_error("Failed to extract control archive.\n");
        runepkg_util_free_and_null(&temp_dir);
        runepkg_util_free_and_null(&control_archive_path);
        runepkg_util_free_and_null(&data_archive_path);
        runepkg_util_free_and_null(&control_extract_dir);
        runepkg_util_free_and_null(&data_extract_dir);
        return -1;
    }

    if (extract_tar_archive(data_archive_path, data_extract_dir) != 0) {
        runepkg_util_error("Failed to extract data archive.\n");
        runepkg_util_free_and_null(&temp_dir);
        runepkg_util_free_and_null(&control_archive_path);
        runepkg_util_free_and_null(&data_archive_path);
        runepkg_util_free_and_null(&control_extract_dir);
        runepkg_util_free_and_null(&data_extract_dir);
        return -1;
    }

    runepkg_util_log_verbose("Temporary files left in: %s\n", temp_dir);

    runepkg_util_free_and_null(&temp_dir);
    runepkg_util_free_and_null(&control_archive_path);
    runepkg_util_free_and_null(&data_archive_path);
    runepkg_util_free_and_null(&control_extract_dir);
    runepkg_util_free_and_null(&data_extract_dir);

    runepkg_util_log_verbose("Complete .deb extraction finished successfully.\n");
    runepkg_util_log_verbose("Control files extracted to: %s/control/\n", extract_dir);
    runepkg_util_log_verbose("Data files extracted to: %s/data/\n", extract_dir);
    
    return 0;
}
