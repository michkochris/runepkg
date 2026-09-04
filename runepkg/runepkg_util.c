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

#include "runepkg_portable.h"
#include "runepkg_util.h"
#include "runepkg_config.h"
#include "runepkg_defensive.h"
#include "runepkg_cpp_ffi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>

/* Define PATH_MAX if not defined */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* External global variable for verbose logging */
extern bool g_verbose_mode;
/* External global variable for debug logging */
extern bool g_debug_mode;

/* --- Logging Functions --- */

void runepkg_util_log_verbose(const char *format, ...) {
    va_list args;
    if (!g_verbose_mode) return;
    
    va_start(args, format);
    printf("[VERBOSE] ");
    vprintf(format, args);
    va_end(args);
}

/* `runepkg_log_verbose` is mapped to `runepkg_util_log_verbose` via header macro. */

void runepkg_util_log_debug(const char *format, ...) {
    va_list args;
    if (!g_debug_mode) return;
    
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

/* --- Memory Management --- */

void runepkg_util_free_and_null(char **ptr) {
    if (ptr != NULL && *ptr != NULL) {
        free(*ptr);
        *ptr = NULL;
    }
}

int runepkg_util_get_elf_dependencies(const char *elf_path, const char *readelf_bin, char ***out_deps, int *out_count) {
    const char *bin;
    char cmd[PATH_MAX + 64];
    FILE *pipe;
    char line[512];

    if (!elf_path || !out_deps || !out_count) return -1;
    *out_deps = NULL;
    *out_count = 0;

    bin = readelf_bin ? readelf_bin : "readelf";
    snprintf(cmd, sizeof(cmd), "%s -d \"%s\"", bin, elf_path);

    pipe = popen(cmd, "r");
    if (!pipe) return -1;

    while (fgets(line, sizeof(line), pipe)) {
        if (strstr(line, "(NEEDED)")) {
            char *start = strchr(line, '[');
            char *end = strchr(line, ']');
            if (start && end && end > start) {
                char *libname;
                *end = '\0';
                libname = strdup(start + 1);
                *out_deps = (char **)realloc(*out_deps, sizeof(char *) * (*out_count + 1));
                (*out_deps)[(*out_count)++] = libname;
            }
        }
    }

    pclose(pipe);
    return 0;
}

char *runepkg_util_trim_whitespace(char *str) {
    char *end;
    if (str == NULL) return NULL;

    while (isspace((unsigned char)*str)) str++;

    if (*str == 0)
        return str;

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    *(end + 1) = 0;

    return str;
}

char *runepkg_util_safe_strncpy(char *dest, const char *src, size_t n) {
    size_t src_len;
    size_t copy_len;
    if (!dest || !src || n == 0) {
        return NULL;
    }
    src_len = strlen(src);
    copy_len = (src_len >= n) ? (n - 1) : src_len;
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
    return dest;
}

char *runepkg_util_concat_path(const char *dir, const char *file) {
    return runepkg_secure_path_concat(dir, file);
}

/* --- Version Comparison --- */

/* Debian character weights: ~ < (nothing) < letters < non-letters */
static int vercmp_weight(int c) {
    if (c == '~') return -1;
    if (c == 0) return 0;
    if (!isdigit(c) && !isalpha(c)) return c + 256;
    return c;
}

/* Helper: Compare two version parts using Debian collation rules */
static int compare_collation(const char *a, const char *b) {
    const char *pa = a, *pb = b;
    while (*pa || *pb) {
        /* Lexical part (up to first digit) */
        while ((*pa && !isdigit((unsigned char)*pa)) || (*pb && !isdigit((unsigned char)*pb))) {
            int wa = vercmp_weight((*pa && !isdigit((unsigned char)*pa)) ? *pa : 0);
            int wb = vercmp_weight((*pb && !isdigit((unsigned char)*pb)) ? *pb : 0);
            if (wa != wb) return (wa < wb) ? -1 : 1;
            if (*pa && !isdigit((unsigned char)*pa)) pa++;
            if (*pb && !isdigit((unsigned char)*pb)) pb++;
        }

        /* Numerical part */
        if (isdigit((unsigned char)*pa) || isdigit((unsigned char)*pb)) {
            size_t len_a, len_b;
            const char *start_a, *start_b;
            while (*pa == '0') pa++;
            while (*pb == '0') pb++;
            start_a = pa;
            while (isdigit((unsigned char)*pa)) pa++;
            start_b = pb;
            while (isdigit((unsigned char)*pb)) pb++;

            len_a = pa - start_a;
            len_b = pb - start_b;
            if (len_a < len_b) return -1;
            if (len_a > len_b) return 1;
            {
                int cmp = strncmp(start_a, start_b, len_a);
                if (cmp != 0) return cmp;
            }
        }
    }
    return 0;
}

/* Parse version into epoch, upstream, revision */
static void parse_version(const char *version, long *epoch, char *upstream, char *revision) {
    char *colon;
    char *dash;

    *epoch = 0;
    runepkg_secure_strcpy(upstream, 256, version);
    revision[0] = '\0';

    colon = strchr(upstream, ':');
    if (colon) {
        *colon = '\0';
        *epoch = strtol(upstream, NULL, 10);
        memmove(upstream, colon + 1, strlen(colon + 1) + 1);
    }

    dash = strrchr(upstream, '-');
    if (dash) {
        runepkg_secure_strcpy(revision, 256, dash + 1);
        *dash = '\0';
    }
}

int runepkg_util_compare_versions(const char *v1, const char *v2) {
    long epoch1, epoch2;
    char up1[256], up2[256], rev1[256], rev2[256];
    int cmp;

    if (!v1 || !v2) return v1 ? 1 : (v2 ? -1 : 0);
    if (strcmp(v1, v2) == 0) return 0;

    parse_version(v1, &epoch1, up1, rev1);
    parse_version(v2, &epoch2, up2, rev2);

    if (epoch1 < epoch2) return -1;
    if (epoch1 > epoch2) return 1;

    cmp = compare_collation(up1, up2);
    if (cmp != 0) return cmp;

    return compare_collation(rev1, rev2);
}

/* --- Dependency Parsing --- */

int runepkg_util_check_version_constraint(const char *installed_version, const char *constraint) {
    char *cons;
    char *cons_trim;
    size_t op_len;
    char op[3];
    char ver[128];
    char *ver_trim;
    int cmp;
    int result = -1;

    if (!installed_version || !constraint) return -1;

    /* Make a trimmed copy of the constraint to simplify parsing and logging */
    cons = strdup(constraint);
    if (!cons) return -1;
    cons_trim = runepkg_util_trim_whitespace(cons);

    /* Parse operator and version from the trimmed copy */
    op_len = strcspn(cons_trim, " 0123456789");
    if (op_len == 0 || op_len > 2) {
        free(cons);
        return -1;
    }

    memset(op, 0, sizeof(op));
    memcpy(op, cons_trim, op_len);
    op[op_len] = '\0';

    memset(ver, 0, sizeof(ver));
    if (strlen(cons_trim + op_len) >= sizeof(ver)) {
        free(cons);
        return -1;
    }
    runepkg_secure_strcpy(ver, sizeof(ver), cons_trim + op_len);
    ver_trim = runepkg_util_trim_whitespace(ver);

    cmp = runepkg_util_compare_versions(installed_version, ver_trim);

    if (strcmp(op, ">=") == 0) result = (cmp >= 0);
    else if (strcmp(op, "<=") == 0) result = (cmp <= 0);
    else if (strcmp(op, "==") == 0) result = (cmp == 0);
    else if (strcmp(op, "=") == 0) result = (cmp == 0);  /* Debian uses = for exact match */
    else if (strcmp(op, "!=") == 0) result = (cmp != 0);
    else if (strcmp(op, ">") == 0) result = (cmp > 0);
    else if (strcmp(op, "<") == 0) result = (cmp < 0);
    else if (strcmp(op, "<<") == 0) result = (cmp < 0);  /* dpkg uses << for strict less */
    else if (strcmp(op, ">>") == 0) result = (cmp > 0);  /* dpkg uses >> for strict greater */
    else result = -1;  /* Unknown op */

    runepkg_util_log_debug("check_version_constraint(installed='%s', constraint='%s') -> op='%s' ver='%s' cmp=%d result=%d\n",
        installed_version, cons_trim, op, ver_trim, cmp, result);

    free(cons);
    return result;
}

Dependency **parse_depends_with_constraints(const char *depends) {
    int count = 1;
    const char *p;
    Dependency **result;
    char *copy;
    char *token;
    int i = 0;

    if (!depends || *depends == '\0') return NULL;

    for (p = depends; *p; p++) {
        if (*p == ',') count++;
    }

    result = calloc(count + 1, sizeof(Dependency*));
    if (!result) return NULL;

    copy = strdup(depends);
    if (!copy) {
        free(result);
        return NULL;
    }

    token = strtok(copy, ",");
    while (token && i < count) {
        char *pipe;
        while (*token == ' ' || *token == '\t') token++;
        if (*token == '\0') {
            token = strtok(NULL, ",");
            continue;
        }

        pipe = strchr(token, '|');
        if (pipe) *pipe = '\0';

        result[i] = calloc(1, sizeof(Dependency));
        if (!result[i]) {
            int j;
            for (j = 0; j < i; j++) {
                free(result[j]->package);
                free(result[j]->constraint);
                free(result[j]);
            }
            free(result);
            free(copy);
            return NULL;
        }

        {
            char *paren = strchr(token, '(');
            if (paren) {
                char *pkg_part;
                char *extra;
                char *inner;
                char *end;
                char *close;

                *paren = '\0';
                close = strchr(paren + 1, ')');
                if (close) *close = '\0';

                pkg_part = token;
                extra = strpbrk(pkg_part, ":[<");
                if (extra) *extra = '\0';
                result[i]->package = strdup(runepkg_util_trim_whitespace(pkg_part));

                inner = paren + 1;
                while (*inner && (*inner == ' ' || *inner == '(')) inner++;
                end = inner + strlen(inner) - 1;
                while (end > inner && (*end == ' ' || *end == ')')) {
                    *end = '\0';
                    end--;
                }
                result[i]->constraint = strdup(inner);
            } else {
                char *pkg_part = token;
                char *extra = strpbrk(pkg_part, ":[<");
                if (extra) *extra = '\0';
                result[i]->package = strdup(runepkg_util_trim_whitespace(pkg_part));
                result[i]->constraint = NULL;
            }
        }

        i++;
        token = strtok(NULL, ",");
    }

    free(copy);
    return result;
}

int runepkg_util_is_path_under_dir(const char *path, const char *dir) {
    char *real_path;
    char *real_dir;
    size_t dir_len;
    int result;

    if (!path || !dir) return -1;

    real_path = realpath(path, NULL);
    real_dir = realpath(dir, NULL);

    if (!real_path || !real_dir) {
        free(real_path);
        free(real_dir);
        return 1;
    }

    dir_len = strlen(real_dir);
    result = (strncmp(real_path, real_dir, dir_len) == 0 &&
                  (real_path[dir_len] == '\0' || real_path[dir_len] == '/'));

    free(real_path);
    free(real_dir);
    return result;
}

/* --- File System Operations --- */

int runepkg_util_file_exists(const char *filepath) {
    return (access(filepath, F_OK) == 0);
}

int runepkg_util_is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return 0;
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
            if (mkdir(temp_path, mode) == -1) {
                if (errno != EEXIST) {
                    perror("Failed to create directory");
                    fprintf(stderr, "Directory: %s\n", temp_path);
                    ret = -1;
                    break;
                } else {
                    struct stat st;
                    if (stat(temp_path, &st) == 0) {
                        if (!S_ISDIR(st.st_mode)) {
                            fprintf(stderr, "\033[1;31m[error]\033[0m Path exists but is not a directory: %s\n", temp_path);
                            ret = -1;
                            break;
                        }
                    }
                }
            }
            runepkg_util_log_debug("Created directory: %s\n", temp_path);
            *p = '/';
        }
    }
    if (ret == 0 && mkdir(temp_path, mode) == -1) {
        if (errno != EEXIST) {
            perror("Failed to create final directory");
            fprintf(stderr, "Directory: %s\n", temp_path);
            ret = -1;
        } else {
            struct stat st;
            if (stat(temp_path, &st) == 0 && !S_ISDIR(st.st_mode)) {
                fprintf(stderr, "\033[1;31m[error]\033[0m Path exists but is not a directory: %s\n", temp_path);
                ret = -1;
            }
        }
    }

    free(temp_path);
    return ret;
}

