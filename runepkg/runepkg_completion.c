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
        snprintf(path, sizeof(path), "%.*s/%s", (int)(sizeof(path)-258), base, entry->d_name);
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
void complete_file_paths_ext(const char *partial, const char *extra_dir, const char *suffix_filter) {
    const char *prefix = partial ? partial : "";

    /* If a directory component exists, change search dir */
    char dirbuf[PATH_MAX];
    char namebuf[PATH_MAX + 512];
    const char *last_slash = strrchr(prefix, '/');
    const char *search_dir = ".";
    const char *match_prefix = prefix;
    if (last_slash) {
        size_t dirlen = last_slash - prefix;
        if (dirlen >= sizeof(dirbuf)) dirlen = sizeof(dirbuf)-1;
        memcpy(dirbuf, prefix, dirlen);
        dirbuf[dirlen] = '\0';

        if (dirlen == 0 && prefix[0] == '/') {
            search_dir = "/";
        } else {
            search_dir = dirbuf[0] ? dirbuf : ".";
        }
        match_prefix = last_slash + 1;
    }

    DIR *d = opendir(search_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            if (strncmp(e->d_name, match_prefix, strlen(match_prefix)) != 0) continue;

            bool is_dir = (e->d_type == DT_DIR);
            bool is_reg = (e->d_type == DT_REG);
            if (e->d_type == DT_UNKNOWN) {
                char full[PATH_MAX + 512];
                snprintf(full, sizeof(full), "%.*s/%s", (int)(sizeof(full)-258), search_dir, e->d_name);
                struct stat st;
                if (stat(full, &st) == 0) {
                    is_dir = S_ISDIR(st.st_mode);
                    is_reg = S_ISREG(st.st_mode);
                }
            }

            if (suffix_filter && is_reg) {
                size_t nlen = strlen(e->d_name);
                size_t slen = strlen(suffix_filter);
                if (nlen < slen || strcmp(e->d_name + nlen - slen, suffix_filter) != 0) continue;
            } else if (suffix_filter && !is_dir) {
                continue;
            }

            if (last_slash) {
                size_t sd_len = strlen(search_dir);
                const char *sep = (sd_len > 0 && search_dir[sd_len-1] == '/') ? "" : "/";
                snprintf(namebuf, sizeof(namebuf), "%.*s%s%s", (int)(sizeof(namebuf)-258), search_dir, sep, e->d_name);
            } else {
                snprintf(namebuf, sizeof(namebuf), "%s", e->d_name);
            }

            struct stat st;
            if (stat(namebuf, &st) == 0 && S_ISDIR(st.st_mode)) {
                if (namebuf[strlen(namebuf)-1] != '/') {
                    printf("%s/\n", namebuf);
                } else {
                    printf("%s\n", namebuf);
                }
            } else {
                printf("%s\n", namebuf);
            }
        }
        closedir(d);
    }

    /* Check for specific known virtual paths (user-facing only) */
    const char* virtual_dirs[] = {g_runepkg_base_dir, g_download_dir, g_build_dir, g_debs_dir,
                                  "/var/lib/runepkg_dir/", "/var/lib/runepkg_dir/download_dir/",
                                  "/var/lib/runepkg_dir/build_dir/", "/var/lib/runepkg_dir/debs/"};
    int num_v = sizeof(virtual_dirs)/sizeof(virtual_dirs[0]);
    size_t plen = strlen(prefix);

    for (int i=0; i<num_v; i++) {
        if (!virtual_dirs[i]) continue;

        // If the path we are typing matches the start of a virtual dir
        if (strncmp(virtual_dirs[i], prefix, plen) == 0) {
            const char* remainder = virtual_dirs[i] + plen;

            // If prefix was exactly a virtual dir, suggest it (with /)
            if (*remainder == '\0') {
                printf("%s/\n", virtual_dirs[i]);
                continue;
            }

            // Find the next segment of this virtual dir
            char segment[PATH_MAX];
            const char* remainder_start = remainder + (*remainder == '/' ? 1 : 0);
            const char* next_slash = strchr(remainder_start, '/');
            if (next_slash) {
                size_t seg_len = (next_slash - virtual_dirs[i]) + 1;
                strncpy(segment, virtual_dirs[i], seg_len);
                segment[seg_len] = '\0';
            } else {
                strcpy(segment, virtual_dirs[i]);
            }

            if (strcmp(segment, prefix) != 0) {
                /* If segment is just "/", and prefix is empty, suggest the first real component instead */
                if (plen == 0 && strcmp(segment, "/") == 0) {
                    const char *first_comp = strchr(virtual_dirs[i] + 1, '/');
                    if (first_comp) {
                        size_t flen = (first_comp - virtual_dirs[i]) + 1;
                        strncpy(segment, virtual_dirs[i], flen);
                        segment[flen] = '\0';
                    }
                }
                printf("%s%s\n", segment, (segment[strlen(segment)-1] == '/') ? "" : "/");
            }
        }
    }

    /* Also scan extra_dir if provided and we are not doing a deep path completion already */
    if (extra_dir && !last_slash) {
        DIR *ed = opendir(extra_dir);
        if (ed) {
            struct dirent *e;
            while ((e = readdir(ed)) != NULL) {
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
                if (strncmp(e->d_name, match_prefix, strlen(match_prefix)) != 0) continue;

                bool is_dir = (e->d_type == DT_DIR);
                bool is_reg = (e->d_type == DT_REG);
                if (e->d_type == DT_UNKNOWN) {
                    char full[PATH_MAX + 512];
                    snprintf(full, sizeof(full), "%.*s/%s", (int)(sizeof(full)-258), extra_dir, e->d_name);
                    struct stat st;
                    if (stat(full, &st) == 0) {
                        is_dir = S_ISDIR(st.st_mode);
                        is_reg = S_ISREG(st.st_mode);
                    }
                }

                if (suffix_filter && is_reg) {
                    size_t nlen = strlen(e->d_name);
                    size_t slen = strlen(suffix_filter);
                    if (nlen < slen || strcmp(e->d_name + nlen - slen, suffix_filter) != 0) continue;
                } else if (suffix_filter && !is_dir) {
                    continue;
                }

                snprintf(namebuf, sizeof(namebuf), "%.*s/%s", (int)(sizeof(namebuf)-258), extra_dir, e->d_name);
                struct stat st;
                if (stat(namebuf, &st) == 0 && S_ISDIR(st.st_mode)) {
                    if (namebuf[strlen(namebuf)-1] != '/') {
                        printf("%s/\n", namebuf);
                    } else {
                        printf("%s\n", namebuf);
                    }
                } else {
                    printf("%s\n", namebuf);
                }
            }
            closedir(ed);
        }
    }
}

