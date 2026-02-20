/****************************************************************************
 * Filename:    runepkg_util.c
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Essential utility functions for runepkg (runar linux)
 * LICENSE:     GPL v3
 * THIS IS FREE SOFTWARE; YOU CAN REDISTRIBUTE IT AND/OR MODIFY IT UNDER
 * THE TERMS OF THE GNU GENERAL PUBLIC LICENSE AS PUBLISHED BY THE FREE
 * SOFTWARE FOUNDATION; EITHER VERSION 3 OF THE LICENSE, OR (AT YOUR OPTION)
 * ANY LATER VERSION.
 * THIS PROGRAM IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. SEE THE
 * GNU GENERAL PUBLIC LICENSE FOR MORE DETAILS.
 ***************************************************************************/

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
#include <sys/ioctl.h>

// Define PATH_MAX if not defined
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// External global variable for verbose logging
extern bool g_verbose_mode;
// External global variable for debug logging
extern bool g_debug_mode;

// --- Logging Functions ---

void runepkg_util_log_verbose(const char *format, ...) {
    if (!g_verbose_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("[VERBOSE] ");
    vprintf(format, args);
    va_end(args);
}

/* `runepkg_log_verbose` is mapped to `runepkg_util_log_verbose` via header macro. */

void runepkg_util_log_debug(const char *format, ...) {
    if (!g_debug_mode) return;
    
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

void runepkg_util_security_blocked(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "SECURITY: Blocked ");
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

// --- Version Comparison ---

// Helper: Compare two version parts using Debian collation rules
static int compare_collation(const char *a, const char *b) {
    const char *pa = a, *pb = b;
    while (*pa || *pb) {
        // Extract next sequence from a
        int is_digit_a = isdigit(*pa);
        const char *start_a = pa;
        while (*pa && isdigit(*pa) == is_digit_a) pa++;
        size_t len_a = pa - start_a;

        // Extract next sequence from b
        int is_digit_b = isdigit(*pb);
        const char *start_b = pb;
        while (*pb && isdigit(*pb) == is_digit_b) pb++;
        size_t len_b = pb - start_b;

        if (is_digit_a && is_digit_b) {
            // Both digit sequences: compare numerically
            char buf_a[64], buf_b[64];
            if (len_a >= sizeof(buf_a) || len_b >= sizeof(buf_b)) return strcmp(a, b); // Fallback
            memcpy(buf_a, start_a, len_a); buf_a[len_a] = '\0';
            memcpy(buf_b, start_b, len_b); buf_b[len_b] = '\0';
            long num_a = strtol(buf_a, NULL, 10);
            long num_b = strtol(buf_b, NULL, 10);
            if (num_a < num_b) return -1;
            if (num_a > num_b) return 1;
        } else if (!is_digit_a && !is_digit_b) {
            // Both non-digit: compare lexicographically
            int cmp = strncmp(start_a, start_b, len_a < len_b ? len_a : len_b);
            if (cmp != 0) return cmp;
            if (len_a < len_b) return -1;
            if (len_a > len_b) return 1;
        } else {
            // One digit, one non-digit: the digit sequence is larger
            if (is_digit_a) return 1;
            else return -1;
        }
    }
    return 0;
}

// Parse version into epoch, upstream, revision
static void parse_version(const char *version, long *epoch, char *upstream, char *revision) {
    *epoch = 0;
    strcpy(upstream, version);
    revision[0] = '\0';

    char *colon = strrchr(upstream, ':');
    if (colon) {
        *colon = '\0';
        *epoch = strtol(upstream, NULL, 10);
        memmove(upstream, colon + 1, strlen(colon + 1) + 1);
    }

    char *dash = strrchr(upstream, '-');
    if (dash) {
        strcpy(revision, dash + 1);
        *dash = '\0';
    }
}

int runepkg_util_compare_versions(const char *v1, const char *v2) {
    if (!v1 || !v2) return v1 ? 1 : (v2 ? -1 : 0);
    if (strcmp(v1, v2) == 0) return 0;

    long epoch1, epoch2;
    char up1[256], up2[256], rev1[256], rev2[256];
    parse_version(v1, &epoch1, up1, rev1);
    parse_version(v2, &epoch2, up2, rev2);

    if (epoch1 < epoch2) return -1;
    if (epoch1 > epoch2) return 1;

    int cmp = compare_collation(up1, up2);
    if (cmp != 0) return cmp;

    return compare_collation(rev1, rev2);
}

// --- Dependency Parsing ---

int runepkg_util_check_version_constraint(const char *installed_version, const char *constraint) {
    if (!installed_version || !constraint) return -1;

    // Make a trimmed copy of the constraint to simplify parsing and logging
    char *cons = strdup(constraint);
    if (!cons) return -1;
    char *cons_trim = runepkg_util_trim_whitespace(cons);

    // Parse operator and version from the trimmed copy
    size_t op_len = strcspn(cons_trim, " 0123456789");
    if (op_len == 0 || op_len > 2) {
        free(cons);
        return -1;
    }

    char op[3] = {0};
    memcpy(op, cons_trim, op_len);
    op[op_len] = '\0';

    char ver[128] = {0};
    if (strlen(cons_trim + op_len) >= sizeof(ver)) {
        free(cons);
        return -1;
    }
    strcpy(ver, cons_trim + op_len);
    char *ver_trim = runepkg_util_trim_whitespace(ver);

    int cmp = runepkg_util_compare_versions(installed_version, ver_trim);

    int result = -1;
    if (strcmp(op, ">=") == 0) result = (cmp >= 0);
    else if (strcmp(op, "<=") == 0) result = (cmp <= 0);
    else if (strcmp(op, "==") == 0) result = (cmp == 0);
    else if (strcmp(op, "=") == 0) result = (cmp == 0);  // Debian uses = for exact match
    else if (strcmp(op, "!=") == 0) result = (cmp != 0);
    else if (strcmp(op, ">") == 0) result = (cmp > 0);
    else if (strcmp(op, "<") == 0) result = (cmp < 0);
    else if (strcmp(op, "<<") == 0) result = (cmp < 0);  // dpkg uses << for strict less
    else if (strcmp(op, ">>") == 0) result = (cmp > 0);  // dpkg uses >> for strict greater
    else result = -1;  // Unknown op

    runepkg_util_log_debug("check_version_constraint(installed='%s', constraint='%s') -> op='%s' ver='%s' cmp=%d result=%d\n",
        installed_version, cons_trim, op, ver_trim, cmp, result);

    free(cons);
    return result;
}

// --- Dependency Parsing ---

Dependency **parse_depends_with_constraints(const char *depends) {
    if (!depends || *depends == '\0') return NULL;

    // Count commas
    int count = 1;
    for (const char *p = depends; *p; p++) {
        if (*p == ',') count++;
    }

    Dependency **result = calloc(count + 1, sizeof(Dependency*));
    if (!result) return NULL;

    char *copy = strdup(depends);
    if (!copy) {
        free(result);
        return NULL;
    }

    char *token = strtok(copy, ",");
    int i = 0;
    while (token && i < count) {
        // Trim
        while (*token == ' ' || *token == '\t') token++;
        if (*token == '\0') {
            token = strtok(NULL, ",");
            continue;
        }

        result[i] = calloc(1, sizeof(Dependency));
        if (!result[i]) {
            // Free all
            for (int j = 0; j < i; j++) {
                free(result[j]->package);
                free(result[j]->constraint);
                free(result[j]);
            }
            free(result);
            free(copy);
            return NULL;
        }

        // Parse package and constraint
        char *paren = strchr(token, '(');
        if (paren) {
            *paren = '\0';
            char *close = strchr(paren + 1, ')');
            if (close) *close = '\0';
            result[i]->package = strdup(runepkg_util_trim_whitespace(token));
            // Extract constraint without parentheses
            char *inner = paren + 1;
            while (*inner && (*inner == ' ' || *inner == '(')) inner++;
            char *end = inner + strlen(inner) - 1;
            while (end > inner && (*end == ' ' || *end == ')')) {
                *end = '\0';
                end--;
            }
            result[i]->constraint = strdup(inner);
        } else {
            result[i]->package = strdup(runepkg_util_trim_whitespace(token));
            result[i]->constraint = NULL;
        }

        i++;
        token = strtok(NULL, ",");
    }

    free(copy);
    return result;
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
    /* entry log removed to reduce verbose noise */

    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        runepkg_util_log_debug("Failed to open config file '%s'. Error: %s\n", filepath, strerror(errno));
        return NULL;
    }

    char line[PATH_MAX * 2];
    char *value = NULL;
    size_t key_len = strlen(key);

    while (fgets(line, sizeof(line), file) != NULL) {
        char *trimmed_line = runepkg_util_trim_whitespace(line);
        if (strlen(trimmed_line) == 0 || trimmed_line[0] == '#') {
            continue;
        }

        if (strncmp(trimmed_line, key, key_len) != 0) {
            continue;
        }

        char *potential_separator = trimmed_line + key_len;
        while (*potential_separator != '\0' && isspace((unsigned char)*potential_separator)) {
            potential_separator++;
        }

        if (*potential_separator != separator) {
            continue;
        }

        char *start_of_value = potential_separator + 1;
        while (*start_of_value != '\0' && isspace((unsigned char)*start_of_value)) {
            start_of_value++;
        }

        char *raw_value = strdup(start_of_value);
        if (!raw_value) {
            /* memory failure; nothing to return */
            break;
        }

        char *trimmed_value = runepkg_util_trim_whitespace(raw_value);

        if (trimmed_value[0] == '~' && (trimmed_value[1] == '/' || trimmed_value[1] == '\0')) {
            char *home_dir = getenv("HOME");
            if (home_dir) {
                size_t home_len = strlen(home_dir);
                size_t value_len = strlen(trimmed_value);
                value = (char *)malloc(home_len + value_len + 1);
                if (value) {
                    snprintf(value, home_len + value_len + 1, "%s%s", home_dir, trimmed_value + 1);
                    free(raw_value);
                } else {
                    free(raw_value);
                    value = NULL;
                }
            } else {
                free(raw_value);
                value = NULL;
            }
        } else {
            value = raw_value;
        }
        break;
    }

    fclose(file);
    if (value) {
        runepkg_util_log_debug("Collected config '%s' = '%s' from '%s'\n", key, value, filepath);
    } else {
        runepkg_util_log_debug("No config value for '%s' in '%s'\n", key, filepath);
    }
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

char **parse_depends(const char *depends) {
    if (!depends || *depends == '\0') return NULL;

    // Count commas to estimate size
    int count = 1;
    for (const char *p = depends; *p; p++) {
        if (*p == ',') count++;
    }

    char **result = calloc(count + 1, sizeof(char*));
    if (!result) return NULL;

    char *copy = strdup(depends);
    if (!copy) {
        free(result);
        return NULL;
    }

    char *token = strtok(copy, ",");
    int i = 0;
    while (token && i < count) {
        // Trim leading whitespace
        while (*token == ' ' || *token == '\t') token++;
        // Find end: stop at space, tab, or '('
        char *end = token;
        while (*end && *end != ' ' && *end != '\t' && *end != '(') end++;
        *end = '\0';
        // If not empty, add
        if (*token) {
            result[i] = strdup(token);
            if (!result[i]) {
                // Free previous
                for (int j = 0; j < i; j++) free(result[j]);
                free(result);
                free(copy);
                return NULL;
            }
            i++;
        }
        token = strtok(NULL, ",");
    }

    free(copy);
    return result;
}

// --- File System Utilities ---

off_t runepkg_util_get_dir_size(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return 0;

    off_t total = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char fullpath[PATH_MAX];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(fullpath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                total += runepkg_util_get_dir_size(fullpath);
            } else {
                total += st.st_size;
            }
        }
    }
    closedir(dir);
    return total;
}