char *runepkg_util_read_file_content(const char *filepath, size_t *len) {
    FILE *f = fopen(filepath, "rb");
    long file_size_long;
    size_t file_size;
    char *buffer;
    size_t bytes_read;

    if (!f) {
        if (len) *len = 0;
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    file_size_long = ftell(f);
    if (file_size_long == -1) {
        perror("ftell error");
        fclose(f);
        if (len) *len = 0;
        return NULL;
    }
    file_size = (size_t)file_size_long;
    fseek(f, 0, SEEK_SET);

    buffer = (char *)malloc(file_size + 1);
    if (!buffer) {
        runepkg_util_error("Memory allocation failed for file content\n");
        fclose(f);
        if (len) *len = 0;
        return NULL;
    }

    bytes_read = fread(buffer, 1, file_size, f);
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
    char buffer[65536];
    size_t bytes;
    int ret = 0;
    struct stat st;

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

    if (stat(source_path, &st) == 0) {
        if (chmod(destination_path, st.st_mode & 0777) == -1) {
            perror("Warning: Could not set permissions on copied file");
        }
    } else {
        perror("Warning: Could not get source file permissions for copy");
    }

    return ret;
}

int runepkg_util_copy_dir_recursive(const char *src_dir, const char *dst_dir) {
    DIR *dp;
    struct dirent *entry;
    struct stat st;

    if (!src_dir || !dst_dir) return -1;

    dp = opendir(src_dir);
    if (!dp) {
        runepkg_util_error("Could not open source directory for copying: %s\n", src_dir);
        return -1;
    }

    if (runepkg_util_create_dir_recursive(dst_dir, 0755) != 0) {
        closedir(dp);
        return -1;
    }

    while ((entry = readdir(dp)) != NULL) {
        char *src_path;
        char *dst_path;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        src_path = runepkg_util_concat_path(src_dir, entry->d_name);
        dst_path = runepkg_util_concat_path(dst_dir, entry->d_name);

        if (!src_path || !dst_path) {
            runepkg_util_free_and_null(&src_path);
            runepkg_util_free_and_null(&dst_path);
            closedir(dp);
            return -1;
        }

        if (lstat(src_path, &st) != 0) {
            runepkg_util_free_and_null(&src_path);
            runepkg_util_free_and_null(&dst_path);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (runepkg_util_copy_dir_recursive(src_path, dst_path) != 0) {
                runepkg_util_free_and_null(&src_path);
                runepkg_util_free_and_null(&dst_path);
                closedir(dp);
                return -1;
            }
        } else if (S_ISLNK(st.st_mode)) {
            char link_target[PATH_MAX];
            ssize_t len = readlink(src_path, link_target, sizeof(link_target) - 1);
            if (len != -1) {
                link_target[len] = '\0';
                unlink(dst_path);
                if (symlink(link_target, dst_path) != 0) {
                    /* ignore error if symlink creation failed */
                }
            }
        } else if (S_ISREG(st.st_mode)) {
            runepkg_util_copy_file(src_path, dst_path);
        }

        runepkg_util_free_and_null(&src_path);
        runepkg_util_free_and_null(&dst_path);
    }

    closedir(dp);
    return 0;
}

/* --- Configuration File Operations --- */

char *runepkg_util_expand_vars(const char *input) {
    char *result;
    const char *p = input;
    size_t res_size = 1024;
    size_t res_len = 0;

    if (!input) return NULL;

    result = (char *)runepkg_secure_malloc(res_size);
    if (!result) return NULL;

    while (*p) {
        if (*p == '$' && *(p + 1) == '{') {
            const char *end = strchr(p + 2, '}');
            if (end) {
                size_t var_len = (size_t)(end - (p + 2));
                char var_name[256];
                const char *val = NULL;
                char pwd_buf[PATH_MAX];

                if (var_len < sizeof(var_name)) {
                    memcpy(var_name, p + 2, var_len);
                    var_name[var_len] = '\0';

                    if (strcmp(var_name, "pwd") == 0) {
                        if (getcwd(pwd_buf, sizeof(pwd_buf))) {
                            val = pwd_buf;
                        }
                    } else {
                        val = getenv(var_name);
                    }

                    if (val) {
                        size_t val_len = strlen(val);
                        while (res_len + val_len + 1 >= res_size) {
                            void *temp;
                            res_size *= 2;
                            temp = runepkg_secure_realloc(result, res_size);
                            if (!temp) {
                                free(result);
                                return NULL;
                            }
                            result = (char *)temp;
                        }
                        memcpy(result + res_len, val, val_len);
                        res_len += val_len;
                        result[res_len] = '\0';
                        p = end + 1;
                        continue;
                    }
                }
            }
        }

        /* Literal character */
        if (res_len + 1 >= res_size) {
            void *temp;
            res_size *= 2;
            temp = runepkg_secure_realloc(result, res_size);
            if (!temp) {
                free(result);
                return NULL;
            }
            result = (char *)temp;
        }
        result[res_len++] = *p++;
        result[res_len] = '\0';
    }

    return result;
}

char *runepkg_util_get_config_value(const char *filepath, const char *key, char separator) {
    FILE *file = fopen(filepath, "r");
    char line[PATH_MAX * 2];
    char *value = NULL;
    size_t key_len = strlen(key);

    if (file == NULL) {
        runepkg_util_log_debug("Failed to open config file '%s'. Error: %s\n", filepath, strerror(errno));
        return NULL;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *trimmed_line = runepkg_util_trim_whitespace(line);
        char *potential_separator;
        char *start_of_value;
        char *raw_value;
        char *trimmed_value;

        if (strlen(trimmed_line) == 0 || trimmed_line[0] == '#') {
            continue;
        }

        if (strncmp(trimmed_line, key, key_len) != 0) {
            continue;
        }

        potential_separator = trimmed_line + key_len;
        while (*potential_separator != '\0' && isspace((unsigned char)*potential_separator)) {
            potential_separator++;
        }

        if (*potential_separator != separator) {
            continue;
        }

        start_of_value = potential_separator + 1;
        while (*start_of_value != '\0' && isspace((unsigned char)*start_of_value)) {
            start_of_value++;
        }

        raw_value = strdup(start_of_value);
        if (!raw_value) {
            break;
        }

        trimmed_value = runepkg_util_trim_whitespace(raw_value);

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

        /* Perform variable expansion (${VAR}, ${pwd}) on the value */
        if (value) {
            char *expanded = runepkg_util_expand_vars(value);
            if (expanded) {
                free(value);
                value = expanded;
            }
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

bool runepkg_util_parse_yes_no(const char *s, bool default_val) {
    char buf[32];
    size_t j = 0;
    size_t i;

    if (!s || s[0] == '\0')
        return default_val;

    for (i = 0; s[i] && j + 1 < sizeof(buf); i++)
        buf[j++] = (char)tolower((unsigned char)s[i]);
    buf[j] = '\0';

    if (strcmp(buf, "yes") == 0 || strcmp(buf, "true") == 0 || strcmp(buf, "1") == 0 || strcmp(buf, "on") == 0)
        return true;
    if (strcmp(buf, "no") == 0 || strcmp(buf, "false") == 0 || strcmp(buf, "0") == 0 || strcmp(buf, "off") == 0)
        return false;
    return default_val;
}

bool runepkg_util_confirm(const char *prompt) {
    char resp[16];
    if (g_auto_confirm_deps) {
        printf("%s [y/N] \033[1;33my (auto)\033[0m\n", prompt);
        return true;
    }
    printf("%s [y/N] ", prompt);
    fflush(stdout);
    if (fgets(resp, sizeof(resp), stdin) && (resp[0] == 'y' || resp[0] == 'Y')) {
        return true;
    }
    return false;
}

/* --- Command Execution --- */

float runepkg_util_get_load_factor(void) {
    double load1 = 0.0;
    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    FILE *fp;
    double cpu_factor;
    unsigned long mem_total = 0, mem_available = 0;
    double mem_factor = 0.0;

    if (num_cores < 1) num_cores = 1;

    /* 1. CPU Load Average */
    fp = fopen("/proc/loadavg", "r");
    if (fp) {
        if (fscanf(fp, "%lf", &load1) != 1) load1 = 0.0;
        fclose(fp);
    }
    cpu_factor = load1 / (double)num_cores;

    /* 2. Memory Pressure */
    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "MemTotal:", 9) == 0) sscanf(line + 9, "%lu", &mem_total);
            else if (strncmp(line, "MemAvailable:", 13) == 0) sscanf(line + 13, "%lu", &mem_available);
            if (mem_total > 0 && mem_available > 0) break;
        }
        fclose(fp);
        if (mem_total > 0) mem_factor = (double)(mem_total - mem_available) / (double)mem_total;
    }

    /* Memory Penalty: if >90% used, escalate load factor to trigger throttling */
    if (mem_factor > 0.90) {
        double memory_penalty = (mem_factor - 0.90) * 5.0;
        if (memory_penalty > cpu_factor) return (float)(memory_penalty > 1.0 ? 1.0 : memory_penalty);
    }

    return (float)cpu_factor;
}