void complete_file_paths(const char *partial) {
    complete_file_paths_ext(partial, NULL, NULL);
}

static void check_rebuild_autocomplete_index(void) {
    char index_path[PATH_MAX];
    if (!g_runepkg_db_dir) return;
    snprintf(index_path, sizeof(index_path), "%s/runepkg_autocomplete.bin", g_runepkg_db_dir);

    struct stat index_st, db_dir_st, build_dir_st, debs_dir_st, download_dir_st;
    int index_exists = (stat(index_path, &index_st) == 0);
    int db_dir_stat = stat(g_runepkg_db_dir, &db_dir_st);
    int build_dir_stat = g_build_dir ? stat(g_build_dir, &build_dir_st) : -1;
    int debs_dir_stat = g_debs_dir ? stat(g_debs_dir, &debs_dir_st) : -1;
    int download_dir_stat = g_download_dir ? stat(g_download_dir, &download_dir_st) : -1;

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
        if (debs_dir_stat == 0 && debs_dir_st.st_mtime > index_st.st_mtime) {
            needs_rebuild = true;
        }
        if (download_dir_stat == 0 && download_dir_st.st_mtime > index_st.st_mtime) {
            needs_rebuild = true;
        }
    }

    if (needs_rebuild) {
        runepkg_storage_build_autocomplete_index();
    }
}

