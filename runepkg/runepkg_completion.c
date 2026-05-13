#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <glob.h>
#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <errno.h>
#include <ctype.h>

#include "runepkg_completion.h"
#include "runepkg_handle.h"
#include "runepkg_storage.h"
#include "runepkg_config.h"
#include "runepkg_util.h"

int is_completion_trigger(char *argv[]) {
    (void)argv; /* suppressed unused warning; argc check is done by caller */
    return 1;
}

/* Recursively scan directories starting at `base` and print any .deb
 * files whose relative path matches the `partial` prefix. This replaces
 * the previous hard-coded search of "." and "debs".
 */
static void scan_deb_recursive(const char *base, const char *partial, int depth) {
    if (depth > 64) return;
    DIR *dir = opendir(base);
    if (!dir) return;

    struct dirent *entry;
    char path[PATH_MAX];
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        snprintf(path, sizeof(path), "%s/%s", base, entry->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_deb_recursive(path, partial, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            size_t len = strlen(entry->d_name);
            if (len > 4 && strcmp(entry->d_name + len - 4, ".deb") == 0) {
                /* Normalize relative path for printing */
                char rel[PATH_MAX];
                if (strncmp(path, "./", 2) == 0) snprintf(rel, sizeof(rel), "%s", path + 2);
                else if (strncmp(path, ".\\", 2) == 0) snprintf(rel, sizeof(rel), "%s", path + 2);
                else snprintf(rel, sizeof(rel), "%s", path);
                if (strncmp(rel, partial, strlen(partial)) == 0) {
                    printf("%s\n", rel);
                }
            }
        }
    }

    closedir(dir);
}

void complete_deb_files(const char *partial) {
    scan_deb_recursive(".", partial, 0);
}

/* Complete generic file paths. Only complete actual filesystem names;
 * callers should decide whether to call this (i.e., don't call when the
 * user has started typing '-' and expects option completions).
 */
void complete_file_paths(const char *partial) {
    const char *prefix = partial ? partial : "";

    /* If a directory component exists, change search dir */
    char dirbuf[PATH_MAX];
    char namebuf[PATH_MAX];
    const char *last_slash = strrchr(prefix, '/');
    const char *search_dir = ".";
    const char *match_prefix = prefix;
    if (last_slash) {
        size_t dirlen = last_slash - prefix;
        if (dirlen >= sizeof(dirbuf)) dirlen = sizeof(dirbuf)-1;
        memcpy(dirbuf, prefix, dirlen);
        dirbuf[dirlen] = '\0';
        search_dir = dirbuf[0] ? dirbuf : ".";
        match_prefix = last_slash + 1;
    }

    DIR *d = opendir(search_dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (strncmp(e->d_name, match_prefix, strlen(match_prefix)) != 0) continue;

        /* Build output path relative to current working dir to match other
         * completion outputs. Preserve provided directory prefix if present. */
        if (last_slash) {
            size_t n = sizeof(namebuf);
            namebuf[0] = '\0';
            size_t dirlen = strlen(search_dir);
            size_t copy1 = (dirlen < n - 1) ? dirlen : n - 1;
            memcpy(namebuf, search_dir, copy1);
            size_t pos = copy1;
            if (pos < n - 1) {
                if (pos == 0 || namebuf[pos - 1] != '/') {
                    namebuf[pos++] = '/';
                }
            }
            size_t namelen = strlen(e->d_name);
            size_t copy2 = 0;
            if (pos < n - 1) copy2 = (namelen < (n - pos - 1)) ? namelen : (n - pos - 1);
            if (copy2 > 0) memcpy(namebuf + pos, e->d_name, copy2);
            namebuf[pos + copy2] = '\0';
        } else {
            size_t n = sizeof(namebuf);
            size_t namelen = strlen(e->d_name);
            size_t copy = (namelen < n - 1) ? namelen : n - 1;
            memcpy(namebuf, e->d_name, copy);
            namebuf[copy] = '\0';
        }

        /* Append '/' for directories to help shells know it's a directory */
        struct stat st;
        if (stat(namebuf, &st) == 0 && S_ISDIR(st.st_mode)) {
            printf("%s/\n", namebuf);
        } else {
            printf("%s\n", namebuf);
        }
    }
    closedir(d);
}