static const char* ELDER_FUTHARK[] = {
    "ᚠ", "ᚢ", "ᚦ", "ᚨ", "ᚱ", "ᚲ", "ᚷ", "ᚹ",
    "ᚺ", "ᚾ", "ᛁ", "ᛃ", "ᛇ", "ᛈ", "ᛉ", "ᛊ",
    "ᛏ", "ᛒ", "ᛖ", "ᛗ", "ᛚ", "ᛜ", "ᛞ", "ᛟ"
};

static void render_telemetry_bar(const char *command_path, float load_factor, int elapsed_sec) {
    int bar_width = 20;
    float fraction = load_factor;
    int pos, i;
    const char *gauge_color;
    char cmd_name[21];
    const char *base;

    if (!isatty(STDOUT_FILENO)) return;

    if (fraction > 1.0f) fraction = 1.0f;
    if (fraction < 0.0f) fraction = 0.0f;

    pos = (int)(bar_width * fraction);
    gauge_color = (load_factor > 0.85f) ? "\033[1;31m" : "\033[1;32m";

    base = strrchr(command_path, '/');
    if (base) base++;
    else base = command_path;

    strncpy(cmd_name, base, 20);
    cmd_name[20] = '\0';

    /* Exact runepkg style: "  -> name [runes] status" */
    printf("\r\033[K  \033[1;34m->\033[0m \033[1;36m%-20s\033[0m [%s", cmd_name, gauge_color);
    for (i = 0; i < bar_width; i++) {
        if (i < pos) printf("%s", ELDER_FUTHARK[i % 24]);
        else printf("·");
    }
    printf("\033[0m] %02d:%02d | L: %.2f", elapsed_sec / 60, elapsed_sec % 60, load_factor);
    fflush(stdout);
}