/* Search the binary autocomplete index for prefix matches and print them. */
int prefix_search_and_print_ext(const char *prefix, const char *suffix_filter) {
    char index_path[PATH_MAX];
    if (!g_runepkg_db_dir) return 0;
    snprintf(index_path, sizeof(index_path), "%s/runepkg_autocomplete.bin", g_runepkg_db_dir);

    check_rebuild_autocomplete_index();

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

    int found = 0;
    char last_printed[PATH_MAX] = {0};
    if (first_match != -1) {
        for (int i = first_match; i < (int)hdr->entry_count; i++) {
            char *name = names + offsets[i];
            if (strncmp(prefix, name, strlen(prefix)) != 0) break;

            size_t plen = strlen(prefix);
            bool prefix_has_slash = (prefix && strchr(prefix, '/') != NULL);
            bool name_has_slash = (strchr(name, '/') != NULL);

            if (plen > 0) {
                /* If prefix is not empty, ensure strict path/basename matching */
                if (prefix_has_slash != name_has_slash) continue;
            }

            if (name_has_slash && prefix_has_slash) {
                /* Path navigation: suggest the next directory segment AND the full name.
                 *
                 * SOLUTION FOR "SKIPPING PAST" DIRECTORIES:
                 * When Bash sees only one possible completion, it "jumps" to the end and
                 * adds a space, which is frustrating when navigating deep paths.
                 * By providing BOTH the next segment (e.g., /var/lib/) AND the full
                 * deep path (e.g., /var/lib/runepkg_dir/...), we force Bash to see
                 * multiple possibilities. Bash will then complete only up to the
                 * common prefix (the next slash) and wait for the user to hit Tab
                 * again, enabling clean, segment-by-segment navigation.
                 */

                /* If we are at a directory boundary (prefix ends in slash),
                 * find the NEXT slash. If not, find the current level's slash. */
                const char *search_start = name + plen;
                const char *next_slash = strchr(search_start, '/');

                /* If we found a slash, suggest only up to that slash */
                if (next_slash) {
                    char segment[PATH_MAX];
                    size_t seg_len = (next_slash - name) + 1;
                    if (seg_len < sizeof(segment)) {
                        strncpy(segment, name, seg_len);
                        segment[seg_len] = '\0';
                        if (strcmp(segment, last_printed) != 0) {
                            printf("%s\n", segment);
                            strncpy(last_printed, segment, sizeof(last_printed)-1);
                            found = 1;
                        }
                        /* Also print the full name as a "secondary" option to keep completion open and prevent jumping */
                        printf("%s\n", name);
                        continue;
                    }
                }
            }

            if (suffix_filter) {
                if (strcmp(suffix_filter, ":pkg") == 0) {
                    /* Only show entries that are NOT .deb, .dsc or -src files/dirs */
                    size_t nlen = strlen(name);
                    size_t clen = nlen;
                    if (clen > 0 && name[clen-1] == '/') clen--;

                    if (clen > 4 && (strncmp(name + clen - 4, ".deb", 4) == 0 || strncmp(name + clen - 4, ".dsc", 4) == 0)) {
                        continue;
                    }
                    if (clen > 4 && strncmp(name + clen - 4, "-src", 4) == 0) {
                        continue;
                    }
                    printf("%s\n", name);
                    found = 1;
                    continue;
                }
                size_t nlen = strlen(name);
                size_t slen = strlen(suffix_filter);
                if (nlen >= slen && strcmp(name + nlen - slen, suffix_filter) == 0) {
                    printf("%s\n", name);
                    found = 1;
                }
            } else {
                printf("%s\n", name);
                found = 1;
            }
        }
    }

    munmap(mapped, st.st_size);
    close(fd);
    return found;
}

int prefix_search_and_print(const char *prefix) {
    return prefix_search_and_print_ext(prefix, NULL);
}

struct RepoIndexEntry {
    char name[64];
    uint32_t file_id;
    uint32_t offset;
};

