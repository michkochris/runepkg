/******************************************************************************
 *  Filename:    upkglib.c
 *  Author:      <michkochris@gmail.com>
 *  Date:        started 12-31-2024
 *  Description: upkg manages linux .deb pkg's
 *
 *  Copyright (c) 2025 upkg (ulinux) All rights reserved.
 *  GPLV3
 *  This program is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation, either version 3 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program. If not, see <https://www.gnu.org/licenses/>.
 ******************************************************************************/
/*file description: file with misc functions to help upkg*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>

#include "upkglib.h"
#include "upkghash.h"
#include "upkgstruct.h"
#include "upkgconfig.h"

void badmsg(char *text) {
printf(YELLOW "==> " WHITE "%s\n" RESET, text);
}
void errormsg(char *text) {
printf(RED "error: " WHITE "%s\n" RESET, text);
}
void goodmsg(char *text) {
printf(GREEN "==> " WHITE "%s\n" RESET, text);
}
void usermsg(char *text) {
printf(CYAN NAME ": " WHITE "%s\n" RESET, text);
}
void success(char *text) {
printf(MAGENTA "==> " WHITE "%s\n" RESET, text);
}
void testmsg() {
badmsg("hello error!");
errormsg("hello error!");
goodmsg("hello error!");
usermsg("hello error!");
success("hello error!");
}
void usage_info() {
printf("Usage: " NAME " [options] input_file.deb's \n");
printf("(Options:)\n");
printf("  -c  --config   Display config file information\n");
printf("  -u  --usage    Display usage information\n");
printf("  -h  --help     Display help message\n");
printf("  -l  --list     List all pkg's downward\n");
printf("  -g  --glob     List all pkg's in glob form\n");
printf("  -v  --version  Print program name and version\n");
printf("  --license      Display license message\n");
printf("  example-pkg_1.30_arch.deb  Install multiple .deb pkg's\n");
printf("\n");
printf("Developer (options)\n");
printf("  -t  --testhash Run the testhash() function in upkghash.c\n");
printf("  -pht  Print entire pkg list with hash index number\n");
printf("  -print_hash_table  \n");
}
void help_msg() {
printf("\n\n");
printf("Report bugs directly to: michkochris@gmail.com\n");
printf("Or file a bug report on github... \n");
printf("upkg (ulinux) home page: <https://www.ulinux.com>\n");
printf("upkg github page <https://github.com/michkochris/upkg>\n");
printf("ulinux github page <https://github.com/michkochris/ulinux>\n");
printf("General help using upkg and ulinux: <facebook.group>\n");
}
void version_info() {
printf("\nupkg (ulinux) 1.0\n");
}
void license_info() {
printf("\n");
printf("Copyright (C) 2025 upkg (ulinux) Christoper Michko\n");
printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>\n");
printf("This is free software: you are free to change and redistribute it.\n");
printf("There is NO WARRANTY, to the extent permitted by law.\n");
}
int create_dir(const char *path) {
    if (!path) {
        fprintf(stderr, "create_dir: NULL path\n");
        return -1;
    }
    struct stat st;
    if (stat(path, &st) == -1) {
        if (errno == ENOENT) {
            if (mkdir(path, 0755) == -1) {
                perror("mkdir failed");
                return -1;
            }
        } else {
            perror("stat failed");
            return -1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "%s exists, but is not a directory\n", path);
        return -1;
    }
    return 0;
}

void sanitize_path(char *path) {
    if (!path) return;
    for (char *p = path; *p; ++p) {
        if (!isalnum(*p) && *p != '/' && *p != '_' && *p != '-' && *p != '.' && *p != '+') {
            *p = '_';
        }
    }
}

int remove_dir(const char *destruct_dir) {
    if (!destruct_dir) {
        fprintf(stderr, "remove_dir: NULL path\n");
        return -1;
    }
    char safe_path[256];
    strncpy(safe_path, destruct_dir, sizeof(safe_path) - 1);
    safe_path[sizeof(safe_path) - 1] = '\0';
    sanitize_path(safe_path);
    if (rmdir(safe_path) == 0) {
        return 0;
    }
    char command[256];
    snprintf(command, sizeof(command), "rm -rf %s", safe_path);
    return system(command);
}

char *concat_path(const char *dir, const char *filename) {
    if (!dir || !filename) {
        fprintf(stderr, "concat_path: NULL argument\n");
        return NULL;
    }
    size_t dir_len = strlen(dir);
    size_t filename_len = strlen(filename);
    size_t total_len = dir_len + filename_len + 2;
    char* result = (char*)malloc(total_len);
    if (result == NULL) {
        perror("Memory allocation failed");
        return NULL;
    }
    strcpy(result, dir);
    strcat(result, filename);
    return result;
}

void extract_deb(const char *deb_file, const char *dest_dir) {
    if (!deb_file || !dest_dir) {
        fprintf(stderr, "extract_deb: NULL argument\n");
        exit(1);
    }
    if (remove_dir("upkgdir/staging") != 0) {
        printf("Failed to clean staging directory!\n");
        exit(1);
    }
    struct stat st = {0};
    if (stat(dest_dir, &st) == -1) {
        if (mkdir(dest_dir, 0755) == -1) {
            printf("Failed to create staging directory!");
            exit(1);
        }
    }
    char command[128];
    snprintf(command, sizeof(command), "ar -x --output %s %s", dest_dir, deb_file);
    int result = system(command);
    if (result != 0) {
        fprintf(stderr, "Error unpacking %s!\n", deb_file);
        exit(1);
    }
}

void extract_tar_xz(const char *tarxz,const char *tdest) {
    if (!tarxz || !tdest) {
        fprintf(stderr, "extract_tar_xz: NULL argument\n");
        exit(1);
    }
    char command[256];
    snprintf(command, sizeof(command), "tar -xf %s -C %s", tarxz, tdest);
    int result = system(command);
    if (result != 0) {
        fprintf(stderr, "Error extracting %s!\n", tarxz);
        exit(1);
    }
}

void extract_data_tar_xz(const char *data_tar_xz, const char *unpack_dir) {
    if (!data_tar_xz || !unpack_dir) {
        fprintf(stderr, "extract_data_tar_xz: NULL argument\n");
        return;
    }
    char command[256];
    snprintf(command, sizeof(command), "tar -xf %s -C %s", data_tar_xz, unpack_dir);
    int result = system(command);
    if (result != 0) {
        fprintf(stderr, "Error extracting %s!\n", data_tar_xz);
        exit(1);
    }
}

char *search_file(const char *control, const char *str_str) {
    if (!control || !str_str) {
        fprintf(stderr, "search_file: NULL argument\n");
        return NULL;
    }
    FILE* file = fopen(control, "r");
    if (file == NULL) {
        printf("Error opening file: %s!\n", control);
        return NULL;
    }
    char* line = NULL;
    size_t len = 0;
    ssize_t read;
    while ((read = getline(&line, &len, file)) != -1) {
        if (strstr(line, str_str) != NULL) {
            fclose(file);
            return line;
        }
    }
    fclose(file);
    if (line) free(line);
    return NULL;
}

char *searchandreadtoend(const char* filename, const char* searchString) {
    if (!filename || !searchString) {
        fprintf(stderr, "searchandreadtoend: NULL argument\n");
        return NULL;
    }
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        printf("Error opening file %s!\n", filename);
        return NULL;
    }
    char* result = NULL;
    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (found) {
            size_t new_len = (result ? strlen(result) : 0) + strlen(line) + 1;
            char *tmp = realloc(result, new_len);
            if (!tmp) {
                free(result);
                fclose(file);
                return NULL;
            }
            result = tmp;
            strcat(result, line);
        } else if (strstr(line, searchString) != NULL) {
            found = 1;
            result = malloc(strlen(line) + 1);
            if (!result) {
                fclose(file);
                return NULL;
            }
            strcpy(result, line);
        }
    }
    fclose(file);
    return result;
}

void rmstr(char *str, const char *sub) {
    if (!str || !sub || !*sub) return;
    size_t len_sub = strlen(sub);
    size_t len_str = strlen(str);
    if (len_sub == 0 || len_str < len_sub) return;

    char *read = str, *write = str;
    while (*read) {
        if (strncmp(read, sub, len_sub) == 0) {
            read += len_sub;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

void remove_white(char *str) {
    if (!str) return;
    int i, j = 0;
    for (i = 0; str[i]; i++) {
        if (str[i] != ' ' && str[i] != '\t' && str[i] != '\n') {
            str[j++] = str[i];
        }
    }
    str[j] = '\0';
}

static void collect_files_recursive(const char *dir, char ***list, int *count, int *capacity) {
    DIR *dp = opendir(dir);
    if (!dp) return;
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            collect_files_recursive(path, list, count, capacity);
        } else {
            if (*count >= *capacity) {
                *capacity *= 2;
                char **tmp = realloc(*list, (*capacity) * sizeof(char *));
                if (!tmp) break;
                *list = tmp;
            }
            (*list)[(*count)++] = strdup(path);
        }
    }
    closedir(dp);
}

char **collect_file_list(const char *unpack_dir, int *file_count) {
    int capacity = 128;
    int count = 0;
    char **list = malloc(capacity * sizeof(char *));
    if (!list) return NULL;
    collect_files_recursive(unpack_dir, &list, &count, &capacity);
    *file_count = count;
    return list;
}

void print_pkg_file_list(const char *pkgname) {
    Pkginfo *info = search((char *)pkgname);
    if (!info) {
        printf("Package not found: %s\n", pkgname);
        return;
    }
    if (!info->file_list || info->file_count == 0) {
        printf("No file list available for package: %s\n", pkgname);
        return;
    }
    printf("File list for package %s:\n", pkgname);
    for (int i = 0; i < info->file_count; i++) {
        printf("%s\n", info->file_list[i]);
    }
}

void print_pkg_file_list_glob_veiw(const char *pkgname) {
    Pkginfo *info = search((char *)pkgname);
    if (!info) {
        printf("Package not found: %s\n", pkgname);
        return;
    }
    if (!info->file_list || info->file_count == 0) {
        printf("No file list available for package: %s\n", pkgname);
        return;
    }
    printf("Glob view (full path) file list for package %s:\n", pkgname);
    for (int i = 0; i < info->file_count; i++) {
        printf("%s ", info->file_list[i]);
    }
}
// end of file