static void prepare_execution_environment(void) {
    if (g_active_profile && !g_bootstrap_mode) {
        unsetenv("CPATH");
        unsetenv("LIBRARY_PATH");
        unsetenv("LD_LIBRARY_PATH");
        unsetenv("C_INCLUDE_PATH");
        unsetenv("CPLUS_INCLUDE_PATH");

        if (g_active_profile->cc) setenv("CC", g_active_profile->cc, 1);
        if (g_active_profile->cxx) setenv("CXX", g_active_profile->cxx, 1);
        if (g_active_profile->ld) setenv("LD", g_active_profile->ld, 1);
        if (g_active_profile->ar) setenv("AR", g_active_profile->ar, 1);
        if (g_active_profile->strip) setenv("STRIP", g_active_profile->strip, 1);
        if (g_active_profile->readelf) setenv("READELF", g_active_profile->readelf, 1);

        if (g_active_profile->pkg_config_sysroot_dir)
            setenv("PKG_CONFIG_SYSROOT_DIR", g_active_profile->pkg_config_sysroot_dir, 1);
        if (g_active_profile->pkg_config_libdir)
            setenv("PKG_CONFIG_LIBDIR", g_active_profile->pkg_config_libdir, 1);

        setenv("PKG_CONFIG_PATH", "", 1);
    }
}