// --- String Formatting Utilities ---

char *runepkg_util_format_size(off_t size_bytes, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return NULL;

    double size;
    const char *unit;

    if (size_bytes >= 1024LL * 1024 * 1024) {
        size = (double)size_bytes / (1024 * 1024 * 1024);
        unit = "GB";
    } else if (size_bytes >= 1024 * 1024) {
        size = (double)size_bytes / (1024 * 1024);
        unit = "MB";
    } else if (size_bytes >= 1024) {
        size = (double)size_bytes / 1024;
        unit = "KB";
    } else {
        size = (double)size_bytes;
        unit = "B";
    }

    if (size_bytes >= 1024) {
        snprintf(buffer, buffer_size, "%.1f %s", size, unit);
    } else {
        snprintf(buffer, buffer_size, "%.0f %s", size, unit);
    }

    return buffer;
}

// --- Terminal Utilities ---

int runepkg_util_get_terminal_width(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        return w.ws_col;
    }
    return 80; // fallback
}

// --- Output Formatting Utilities ---

void runepkg_util_print_columns(const char *items[], int count) {
    if (!items || count <= 0) return;

    // Find maximum length
    size_t max_len = 0;
    for (int i = 0; i < count; i++) {
        if (items[i]) {
            size_t len = strlen(items[i]);
            if (len > max_len) max_len = len;
        }
    }

    int col_width = max_len + 2;
    int width = runepkg_util_get_terminal_width();
    int cols = width / col_width;
    if (cols < 1) cols = 1;
    int rows = (count + cols - 1) / cols;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = r * cols + c;
            if (idx < count && items[idx]) {
                printf("%-*s", col_width, items[idx]);
            }
        }
        printf("\n");
    }
}

// --- Package Suggestion Utilities ---

int runepkg_util_get_package_suggestions(const char *search_name, const char *db_dir, char suggestions[][PATH_MAX], int max_suggestions) {
    if (!search_name || !db_dir || !suggestions || max_suggestions <= 0) {
        return 0;
    }

    DIR *dir = opendir(db_dir);
    if (!dir) {
        return 0;
    }

    int suggestion_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && suggestion_count < max_suggestions) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            if (strstr(entry->d_name, search_name) != NULL) {
                strncpy(suggestions[suggestion_count], entry->d_name, PATH_MAX - 1);
                suggestions[suggestion_count][PATH_MAX - 1] = '\0';
                suggestion_count++;
            }
        }
    }
    closedir(dir);
    return suggestion_count;
}