/* Search the binary autocomplete index for prefix matches and print them. */
int prefix_search_and_print(const char *prefix) {
    char index_path[PATH_MAX];
    if (!g_runepkg_db_dir) return 0;
    snprintf(index_path, sizeof(index_path), "%s/runepkg_autocomplete.bin", g_runepkg_db_dir);

    struct stat index_st, db_dir_st, build_dir_st;
    int index_exists = (stat(index_path, &index_st) == 0);
    int db_dir_stat = stat(g_runepkg_db_dir, &db_dir_st);
    int build_dir_stat = g_build_dir ? stat(g_build_dir, &build_dir_st) : -1;

    bool needs_rebuild = false;
    if (!index_exists) {
        needs_rebuild = true;
    } else {
        if (db_dir_stat == 0 && db_dir_st.st_mtime > index_st.st_mtime) {
            needs_rebuild = true;
        }
        if (build_dir_stat == 0 && build_dir_st.st_mtime > index_st.st_mtime) {
            needs_rebuild = true;
        }
    }

    if (needs_rebuild) {
        runepkg_storage_build_autocomplete_index();
    }

    int fd = open(index_path, O_RDONLY);
    if (fd < 0) return 0;

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return 0; }

    void *mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) { close(fd); return 0; }

    AutocompleteHeader *hdr = (AutocompleteHeader *)mapped;
    if (hdr->magic != 0x52554E45) { munmap(mapped, st.st_size); close(fd); return 0; }

    uint32_t *offsets = (uint32_t *)((char *)mapped + sizeof(AutocompleteHeader));
    char *names = (char *)mapped + sizeof(AutocompleteHeader) + hdr->entry_count * sizeof(uint32_t);

    int low = 0, high = hdr->entry_count - 1;
    int first_match = -1;
    while (low <= high) {
        int mid = low + (high - low) / 2;
        char *current_name = names + offsets[mid];
        int cmp = strcmp(prefix, current_name);
        if (cmp == 0) {
            first_match = mid; high = mid - 1;
        } else if (cmp < 0) {
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }

    if (first_match == -1 && low < (int)hdr->entry_count) {
        char *name = names + offsets[low];
        if (strncmp(prefix, name, strlen(prefix)) == 0) first_match = low;
    }

    if (first_match != -1) {
        for (int i = first_match; i < (int)hdr->entry_count; i++) {
            char *name = names + offsets[i];
            if (strncmp(prefix, name, strlen(prefix)) != 0) break;

            // If it's in build_dir, print the absolute path
            if (g_build_dir) {
                char *full = runepkg_util_concat_path(g_build_dir, name);
                if (full) {
                    struct stat st;
                    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
                        printf("%s\n", full);
                        free(full);
                        continue;
                    }
                    free(full);
                }
            }
            printf("%s\n", name);
        }
    }

    munmap(mapped, st.st_size);
    close(fd);
    return (first_match != -1) ? 1 : 0;
}

struct RepoIndexEntry {
    char name[64];
    uint32_t file_id;
    uint32_t offset;
};

static int repo_generic_prefix_search(const char *prefix, const char *index_filename) {
    char index_path[PATH_MAX];
    if (!g_runepkg_db_dir) return 0;
    snprintf(index_path, sizeof(index_path), "%s/%s", g_runepkg_db_dir, index_filename);

    int fd = open(index_path, O_RDONLY);
    if (fd < 0) return 0;

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return 0; }
    if (st.st_size < (off_t)sizeof(uint32_t)) { close(fd); return 0; }

    void *mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) { close(fd); return 0; }

    uint32_t count = *(uint32_t *)mapped;
    struct RepoIndexEntry *entries = (struct RepoIndexEntry *)((char *)mapped + sizeof(uint32_t));

    int low = 0, high = (int)count - 1;
    int first_match = -1;
    size_t prefix_len = strlen(prefix);

    while (low <= high) {
        int mid = low + (high - low) / 2;
        int cmp = strncmp(prefix, entries[mid].name, prefix_len);
        if (cmp == 0) {
            first_match = mid;
            high = mid - 1;
        } else if (cmp < 0) {
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }

    if (first_match != -1) {
        char last_printed[64] = {0};
        for (int i = first_match; i < (int)count; i++) {
            if (strncmp(prefix, entries[i].name, prefix_len) != 0) break;
            if (strcmp(last_printed, entries[i].name) != 0) {
                printf("%s\n", entries[i].name);
                strncpy(last_printed, entries[i].name, sizeof(last_printed)-1);
            }
        }
    }

    munmap(mapped, st.st_size);
    close(fd);
    return (first_match != -1) ? 1 : 0;
}

int repo_prefix_search_and_print(const char *prefix) {
    return repo_generic_prefix_search(prefix, "repo_index.bin");
}

int repo_src_prefix_search_and_print(const char *prefix) {
    return repo_generic_prefix_search(prefix, "repo_src_index.bin");
}