int runepkg_util_execute_command(const char *command_path, char *const argv[]) {
    float max_load = 0.85f;
    pid_t pid;

    /* Load Factor Governor */
    while (1) {
        float current_load = runepkg_util_get_load_factor();
        if (current_load < 0 || current_load < max_load) break;

        runepkg_util_log_verbose("System load too high (%.2f > %.2f). Throttling...\n", current_load, max_load);
        sleep(5);
    }

    /* Cross-compilation environment sanitization */
    prepare_execution_environment();

    runepkg_util_log_debug("Executing command: %s\n", command_path);
    pid = fork();

    if (pid == -1) {
        perror("Failed to fork process");
        return -1;
    } else if (pid == 0) {
#ifdef ENABLE_CPP_FFI
        if (runepkg_security_is_root()) {
            runepkg_security_drop_privileges_for_worker("_apt");
        }
#endif
        execvp(argv[0], argv);
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

int runepkg_util_execute_command_to_file(const char *command_path, char *const argv[], const char *log_path) {
    pid_t pid;
    float max_load = 0.85f;
    time_t start_time;
    int status;

    /* Initial Load Check */
    while (1) {
        float current_load = runepkg_util_get_load_factor();
        if (current_load < 0 || current_load < max_load) break;
        runepkg_util_log_verbose("System load too high (%.2f > %.2f). Throttling...\n", current_load, max_load);
        sleep(5);
    }

    /* Cross-compilation environment sanitization */
    prepare_execution_environment();

    start_time = time(NULL);
    pid = fork();

    if (pid == -1) {
        perror("Failed to fork process");
        return -1;
    } else if (pid == 0) {
        int fd;
#ifdef ENABLE_CPP_FFI
        if (runepkg_security_is_root()) {
            runepkg_security_drop_privileges_for_worker("_apt");
        }
#endif
        fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd != -1) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        execvp(argv[0], argv);
        perror("Failed to execute command");
        _exit(1);
    } else {
        /* Monitor Loop with Load Factor Governor (SIGSTOP/SIGCONT throttling) */
        int is_paused = 0;
        while (waitpid(pid, &status, WNOHANG) == 0) {
            float current_load = runepkg_util_get_load_factor();
            int elapsed = (int)(time(NULL) - start_time);

            if (current_load > 0.85f && !is_paused) {
                kill(pid, SIGSTOP);
                is_paused = 1;
            } else if (current_load < 0.70f && is_paused) {
                kill(pid, SIGCONT);
                is_paused = 0;
            }

            render_telemetry_bar(command_path, current_load, elapsed);
            if (is_paused) {
                printf(" \033[1;33m[THROTTLED]\033[0m");
                fflush(stdout);
            }

            usleep(500000);
        }

        /* Ensure child is resumed before finishing if it was left paused */
        if (is_paused) kill(pid, SIGCONT);

        /* Final status print (clear line) */
        printf("\r\033[K");
        fflush(stdout);

        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return -1;
    }
    return -1;
}

int runepkg_util_execute_command_telemetry(const char *command_path, char *const argv[], const char *log_path) {
    return runepkg_util_execute_command_to_file(command_path, argv, log_path);
}

int runepkg_util_execute_command_silent(const char *command_path, char *const argv[]) {
    pid_t pid;
    runepkg_util_log_debug("Executing silent command: %s\n", command_path);
    pid = fork();

    if (pid == -1) {
        perror("Failed to fork process");
        return -1;
    } else if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execvp(argv[0], argv);
        _exit(1);
    } else {
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("Failed to wait for child process");
            return -1;
        }
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
    }
    return -1;
}

/* --- .deb Package Operations --- */

static int extract_deb_archive(const char *deb_path, const char *destination_dir) {
    char *absolute_deb_path;
    char current_dir[PATH_MAX];
    char *ar_path;
    char *argv_ar[4];
    int result;

    runepkg_util_log_verbose("Extracting .deb file '%s' to '%s'...\n", deb_path, destination_dir);

    if (runepkg_util_create_dir_recursive(destination_dir, 0777) != 0) {
        runepkg_util_error("Failed to create destination directory for .deb extraction.\n");
        return -1;
    }
    chmod(destination_dir, 0777);
#ifdef ENABLE_CPP_FFI
    if (runepkg_security_is_root()) {
        struct passwd *pw = getpwnam("_apt");
        if (!pw) pw = getpwnam("nobody");
        if (pw) {
            if (chown(destination_dir, pw->pw_uid, pw->pw_gid) != 0) {
                /* Ignored */
            }
        }
    }
#endif

    absolute_deb_path = realpath(deb_path, NULL);
    if (!absolute_deb_path) {
        perror("Failed to resolve absolute path for .deb file");
        runepkg_util_error("Could not resolve absolute path for '%s'.\n", deb_path);
        return -1;
    }

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

    ar_path = "/usr/bin/ar";
    argv_ar[0] = "ar";
    argv_ar[1] = "-x";
    argv_ar[2] = absolute_deb_path;
    argv_ar[3] = NULL;

    result = runepkg_util_execute_command(ar_path, argv_ar);

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
    char *archive_name;
    char *tar_path;
    char *argv_tar[8];
    int result;
#ifdef ENABLE_CPP_FFI
    char sanitized_dest[PATH_MAX];
#endif

    if (!archive_path || !destination_dir) {
        runepkg_util_error("extract_tar_archive: NULL archive_path or destination_dir.\n");
        return -1;
    }

#ifdef ENABLE_CPP_FFI
    if (!runepkg_security_sanitize_path(destination_dir, ".", sanitized_dest, sizeof(sanitized_dest))) {
        runepkg_util_security_blocked("Target extraction directory fails security path sanitization: %s\n", destination_dir);
        return -1;
    }
#endif

    archive_name = basename((char*)archive_path);
    runepkg_util_log_verbose("Extracting tar archive '%s' to '%s'...\n", archive_name, destination_dir);

    if (!runepkg_util_file_exists(archive_path)) {
        runepkg_util_error("Tar archive file not found: %s\n", archive_path);
        return -1;
    }

    if (runepkg_util_create_dir_recursive(destination_dir, 0777) != 0) {
        runepkg_util_error("Failed to create destination directory for tar extraction.\n");
        return -1;
    }
    chmod(destination_dir, 0777);
#ifdef ENABLE_CPP_FFI
    if (runepkg_security_is_root()) {
        struct passwd *pw = getpwnam("_apt");
        if (!pw) pw = getpwnam("nobody");
        if (pw) {
            if (chown(destination_dir, pw->pw_uid, pw->pw_gid) != 0) {
                /* Ignored */
            }
        }
    }
#endif

    tar_path = "/usr/bin/tar";

    argv_tar[0] = "tar";
    argv_tar[1] = "--force-local";
    argv_tar[2] = "-xf";
    argv_tar[3] = (char *)archive_path;
    argv_tar[4] = "-C";
    argv_tar[5] = (char *)destination_dir;
    argv_tar[6] = NULL;

    result = runepkg_util_execute_command(tar_path, argv_tar);

    if (result != 0) {
        runepkg_util_error("Failed to execute 'tar' for archive extraction.\n");
        return -1;
    }

    runepkg_util_log_verbose("Tar archive extracted successfully.\n");
    return 0;
}