int repo_generic_prefix_search(const char *prefix, const char *index_filename) {
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

int runepkg_completion_get_repo_suggestions(const char *search_name, char suggestions[][PATH_MAX], int max_suggestions) {
    if (!search_name || !suggestions || max_suggestions <= 0 || !g_runepkg_db_dir) return 0;

    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/repo_index.bin", g_runepkg_db_dir);

    int fd = open(index_path, O_RDONLY);
    if (fd < 0) return 0;

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return 0; }
    if (st.st_size < (off_t)sizeof(uint32_t)) { close(fd); return 0; }

    void *mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) { close(fd); return 0; }

    uint32_t count = *(uint32_t *)mapped;
    struct RepoIndexEntry *entries = (struct RepoIndexEntry *)((char *)mapped + sizeof(uint32_t));

    int found = 0;
    char last_added[64] = {0};

    /* Pass 1: Prefix matches (higher relevance) */
    for (uint32_t i = 0; i < count && found < max_suggestions; i++) {
        if (strncmp(entries[i].name, search_name, strlen(search_name)) == 0) {
            if (strcmp(last_added, entries[i].name) != 0) {
                strncpy(suggestions[found], entries[i].name, PATH_MAX - 1);
                suggestions[found][PATH_MAX - 1] = '\0';
                strncpy(last_added, entries[i].name, sizeof(last_added) - 1);
                found++;
            }
        }
    }

    /* Pass 2: Substring matches (if we still have room) */
    if (found < max_suggestions) {
        for (uint32_t i = 0; i < count && found < max_suggestions; i++) {
            /* Skip if it was already found as a prefix match */
            if (strncmp(entries[i].name, search_name, strlen(search_name)) == 0) continue;

            if (strstr(entries[i].name, search_name) != NULL) {
                if (strcmp(last_added, entries[i].name) != 0) {
                    /* Secondary check to avoid duplicates from previous pass if names are unsorted or similar */
                    bool already = false;
                    for (int k = 0; k < found; k++) {
                        if (strcmp(suggestions[k], entries[i].name) == 0) { already = true; break; }
                    }
                    if (already) continue;

                    strncpy(suggestions[found], entries[i].name, PATH_MAX - 1);
                    suggestions[found][PATH_MAX - 1] = '\0';
                    found++;
                }
            }
        }
    }

    munmap(mapped, st.st_size);
    close(fd);
    return found;
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
                } else if (strcmp(tok, "source") == 0 || strcmp(tok, "source-depends") == 0 || strcmp(tok, "source-build-depends") == 0) {
                    strncpy(inferred_cmd, "source", sizeof(inferred_cmd)-1);
                } else if (strcmp(tok, "download-only") == 0 || strcmp(tok, "download-depends") == 0 || strcmp(tok, "download-build-depends") == 0) {
                    strncpy(inferred_cmd, "download-only", sizeof(inferred_cmd)-1);
                } else if (strcmp(tok, "source-build") == 0) {
                    strncpy(inferred_cmd, "source-build", sizeof(inferred_cmd)-1);
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
                        } else if (strcmp(t2, "source") == 0 || strcmp(t2, "source-depends") == 0 || strcmp(t2, "source-build-depends") == 0) {
                            strncpy(inferred_cmd, "source", sizeof(inferred_cmd)-1);
                            break;
                        } else if (strcmp(t2, "download-only") == 0 || strcmp(t2, "download-depends") == 0 || strcmp(t2, "download-build-depends") == 0) {
                            strncpy(inferred_cmd, "download-only", sizeof(inferred_cmd)-1);
                            break;
                        } else if (strcmp(t2, "source-build") == 0) {
                            strncpy(inferred_cmd, "source-build", sizeof(inferred_cmd)-1);
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
                const char *short_opts[] = {"-f", "-v"};
                int n = sizeof(short_opts)/sizeof(short_opts[0]);
                for (int i = 0; i < n; i++) if (strncmp(short_opts[i], partial, strlen(partial))==0) printf("%s\n", short_opts[i]);
                if (strncmp(partial, "--", 2) == 0) {
                    const char *long_opts[] = {"--force", "--verbose", "--print-config", "--print-config-file", "--print-pkglist-file", "--print-autopool"};
                    int m = sizeof(long_opts)/sizeof(long_opts[0]);
                    for (int i = 0; i < m; i++) if (strncmp(long_opts[i], partial, strlen(partial))==0) printf("%s\n", long_opts[i]);
                }
            } else {
                prefix_search_and_print_ext(partial, ".deb");
                prefix_search_and_print_ext(partial, ":pkg");
                repo_prefix_search_and_print(partial);
                complete_deb_files(partial);
                complete_file_paths_ext(partial, g_download_dir, ".deb");
            }
            return;
        }
        if (strcmp(inferred_cmd, "remove") == 0) {
            if (partial[0] == '-') {
                // ... same as before but tighter
                const char *all_long_opts[] = {
                    "--remove","--verbose","--force","--help"
                };
                int n = sizeof(all_long_opts)/sizeof(all_long_opts[0]);
                for (int i=0;i<n;i++) if (strncmp(all_long_opts[i], partial, strlen(partial))==0) printf("%s\n", all_long_opts[i]);
            } else {
                prefix_search_and_print_ext(partial, ":pkg");
            }
            return;
        }
        if (strcmp(inferred_cmd, "list") == 0) {
            prefix_search_and_print_ext(partial, ":pkg");
            return;
        }
        if (strcmp(inferred_cmd, "status") == 0) {
            if (partial && partial[0] == '-') {
                printf("--help\n--verbose\n");
                return;
            }
            prefix_search_and_print_ext(partial, ":pkg");
            repo_prefix_search_and_print(partial);
            return;
        }
        if (strcmp(inferred_cmd, "unpack") == 0) {
            prefix_search_and_print_ext(partial, ".deb");
            complete_deb_files(partial);
            complete_file_paths_ext(partial, g_download_dir, ".deb");
            return;
        }
        if (strcmp(inferred_cmd, "build") == 0) {
            /* For build, we want directories and potentially other files.
             * Using NULL filter shows everything in the pool (filtered by path-ness). */
            prefix_search_and_print(partial);
            complete_file_paths_ext(partial, g_build_dir, NULL);
            return;
        }
        if (strcmp(inferred_cmd, "md5check") == 0) {
            prefix_search_and_print_ext(partial, ":pkg");
            return;
        }
        if (strcmp(inferred_cmd, "source") == 0) {
            repo_src_prefix_search_and_print(partial);
            return;
        }
        if (strcmp(inferred_cmd, "download-only") == 0) {
            repo_prefix_search_and_print(partial);
            return;
        }
        if (strcmp(inferred_cmd, "source-build") == 0) {
            /* Tighten up: ONLY .dsc */
            prefix_search_and_print_ext(partial, ".dsc");
            complete_file_paths_ext(partial, g_build_dir, ".dsc");
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
                    "--print-config", "--print-config-file", "--print-pkglist-file", "--print-autopool"
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
                "download-only", "download-depends", "download-build-depends", "depends", "verify", "update", "upgrade", "source", "source-depends", "source-build-depends", "source-build"
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
                    const char *long_opts[] = {"--force", "--verbose", "--print-config", "--print-config-file", "--print-pkglist-file", "--print-autopool"};
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
        prefix_search_and_print_ext(partial, ".deb");
        prefix_search_and_print_ext(partial, ":pkg");
        repo_prefix_search_and_print(partial);
        complete_deb_files(partial);
        complete_file_paths_ext(partial, g_download_dir, ".deb");
    } else if (strcmp(prev, "remove") == 0 || strcmp(prev, "-r") == 0) {
        prefix_search_and_print_ext(partial, ":pkg");
    } else if (strcmp(prev, "list") == 0 || strcmp(prev, "-l") == 0 || strcmp(prev, "-L") == 0) {
        prefix_search_and_print_ext(partial, ":pkg");
    } else if (strcmp(prev, "status") == 0 || strcmp(prev, "-s") == 0) {
        prefix_search_and_print_ext(partial, ":pkg");
        repo_prefix_search_and_print(partial);
    } else if (strcmp(prev, "source") == 0 || strcmp(prev, "source-depends") == 0 || strcmp(prev, "source-build-depends") == 0) {
        repo_src_prefix_search_and_print(partial);
    } else if (strcmp(prev, "download-only") == 0 || strcmp(prev, "download-depends") == 0 || strcmp(prev, "download-build-depends") == 0) {
        repo_prefix_search_and_print(partial);
    } else if (strcmp(prev, "source-build") == 0) {
        prefix_search_and_print_ext(partial, ".dsc");
        complete_file_paths_ext(partial, g_build_dir, ".dsc");
        repo_src_prefix_search_and_print(partial);
    } else if (strcmp(prev, "unpack") == 0 || strcmp(prev, "-u") == 0) {
        prefix_search_and_print_ext(partial, ".deb");
        complete_deb_files(partial);
        complete_file_paths_ext(partial, g_download_dir, ".deb");
    } else if (strcmp(prev, "build") == 0 || strcmp(prev, "-b") == 0) {
        prefix_search_and_print(partial);
        complete_file_paths_ext(partial, g_build_dir, NULL);
    }
}

void handle_print_autopool(void) {
    check_rebuild_autocomplete_index();
    print_package_data_header();
    printf("Listing consolidated autocomplete pool entries...\n");

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