void handle_binary_completion(const char *partial, const char *prev) {
    const char *comp_line = getenv("COMP_LINE");
    const char *comp_point_s = getenv("COMP_POINT");
    int comp_point = 0;
    if (comp_point_s) comp_point = atoi(comp_point_s);

    char inferred_cmd[64] = {0};
    if (comp_line) {
        size_t len = strlen(comp_line);
        size_t use_len = len;
        if (comp_point > 0 && (size_t)comp_point < len) use_len = (size_t)comp_point;

        char *buf = malloc(use_len + 1);
        if (buf) {
            memcpy(buf, comp_line, use_len);
            buf[use_len] = '\0';

            char *saveptr = NULL;
            char *tok = strtok_r(buf, " \t", &saveptr);
            /* skip program name */
            if (tok) tok = strtok_r(NULL, " \t", &saveptr);
            const char *last_token = NULL;
            while (tok) {
                if (strcmp(tok, "install") == 0 || strcmp(tok, "-i") == 0 || strcmp(tok, "--install") == 0) {
                    strncpy(inferred_cmd, "install", sizeof(inferred_cmd)-1);
                } else if (strcmp(tok, "remove") == 0 || strcmp(tok, "-r") == 0 || strcmp(tok, "--remove") == 0) {
                    strncpy(inferred_cmd, "remove", sizeof(inferred_cmd)-1);
                } else if (strcmp(tok, "list") == 0 || strcmp(tok, "-l") == 0 || strcmp(tok, "-L") == 0 || strcmp(tok, "--list") == 0) {
                    strncpy(inferred_cmd, "list", sizeof(inferred_cmd)-1);
                } else if (strcmp(tok, "status") == 0 || strcmp(tok, "-s") == 0 || strcmp(tok, "--status") == 0) {
                    strncpy(inferred_cmd, "status", sizeof(inferred_cmd)-1);
                } else if (strcmp(tok, "-u") == 0 || strcmp(tok, "--unpack") == 0) {
                    strncpy(inferred_cmd, "unpack", sizeof(inferred_cmd)-1);
                } else if (strcmp(tok, "-b") == 0 || strcmp(tok, "--build") == 0) {
                    strncpy(inferred_cmd, "build", sizeof(inferred_cmd)-1);
                } else if (strcmp(tok, "-m") == 0 || strcmp(tok, "--md5check") == 0) {
                    strncpy(inferred_cmd, "md5check", sizeof(inferred_cmd)-1);
                } else if (strcmp(tok, "source") == 0) {
                    strncpy(inferred_cmd, "source", sizeof(inferred_cmd)-1);
                }
                last_token = tok;
                tok = strtok_r(NULL, " \t", &saveptr);
            }
            free(buf);

            /* If nothing inferred and last token is a flag, scan full line for hints */
            if (inferred_cmd[0] == '\0' && last_token && last_token[0] == '-') {
                char *full = strdup(comp_line);
                if (full) {
                    char *save2 = NULL;
                    char *t2 = strtok_r(full, " \t", &save2);
                    if (t2) t2 = strtok_r(NULL, " \t", &save2);
                    while (t2) {
                        if (strcmp(t2, "install") == 0 || strcmp(t2, "-i") == 0 || strcmp(t2, "--install") == 0) {
                            strncpy(inferred_cmd, "install", sizeof(inferred_cmd)-1);
                            break;
                        } else if (strcmp(t2, "remove") == 0 || strcmp(t2, "-r") == 0 || strcmp(t2, "--remove") == 0) {
                            strncpy(inferred_cmd, "remove", sizeof(inferred_cmd)-1);
                            break;
                        } else if (strcmp(t2, "list") == 0 || strcmp(t2, "-l") == 0 || strcmp(t2, "-L") == 0 || strcmp(t2, "--list") == 0) {
                            strncpy(inferred_cmd, "list", sizeof(inferred_cmd)-1);
                            break;
                        } else if (strcmp(t2, "status") == 0 || strcmp(t2, "-s") == 0 || strcmp(t2, "--status") == 0) {
                            strncpy(inferred_cmd, "status", sizeof(inferred_cmd)-1);
                            break;
                        } else if (strcmp(t2, "-u") == 0 || strcmp(t2, "--unpack") == 0) {
                            strncpy(inferred_cmd, "unpack", sizeof(inferred_cmd)-1);
                            break;
                        } else if (strcmp(t2, "-b") == 0 || strcmp(t2, "--build") == 0) {
                            strncpy(inferred_cmd, "build", sizeof(inferred_cmd)-1);
                            break;
                        } else if (strcmp(t2, "-m") == 0 || strcmp(t2, "--md5check") == 0) {
                            strncpy(inferred_cmd, "md5check", sizeof(inferred_cmd)-1);
                            break;
                        } else if (strcmp(t2, "source") == 0) {
                            strncpy(inferred_cmd, "source", sizeof(inferred_cmd)-1);
                            break;
                        }
                        t2 = strtok_r(NULL, " \t", &save2);
                    }
                    free(full);
                }
            }
        }
    }

    /* If we have an inferred command prefer it */
    if (inferred_cmd[0] != '\0') {
        if (strcmp(inferred_cmd, "install") == 0) {
            if (partial[0] == '-') {
                if (strncmp(partial, "--", 2) == 0) {
                    const char *all_long_opts[] = {
                        "--install","--remove","--list","--status","--list-files","--search",
                        "--unpack","--build","--md5check",
                        "--verbose","--force","--help","--version",
                        "--print-config","--print-config-file","--print-pkglist-file","--print-auto-pkgs"
                    };
                    int n = sizeof(all_long_opts)/sizeof(all_long_opts[0]);
                    for (int i=0;i<n;i++) if (strncmp(all_long_opts[i], partial, strlen(partial))==0) printf("%s\n", all_long_opts[i]);
                } else {
                    const char *all_short_opts[] = {"-i","-r","-l","-L","-s","-S","-u","-b","-m","-v","-f","-h"};
                    int n = sizeof(all_short_opts)/sizeof(all_short_opts[0]);
                    for (int i=0;i<n;i++) if (strncmp(all_short_opts[i], partial, strlen(partial))==0) printf("%s\n", all_short_opts[i]);
                }
            } else {
                complete_deb_files(partial);
                repo_prefix_search_and_print(partial);
            }
            return;
        }
        if (strcmp(inferred_cmd, "remove") == 0) {
            if (partial[0] == '-') {
                if (strncmp(partial, "--", 2) == 0) {
                    const char *all_long_opts[] = {
                        "--install","--remove","--list","--status","--list-files","--search",
                        "--unpack","--build","--md5check",
                        "--verbose","--force","--help","--version",
                        "--print-config","--print-config-file","--print-pkglist-file","--print-auto-pkgs"
                    };
                    int n = sizeof(all_long_opts)/sizeof(all_long_opts[0]);
                    for (int i=0;i<n;i++) if (strncmp(all_long_opts[i], partial, strlen(partial))==0) printf("%s\n", all_long_opts[i]);
                } else {
                    const char *all_short_opts[] = {"-i","-r","-l","-L","-s","-S","-u","-b","-m","-v","-f","-h"};
                    int n = sizeof(all_short_opts)/sizeof(all_short_opts[0]);
                    for (int i=0;i<n;i++) if (strncmp(all_short_opts[i], partial, strlen(partial))==0) printf("%s\n", all_short_opts[i]);
                }
            } else {
                prefix_search_and_print(partial);
            }
            return;
        }
        if (strcmp(inferred_cmd, "list") == 0) {
            prefix_search_and_print(partial);
            return;
        }
        if (strcmp(inferred_cmd, "status") == 0) {
            if (partial && partial[0] == '-') {
                const char *all_short_opts[] = {"-i","-r","-l","-L","-s","-S","-u","-b","-m","-v","-f","-h"};
                int n = sizeof(all_short_opts)/sizeof(all_short_opts[0]);
                for (int i=0;i<n;i++) if (strncmp(all_short_opts[i], partial, strlen(partial))==0) printf("%s\n", all_short_opts[i]);
                if (strncmp(partial, "--", 2) == 0) {
                    const char *all_long_opts[] = {
                        "--install","--remove","--list","--status","--list-files","--search",
                        "--unpack","--build","--md5check",
                        "--verbose","--force","--help","--version",
                        "--print-config","--print-config-file","--print-pkglist-file","--print-auto-pkgs"
                    };
                    int m = sizeof(all_long_opts)/sizeof(all_long_opts[0]);
                    for (int j = 0; j < m; j++) if (strncmp(all_long_opts[j], partial, strlen(partial))==0) printf("%s\n", all_long_opts[j]);
                }
                return;
            }
            if (!prefix_search_and_print(partial)) {
                repo_prefix_search_and_print(partial);
            }
            return;
        }
        if (strcmp(inferred_cmd, "unpack") == 0) {
            complete_deb_files(partial);
            return;
        }
        if (strcmp(inferred_cmd, "build") == 0) {
            if (!prefix_search_and_print(partial)) {
                complete_file_paths(partial);
            }
            return;
        }
        if (strcmp(inferred_cmd, "md5check") == 0) {
            prefix_search_and_print(partial);
            return;
        }
        if (strcmp(inferred_cmd, "source") == 0) {
            repo_src_prefix_search_and_print(partial);
            return;
        }
    }

    if (strcmp(prev, "runepkg") == 0) {
        if (partial[0] == '-') {
            if (strncmp(partial, "--", 2) == 0) {
                const char *long_opts[] = {
                    "--install", "--remove", "--list", "--status", "--list-files", "--search",
                    "--unpack", "--build", "--md5check",
                    "--verbose", "--force", "--version", "--help",
                    "--print-config", "--print-config-file", "--print-pkglist-file", "--print-auto-pkgs"
                };
                int num_long = sizeof(long_opts) / sizeof(long_opts[0]);
                for (int i = 0; i < num_long; i++) {
                    if (strncmp(long_opts[i], partial, strlen(partial)) == 0) printf("%s\n", long_opts[i]);
                }
            } else {
                const char *short_opts[] = {"-i", "-r", "-l", "-s", "-L", "-S", "-u", "-b", "-m", "-v", "-f", "-h"};
                int num_short = sizeof(short_opts) / sizeof(short_opts[0]);
                for (int i = 0; i < num_short; i++) {
                    if (strncmp(short_opts[i], partial, strlen(partial)) == 0) printf("%s\n", short_opts[i]);
                }
            }
        } else {
            const char *sub_cmds[] = {
                "install", "remove", "list", "status", "list-files", "search",
                "download-only", "depends", "verify", "update", "upgrade", "source"
            };
            int num_sub = sizeof(sub_cmds) / sizeof(sub_cmds[0]);
            for (int i = 0; i < num_sub; i++) {
                if (strncmp(sub_cmds[i], partial, strlen(partial)) == 0) printf("%s\n", sub_cmds[i]);
            }
        }
    } else if (partial[0] == '-') {
        if (inferred_cmd[0] != '\0') {
            if (strcmp(inferred_cmd, "install") == 0) {
                const char *short_opts[] = {"-f", "-v"};
                int n = sizeof(short_opts)/sizeof(short_opts[0]);
                for (int i = 0; i < n; i++) if (strncmp(short_opts[i], partial, strlen(partial))==0) printf("%s\n", short_opts[i]);
                if (strncmp(partial, "--", 2) == 0) {
                    const char *long_opts[] = {"--force", "--verbose", "--print-config", "--print-config-file", "--print-pkglist-file", "--print-auto-pkgs"};
                    int m = sizeof(long_opts)/sizeof(long_opts[0]);
                    for (int i = 0; i < m; i++) if (strncmp(long_opts[i], partial, strlen(partial))==0) printf("%s\n", long_opts[i]);
                }
            } else {
                printf("--help\n--version\n--verbose\n--force\n");
            }
        } else {
            printf("--help\n--version\n--verbose\n--force\n");
        }
        if (prev && prev[0] == '-') {
            complete_deb_files(partial);
            prefix_search_and_print(partial);
        }
    } else if (strcmp(prev, "install") == 0 || strcmp(prev, "-i") == 0) {
        complete_deb_files(partial);
        repo_prefix_search_and_print(partial);
    } else if (strcmp(prev, "remove") == 0 || strcmp(prev, "-r") == 0) {
        prefix_search_and_print(partial);
    } else if (strcmp(prev, "list") == 0 || strcmp(prev, "-l") == 0 || strcmp(prev, "-L") == 0) {
        prefix_search_and_print(partial);
    } else if (strcmp(prev, "status") == 0 || strcmp(prev, "-s") == 0) {
        if (!prefix_search_and_print(partial)) {
            repo_prefix_search_and_print(partial);
        }
    } else if (strcmp(prev, "source") == 0) {
        repo_src_prefix_search_and_print(partial);
    }
}

void handle_print_auto_pkgs(void) {
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

    uint32_t count = hdr->entry_count;
    size_t max_len = 0;
    for (uint32_t i = 0; i < count; i++) {
        size_t len = strlen(names + offsets[i]);
        if (len > max_len) max_len = len;
    }

    if (count == 0) {
        munmap(mapped, st.st_size);
        close(fd);
        return;
    }

    struct winsize w;
    int width = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        width = w.ws_col;
    }

    int col_width = max_len + 2;
    int cols = width / col_width;
    if (cols < 1) cols = 1;

    int rows = (count + cols - 1) / cols;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = r * cols + c;
            if ((uint32_t)idx < count) {
                printf("%-*s", (int)col_width, names + offsets[idx]);
            }
        }
        printf("\n");
    }

    munmap(mapped, st.st_size);
    close(fd);
}