int runepkg_util_extract_deb_complete(const char *deb_path, const char *extract_dir) {
    char *temp_dir;
    char *control_archive_path = NULL;
    char *data_archive_path = NULL;
    char *control_extract_dir;
    char *data_extract_dir;
#ifdef ENABLE_CPP_FFI
    char sanitized_extract[PATH_MAX];
#endif

    if (!deb_path || !extract_dir) {
        runepkg_util_error("extract_deb_complete: NULL deb_path or extract_dir.\n");
        return -1;
    }

#ifdef ENABLE_CPP_FFI
    if (!runepkg_security_sanitize_path(extract_dir, ".", sanitized_extract, sizeof(sanitized_extract))) {
        runepkg_util_security_blocked("Top-level extraction directory fails security path sanitization: %s\n", extract_dir);
        return -1;
    }
    runepkg_security_apply_rlimits((size_t)2048 * 1024 * 1024, 1024);
#endif

    runepkg_util_log_verbose("Starting complete .deb extraction of '%s' to '%s'\n", deb_path, extract_dir);

    runepkg_util_create_dir_recursive(extract_dir, 0777);
    chmod(extract_dir, 0777);
#ifdef ENABLE_CPP_FFI
    if (runepkg_security_is_root()) {
        struct passwd *pw = getpwnam("_apt");
        if (!pw) pw = getpwnam("nobody");
        if (pw) {
            if (chown(extract_dir, pw->pw_uid, pw->pw_gid) != 0) {
                /* Ignored */
            }
        }
    }
#endif

    if (!runepkg_util_file_exists(deb_path)) {
        runepkg_util_error(".deb file not found: %s\n", deb_path);
        return -1;
    }

    temp_dir = runepkg_util_concat_path(extract_dir, "temp_deb_extract");
    if (!temp_dir) {
        runepkg_util_error("Failed to create temporary directory path.\n");
        return -1;
    }

    if (extract_deb_archive(deb_path, temp_dir) != 0) {
        runepkg_util_error("Failed to extract .deb archive.\n");
        runepkg_util_free_and_null(&temp_dir);
        return -1;
    }

    if (find_tar_archives(temp_dir, &control_archive_path, &data_archive_path) != 0) {
        runepkg_util_error("Failed to find tar archives in .deb extraction.\n");
        runepkg_util_free_and_null(&temp_dir);
        return -1;
    }

    control_extract_dir = runepkg_util_concat_path(extract_dir, "control");
    data_extract_dir = runepkg_util_concat_path(extract_dir, "data");
    
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

int runepkg_util_create_deb(const char *source_dir, const char *output_deb) {
    char *abs_source;
    char *control_dir;
    char *data_dir;
    char cwd[PATH_MAX];
    char *deb_bin_path;
    FILE *f;
    char *control_tar;
    char *data_tar;
    char *abs_output;

    if (!source_dir || !output_deb) {
        runepkg_util_error("create_deb: NULL source_dir or output_deb.\n");
        return -1;
    }

    abs_source = realpath(source_dir, NULL);
    if (!abs_source) {
        perror("realpath source_dir");
        return -1;
    }

    runepkg_util_log_verbose("Building .deb package: %s from %s\n", output_deb, abs_source);

    control_dir = runepkg_util_concat_path(abs_source, "control");
    data_dir = runepkg_util_concat_path(abs_source, "data");

    if (!runepkg_util_file_exists(control_dir)) {
        runepkg_util_error("Control directory not found: %s\n", control_dir);
        free(control_dir); free(data_dir); free(abs_source);
        return -1;
    }
    if (!runepkg_util_file_exists(data_dir)) {
        runepkg_util_error("Data directory not found: %s\n", data_dir);
        free(control_dir); free(data_dir); free(abs_source);
        return -1;
    }

    if (!getcwd(cwd, sizeof(cwd))) {
        perror("getcwd");
        free(control_dir); free(data_dir); free(abs_source);
        return -1;
    }

    /* 1. Create debian-binary */
    deb_bin_path = runepkg_util_concat_path(abs_source, "debian-binary");
    f = fopen(deb_bin_path, "w");
    if (!f) {
        perror("fopen debian-binary");
        free(control_dir); free(data_dir); free(deb_bin_path); free(abs_source);
        return -1;
    }
    fprintf(f, "2.0\n");
    fclose(f);

    /* 2. Create control.tar.gz */
    control_tar = runepkg_util_concat_path(abs_source, "control.tar.gz");
    if (chdir(control_dir) != 0) {
        perror("chdir control");
        free(control_dir); free(data_dir); free(deb_bin_path); free(control_tar); free(abs_source);
        return -1;
    }
    {
        char *argv_control[7];
        argv_control[0] = (char*)"tar";
        argv_control[1] = (char*)"--force-local";
        argv_control[2] = (char*)"-czf";
        argv_control[3] = control_tar;
        argv_control[4] = (char*)".";
        argv_control[5] = NULL;

        if (runepkg_util_execute_command("/usr/bin/tar", argv_control) != 0) {
            runepkg_util_error("Failed to create control.tar.gz\n");
            if (chdir(cwd) != 0) perror("chdir rollback failed");
            free(control_dir); free(data_dir); free(deb_bin_path); free(control_tar); free(abs_source);
            return -1;
        }
    }

    /* 3. Create data.tar.xz */
    data_tar = runepkg_util_concat_path(abs_source, "data.tar.xz");
    if (chdir(data_dir) != 0) {
        perror("chdir data");
        if (chdir(cwd) != 0) perror("chdir rollback failed");
        free(control_dir); free(data_dir); free(deb_bin_path); free(control_tar); free(data_tar); free(abs_source);
        return -1;
    }
    {
        char *argv_data[7];
        argv_data[0] = (char*)"tar";
        argv_data[1] = (char*)"--force-local";
        argv_data[2] = (char*)"-cJf";
        argv_data[3] = data_tar;
        argv_data[4] = (char*)".";
        argv_data[5] = NULL;

        if (runepkg_util_execute_command("/usr/bin/tar", argv_data) != 0) {
            runepkg_util_error("Failed to create data.tar.xz\n");
            if (chdir(cwd) != 0) perror("chdir rollback failed");
            free(control_dir); free(data_dir); free(deb_bin_path); free(control_tar); free(data_tar); free(abs_source);
            return -1;
        }
    }

    /* 4. Assemble with ar */
    if (chdir(abs_source) != 0) {
        perror("chdir abs_source");
        if (chdir(cwd) != 0) perror("chdir rollback failed");
        free(control_dir); free(data_dir); free(deb_bin_path); free(control_tar); free(data_tar); free(abs_source);
        return -1;
    }
    /* Use absolute path for output if it doesn't start with / */
    abs_output = output_deb[0] == '/' ? strdup(output_deb) : runepkg_util_concat_path(cwd, output_deb);

    {
        char *argv_ar[8];
        argv_ar[0] = (char*)"ar";
        argv_ar[1] = (char*)"-rc";
        argv_ar[2] = abs_output;
        argv_ar[3] = (char*)"debian-binary";
        argv_ar[4] = (char*)"control.tar.gz";
        argv_ar[5] = (char*)"data.tar.xz";
        argv_ar[6] = NULL;

        if (runepkg_util_execute_command("/usr/bin/ar", argv_ar) != 0) {
            runepkg_util_error("Failed to assemble .deb with ar\n");
            if (chdir(cwd) != 0) perror("chdir rollback failed");
            free(control_dir); free(data_dir); free(deb_bin_path); free(control_tar); free(data_tar); free(abs_output); free(abs_source);
            return -1;
        }
    }

    if (chdir(cwd) != 0) perror("chdir rollback failed");
    runepkg_util_log_verbose(".deb package built successfully: %s\n", abs_output);

    free(control_dir); free(data_dir); free(deb_bin_path); free(control_tar); free(data_tar); free(abs_output); free(abs_source);
    return 0;
}

int runepkg_util_init_fhs(const char *root) {
    const char *dirs[16];
    int num_dirs = 16;
    int i;
    char *etc_dir;

    if (!root) return -1;

    /* Standard LFS-style directories */
    dirs[0] = "bin"; dirs[1] = "sbin"; dirs[2] = "etc"; dirs[3] = "lib";
    dirs[4] = "lib64"; dirs[5] = "usr/bin"; dirs[6] = "usr/sbin"; dirs[7] = "usr/lib";
    dirs[8] = "usr/local/bin"; dirs[9] = "var/lib/dpkg"; dirs[10] = "var/log";
    dirs[11] = "tmp"; dirs[12] = "root"; dirs[13] = "proc"; dirs[14] = "sys"; dirs[15] = "dev";

    runepkg_util_log_verbose("Initializing FHS skeleton in: %s\n", root);

    for (i = 0; i < num_dirs; i++) {
        char *path = runepkg_util_concat_path(root, dirs[i]);
        if (path) {
            if (!runepkg_util_file_exists(path)) {
                runepkg_util_create_dir_recursive(path, 0755);
            }
            free(path);
        }
    }

    /* Create basic /etc/passwd if it doesn't exist */
    etc_dir = runepkg_util_concat_path(root, "etc");
    if (etc_dir) {
        char *passwd = runepkg_util_concat_path(etc_dir, "passwd");
        if (passwd && !runepkg_util_file_exists(passwd)) {
            FILE *f = fopen(passwd, "w");
            if (f) {
                fprintf(f, "root:x:0:0:root:/root:/bin/sh\n");
                fclose(f);
            }
        }
        free(passwd);
        free(etc_dir);
    }

    return 0;
}

char **parse_depends(const char *depends) {
    int count = 1;
    const char *p;
    char **result;
    char *copy;
    char *token;
    int i = 0;

    if (!depends || *depends == '\0') return NULL;

    for (p = depends; *p; p++) {
        if (*p == ',') count++;
    }

    result = calloc(count + 1, sizeof(char*));
    if (!result) return NULL;

    copy = strdup(depends);
    if (!copy) {
        free(result);
        return NULL;
    }

    token = strtok(copy, ",");
    while (token && i < count) {
        char *pipe;
        char *end;
        char *extra;
        char *clean_name;

        while (*token == ' ' || *token == '\t') token++;

        pipe = strchr(token, '|');
        if (pipe) *pipe = '\0';

        end = token;
        while (*end && *end != ' ' && *end != '\t' && *end != '(') end++;
        *end = '\0';

        extra = strpbrk(token, ":[<");
        if (extra) *extra = '\0';
        clean_name = runepkg_util_trim_whitespace(token);

        if (*clean_name) {
            result[i] = strdup(clean_name);
            if (!result[i]) {
                int j;
                for (j = 0; j < i; j++) free(result[j]);
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

/* --- File System Utilities --- */

off_t runepkg_util_get_dir_size(const char *path) {
    DIR *dir = opendir(path);
    off_t total = 0;
    struct dirent *entry;

    if (!dir) return 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        {
            char fullpath[PATH_MAX];
            struct stat st;
            snprintf(fullpath, sizeof(fullpath), "%.*s/%s", (int)(sizeof(fullpath)-258), path, entry->d_name);

            if (stat(fullpath, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    total += runepkg_util_get_dir_size(fullpath);
                } else {
                    total += st.st_size;
                }
            }
        }
    }
    closedir(dir);
    return total;
}

/* --- String Formatting Utilities --- */

char *runepkg_util_format_size(off_t size_bytes, char *buffer, size_t buffer_size) {
    double size;
    const char *unit;

    if (!buffer || buffer_size == 0) return NULL;

    if (size_bytes >= (off_t)1024 * 1024 * 1024) {
        size = (double)size_bytes / (1024.0 * 1024.0 * 1024.0);
        unit = "GB";
    } else if (size_bytes >= (off_t)1024 * 1024) {
        size = (double)size_bytes / (1024.0 * 1024.0);
        unit = "MB";
    } else if (size_bytes >= (off_t)1024) {
        size = (double)size_bytes / 1024.0;
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

/* --- Terminal Utilities --- */

int runepkg_util_get_terminal_width(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        return w.ws_col;
    }
    return 80; /* fallback */
}

/* --- Output Formatting Utilities --- */

void runepkg_util_print_columns(const char *items[], int count, const char *prefix) {
    size_t max_len = 0;
    int col_width;
    int width;
    int cols;
    int rows;
    int r;
    int i;

    if (!items || count <= 0) return;

    for (i = 0; i < count; i++) {
        if (items[i]) {
            size_t len = strlen(items[i]);
            if (len > max_len) max_len = len;
        }
    }

    col_width = (int)max_len + 2;
    width = runepkg_util_get_terminal_width();

    if (prefix) {
        int prefix_len = (int)strlen(prefix);
        if (prefix_len < width) width -= prefix_len;
        else width = 1;
    }

    cols = width / col_width;
    if (cols < 1) cols = 1;
    rows = (count + cols - 1) / cols;

    for (r = 0; r < rows; r++) {
        int c;
        if (prefix) printf("%s", prefix);
        for (c = 0; c < cols; c++) {
            int idx = r * cols + c;
            if (idx < count && items[idx]) {
                printf("%-*s", col_width, items[idx]);
            }
        }
        printf("\n");
    }
}

/* --- MOTD --- */

void runepkg_util_motd(void) {
    printf("  \033[90m[#####]\033[0m  \033[1;37mrunepkg\033[0m\n");
    printf("  \033[90m[#\033[1;36m\\ /\033[90m#]\033[0m  \033[37mversion 1.0.4\033[0m\n");
    printf("  \033[90m[# \033[1;36mV \033[90m#]\033[0m\n");
    printf("  \033[90m[#####]\033[0m  \033[37mGPL-V3\033[0m\n\n");
}

int runepkg_util_get_package_suggestions(const char *search_name, const char *db_dir, char suggestions[][PATH_MAX], int max_suggestions) {
    DIR *dir;
    int suggestion_count = 0;
    struct dirent *entry;

    if (!search_name || !db_dir || !suggestions || max_suggestions <= 0) {
        return 0;
    }

    dir = opendir(db_dir);
    if (!dir) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL && suggestion_count < max_suggestions) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            if (strstr(entry->d_name, search_name) != NULL) {
                runepkg_secure_strcpy(suggestions[suggestion_count], PATH_MAX, entry->d_name);
                suggestion_count++;
            }
        }
    }
    closedir(dir);
    return suggestion_count;
}

const char* runepkg_util_find_version_separator(const char *s) {
    const char *p;
    const char *last_match = NULL;
    if (!s) return NULL;
    /* We look for the LAST hyphen followed by a digit.
     * Heuristic: A version separator is a hyphen followed by a digit,
     * and that version segment usually contains a dot, colon, tilde, or plus.
     * If there's no such character before the next hyphen or end of string,
     * it's likely part of the name (e.g., 'gcc-14'). */
    for (p = s; *p; p++) {
        if (*p == '-' && isdigit((unsigned char)p[1])) {
            const char *v = p + 1;
            bool has_ver_char = false;
            while (*v && *v != '-') {
                if (*v == '.' || *v == ':' || *v == '~' || *v == '+') {
                    has_ver_char = true;
                    break;
                }
                v++;
            }
            if (has_ver_char) {
                last_match = p;
            }
        }
    }
    return last_match;
}

const char *runepkg_util_resolve_build_target(const char *arg, const char *build_dir) {
    static char resolved_path[PATH_MAX];
    struct stat st;

    if (!arg) return NULL;

    /* 1. Direct path check (handles '.', './', '/absolute/path', '../') */
    if (stat(arg, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (realpath(arg, resolved_path) != NULL) {
            return resolved_path;
        }
    }

    /* 2. Fallback: Lookup package directory inside configured build_dir */
    if (build_dir) {
        snprintf(resolved_path, sizeof(resolved_path), "%s/%s", build_dir, arg);
        if (stat(resolved_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            return resolved_path;
        }
    }

    return NULL; /* Not found */
}

/* --- FSM Transaction Logging Implementation --- */

static FILE *g_tx_log_fp = NULL;
static char g_tx_log_path[PATH_MAX] = {0};

int runepkg_log_init(const char *log_dir, const char *timestamp)
{
    char ts_buf[64];

    if (!log_dir || log_dir[0] == '\0') {
        log_dir = g_log_dir ? g_log_dir : "/var/lib/runepkg_dir/log";
    }

    if (runepkg_util_create_dir_recursive(log_dir, 0755) != 0) {
        return -1;
    }

    if (!timestamp || timestamp[0] == '\0') {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        strftime(ts_buf, sizeof(ts_buf), "%Y%m%d-%H%M%S", t);
        timestamp = ts_buf;
    }

    runepkg_secure_snprintf(g_tx_log_path, sizeof(g_tx_log_path),
        "%s/transaction-%s.log", log_dir, timestamp);

    g_tx_log_fp = fopen(g_tx_log_path, "w");
    if (!g_tx_log_fp) {
        return -1;
    }

    fprintf(g_tx_log_fp, "[INIT] Transaction log initialized at %s\n", timestamp);
    fflush(g_tx_log_fp);
    return 0;
}

void runepkg_log_write(const char *level, const char *fmt, ...)
{
    va_list args;
    time_t now;
    struct tm *t;
    char time_str[32];

    if (!g_tx_log_fp) return;

    now = time(NULL);
    t = localtime(&now);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", t);

    fprintf(g_tx_log_fp, "[%s] [%s] ", time_str, level ? level : "INFO");

    va_start(args, fmt);
    vfprintf(g_tx_log_fp, fmt, args);
    va_end(args);

    fprintf(g_tx_log_fp, "\n");
    fflush(g_tx_log_fp);
}

void runepkg_log_fail(const char *err_msg, const char *log_dir)
{
    char fail_path[PATH_MAX];
    FILE *fail_fp;
    time_t now;
    struct tm *t;
    char time_str[64];

    if (g_tx_log_fp) {
        runepkg_log_write("FATAL", "Transaction abort: %s", err_msg ? err_msg : "Unknown error");
    }

    if (!log_dir || log_dir[0] == '\0') {
        log_dir = g_log_dir ? g_log_dir : "/var/lib/runepkg_dir/log";
    }

    runepkg_util_create_dir_recursive(log_dir, 0755);
    runepkg_secure_snprintf(fail_path, sizeof(fail_path), "%s/transaction_failure.log", log_dir);

    fail_fp = fopen(fail_path, "a");
    if (fail_fp) {
        now = time(NULL);
        t = localtime(&now);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

        fprintf(fail_fp, "[%s] TRANSACTION FAILURE: %s (Detailed log: %s)\n",
            time_str, err_msg ? err_msg : "Unknown failure",
            g_tx_log_path[0] != '\0' ? g_tx_log_path : "N/A");
        fclose(fail_fp);
    }
}

void runepkg_log_close(int clean_up_enabled)
{
    if (g_tx_log_fp) {
        fclose(g_tx_log_fp);
        g_tx_log_fp = NULL;
    }

    if (clean_up_enabled && g_tx_log_path[0] != '\0') {
        unlink(g_tx_log_path);
        g_tx_log_path[0] = '\0';
    }
}
