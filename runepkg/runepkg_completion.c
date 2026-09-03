/*****************************************************************************
 * Filename:    runepkg_completion.c
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Bash completion logic for runepkg
 * LICENSE:     GPL v3
 ******************************************************************************/

#include "runepkg_portable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <glob.h>
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
#include "runepkg_defensive.h"

static int g_completion_printed_count = 0;
static const int MAX_COMPLETION_RESULTS = 99;

static char g_completion_seen[256][64];
static int g_completion_seen_count = 0;

static char g_already_on_cmdline[128][64];
static int g_already_on_cmdline_count = 0;

static char g_current_partial[128] = {0};

static void normalize_candidate_name(char *dest, const char *src, size_t dest_size) {
    const char *p;
    size_t len;

    if (!src) { dest[0] = '\0'; return; }

    /* Strip constraints like " (= 2:9.2...)" or " (>= 1.0)" */
    p = strchr(src, ' ');
    if (p && p[1] == '(') {
        len = (size_t)(p - src);
        if (len >= dest_size) len = dest_size - 1;
        memcpy(dest, src, len);
        dest[len] = '\0';
    } else {
        /* Strip version from local DB entries like "vim-2:9.2..." */
        const char *ver_sep = runepkg_util_find_version_separator(src);
        if (ver_sep && ver_sep != src) {
            len = (size_t)(ver_sep - src);
            if (len >= dest_size) len = dest_size - 1;
            memcpy(dest, src, len);
            dest[len] = '\0';
        } else {
            runepkg_secure_strcpy(dest, dest_size, src);
        }
    }
}

static void print_candidate(const char *name) {
    char norm[64];
    int i;

    if (g_completion_printed_count >= MAX_COMPLETION_RESULTS) return;
    if (!name || name[0] == '\0') return;

    normalize_candidate_name(norm, name, sizeof(norm));

    /* Safety: If normalization stripped the prefix the user is typing,
     * fallback to the full name to avoid breaking Bash completion.
     * e.g., if typing 'gcc-' and 'gcc-14' is normalized to 'gcc',
     * Bash would incorrectly try to replace 'gcc-' with 'gcc'. */
    if (g_current_partial[0] != '\0') {
        size_t plen = strlen(g_current_partial);
        if (strncmp(name, g_current_partial, plen) == 0 &&
            strncmp(norm, g_current_partial, plen) != 0) {
            runepkg_secure_strcpy(norm, sizeof(norm), name);
        }
    }

    /* 1. Filter out items already on the current command line (Lock Down) */
    for (i = 0; i < g_already_on_cmdline_count; i++) {
        if (strcmp(norm, g_already_on_cmdline[i]) == 0) return;
    }

    /* 2. Filter out duplicates (Visual Deduplication) */
    for (i = 0; i < g_completion_seen_count; i++) {
        if (strcmp(norm, g_completion_seen[i]) == 0) return;
    }

    /* 3. Add to seen list and print */
    if (g_completion_seen_count < 256) {
        runepkg_secure_strcpy(g_completion_seen[g_completion_seen_count++], 64, norm);
    }

    printf("%s\n", norm);
    g_completion_printed_count++;
}

static void populate_already_on_cmdline(const char *partial) {
    const char *comp_line = getenv("COMP_LINE");
    const char *comp_point_s = getenv("COMP_POINT");
    int comp_point = 0;
    char *buf;
    size_t use_len;
    size_t partial_len = partial ? strlen(partial) : 0;

    g_already_on_cmdline_count = 0;
    if (!comp_line) return;
    if (comp_point_s) comp_point = atoi(comp_point_s);

    use_len = (size_t)comp_point;
    if (use_len > partial_len) use_len -= partial_len;

    buf = malloc(use_len + 1);
    if (buf) {
        char *tok;
        memcpy(buf, comp_line, use_len);
        buf[use_len] = '\0';

        tok = strtok(buf, " \t");
        while (tok && g_already_on_cmdline_count < 128) {
            /* Only track tokens that don't look like flags and aren't the program name */
            if (tok[0] != '-' && strcmp(tok, "runepkg") != 0) {
                char norm[64];
                normalize_candidate_name(norm, tok, sizeof(norm));
                runepkg_secure_strcpy(g_already_on_cmdline[g_already_on_cmdline_count++], 64, norm);
            }
            tok = strtok(NULL, " \t");
        }
        free(buf);
    }
}

/**
 * @brief Checks if the current completion request is a repeated press of Tab
 * for the exact same command-line state. If so, suggests suppressing output
 * to avoid terminal "spam".
 *
 * @return 1 if completion should be suppressed, 0 otherwise.
 */
static int should_suppress_completion(void) {
    const char *comp_line = getenv("COMP_LINE");
    const char *comp_point_s = getenv("COMP_POINT");
    char state_path[PATH_MAX];
    FILE *fp;
    int last_point = -1;
    int count = 0;
    int comp_point;

    if (!comp_line || !comp_point_s || !g_runepkg_db_dir) return 0;
    comp_point = atoi(comp_point_s);

    snprintf(state_path, sizeof(state_path), "%s/.autocomplete_state", g_runepkg_db_dir);

    fp = fopen(state_path, "r");
    if (fp) {
        char line_point[32], line_count[32], line_cmd[1024];
        if (fgets(line_point, sizeof(line_point), fp) &&
            fgets(line_count, sizeof(line_count), fp) &&
            fgets(line_cmd, sizeof(line_cmd), fp)) {

            line_point[strcspn(line_point, "\n")] = 0;
            line_count[strcspn(line_count, "\n")] = 0;
            line_cmd[strcspn(line_cmd, "\n")] = 0;

            last_point = atoi(line_point);
            count = atoi(line_count);
            if (last_point == comp_point && strcmp(line_cmd, comp_line) == 0) {
                count++;
            } else {
                count = 1;
            }
        } else {
            count = 1;
        }
        fclose(fp);
    } else {
        count = 1;
    }

    fp = fopen(state_path, "w");
    if (fp) {
        fprintf(fp, "%d\n%d\n%s\n", comp_point, count, comp_line);
        fclose(fp);
    }

    /* Suppress if the same state has been queried more than twice consecutively.
     * Tab 1: Beep (Bash sees multiple)
     * Tab 2: Show List
     * Tab 3+: Suppress (User has seen it) */
    if (count > 2) return 1;
    return 0;
}

int is_completion_trigger(char *argv[]) {
    (void)argv; /* suppressed unused warning; argc check is done by caller */
    return 1;
}

void complete_file_paths_ext(const char *partial, const char *extra_dir, const char *suffix_filter) {
    const char *prefix;
    bool is_absolute;
    char dirbuf[PATH_MAX];
    const char *last_slash;
    const char *search_dir = ".";
    const char *match_prefix;
    DIR *d;
    const char* virtual_dirs[8];
    int num_v;
    size_t plen;
    int i;

    prefix = partial ? partial : "";
    is_absolute = (prefix[0] == '/');
    last_slash = strrchr(prefix, '/');
    match_prefix = prefix;

    if (last_slash) {
        size_t dirlen = (size_t)(last_slash - prefix);
        if (dirlen >= sizeof(dirbuf)) dirlen = sizeof(dirbuf)-1;
        memcpy(dirbuf, prefix, dirlen);
        dirbuf[dirlen] = '\0';

        if (dirlen == 0 && prefix[0] == '/') {
            search_dir = "/";
        } else {
            search_dir = dirbuf[0] ? dirbuf : ".";
        }
        match_prefix = last_slash + 1;
    } else if (is_absolute) {
        search_dir = "/";
        match_prefix = prefix + 1;
    }

    /* FS Scan */
    d = opendir(search_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            bool is_dir_entry;
            bool is_reg_entry;
            char full_path[PATH_MAX + 1024];
            struct stat st_entry;

            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;

            /* Skip hidden files unless specifically requested */
            if (e->d_name[0] == '.' && match_prefix[0] != '.') continue;

            if (strncmp(e->d_name, match_prefix, strlen(match_prefix)) != 0) continue;

            is_dir_entry = (e->d_type == DT_DIR);
            is_reg_entry = (e->d_type == DT_REG);
            if (e->d_type == DT_UNKNOWN) {
                char full[PATH_MAX + 512];
                struct stat st2;
                snprintf(full, sizeof(full), "%.*s/%s", (int)(sizeof(full)-258), search_dir, e->d_name);
                if (stat(full, &st2) == 0) {
                    is_dir_entry = S_ISDIR(st2.st_mode);
                    is_reg_entry = S_ISREG(st2.st_mode);
                }
            }

            if (suffix_filter && is_reg_entry) {
                size_t nlen = strlen(e->d_name);
                size_t slen = strlen(suffix_filter);
                if (nlen < slen || strcmp(e->d_name + nlen - slen, suffix_filter) != 0) continue;
            } else if (suffix_filter && !is_dir_entry) {
                continue;
            }

            snprintf(full_path, sizeof(full_path), "%.*s%s%s", (int)(sizeof(full_path)-258), search_dir, (search_dir[strlen(search_dir)-1] == '/') ? "" : "/", e->d_name);

            if (stat(full_path, &st_entry) == 0) {
                bool st_is_dir = S_ISDIR(st_entry.st_mode);
                bool st_is_reg = S_ISREG(st_entry.st_mode);

                if (suffix_filter && st_is_reg) {
                    size_t nlen = strlen(e->d_name);
                    size_t slen = strlen(suffix_filter);
                    if (nlen < slen || strcmp(e->d_name + nlen - slen, suffix_filter) != 0) continue;
                }

                if (st_is_dir) {
                    if (last_slash) {
                        size_t sd_len = strlen(search_dir);
                        const char *sep = (sd_len > 0 && search_dir[sd_len-1] == '/') ? "" : "/";
                        char buf[PATH_MAX + 256];
                        snprintf(buf, sizeof(buf), "%s%s%s/", search_dir, sep, e->d_name);
                        print_candidate(buf);
                    } else if (is_absolute) {
                        char buf[PATH_MAX + 256];
                        snprintf(buf, sizeof(buf), "/%s/", e->d_name);
                        print_candidate(buf);
                    } else {
                        char buf[PATH_MAX + 256];
                        snprintf(buf, sizeof(buf), "%s/", e->d_name);
                        print_candidate(buf);
                    }
                } else {
                    if (last_slash) {
                        size_t sd_len = strlen(search_dir);
                        const char *sep = (sd_len > 0 && search_dir[sd_len-1] == '/') ? "" : "/";
                        char buf[PATH_MAX + 256];
                        snprintf(buf, sizeof(buf), "%s%s%s", search_dir, sep, e->d_name);
                        print_candidate(buf);
                    } else if (is_absolute) {
                        char buf[PATH_MAX + 256];
                        snprintf(buf, sizeof(buf), "/%s", e->d_name);
                        print_candidate(buf);
                    } else {
                        print_candidate(e->d_name);
                    }
                }
            }
        }
        closedir(d);
    }

    virtual_dirs[0] = g_runepkg_base_dir; virtual_dirs[1] = g_download_dir;
    virtual_dirs[2] = g_build_dir; virtual_dirs[3] = g_debs_dir;
    virtual_dirs[4] = "/var/lib/runepkg_dir/"; virtual_dirs[5] = "/var/lib/runepkg_dir/download_dir/";
    virtual_dirs[6] = "/var/lib/runepkg_dir/build_dir/"; virtual_dirs[7] = "/var/lib/runepkg_dir/debs/";
    num_v = 8;
    plen = strlen(prefix);

    for (i=0; i<num_v; i++) {
        if (!virtual_dirs[i]) continue;

        if (strncmp(virtual_dirs[i], prefix, plen) == 0) {
            const char* remainder = virtual_dirs[i] + plen;

            if (*remainder == '\0') continue;

            {
                char segment[PATH_MAX];
                const char* remainder_start = (*remainder == '/') ? remainder + 1 : remainder;
                const char* next_slash = strchr(remainder_start, '/');

                if (next_slash) {
                    size_t seg_len = (size_t)(next_slash - virtual_dirs[i]) + 1;
                    runepkg_util_safe_strncpy(segment, virtual_dirs[i], seg_len + 1);
                    segment[seg_len] = '\0';
                    print_candidate(segment);
                } else {
                    char buf[PATH_MAX];
                    snprintf(buf, sizeof(buf), "%s/", virtual_dirs[i]);
                    print_candidate(buf);
                }
            }
        }
    }

    if (extra_dir && !is_absolute && !last_slash && prefix[0] != '\0') {
        DIR *ed = opendir(extra_dir);
        if (ed) {
            struct dirent *e;
            while ((e = readdir(ed)) != NULL) {
                char namebuf[PATH_MAX + 512];
                bool is_dir_e;
                bool is_reg_e;
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
                if (strncmp(e->d_name, match_prefix, strlen(match_prefix)) != 0) continue;

                is_dir_e = (e->d_type == DT_DIR);
                is_reg_e = (e->d_type == DT_REG);
                if (e->d_type == DT_UNKNOWN) {
                    char full[PATH_MAX + 512];
                    struct stat st2;
                    snprintf(full, sizeof(full), "%.*s/%s", (int)(sizeof(full)-258), extra_dir, e->d_name);
                    if (stat(full, &st2) == 0) {
                        is_dir_e = S_ISDIR(st2.st_mode);
                        is_reg_e = S_ISREG(st2.st_mode);
                    }
                }

                if (suffix_filter && is_reg_e) {
                    size_t nlen = strlen(e->d_name);
                    size_t slen = strlen(suffix_filter);
                    if (nlen < slen || strcmp(e->d_name + nlen - slen, suffix_filter) != 0) continue;
                } else if (suffix_filter && !is_dir_e) {
                    continue;
                }

                snprintf(namebuf, sizeof(namebuf), "%.*s/%s", (int)(sizeof(namebuf)-258), extra_dir, e->d_name);
                {
                    struct stat st;
                    if (stat(namebuf, &st) == 0 && S_ISDIR(st.st_mode)) {
                        if (namebuf[strlen(namebuf)-1] != '/') {
                            char buf2[PATH_MAX + 1024];
                            snprintf(buf2, sizeof(buf2), "%s/", namebuf);
                            print_candidate(buf2);
                        } else {
                            print_candidate(namebuf);
                        }
                    } else {
                        print_candidate(namebuf);
                    }
                }
            }
            closedir(ed);
        }
    }
}

void complete_file_paths(const char *partial) {
    complete_file_paths_ext(partial, NULL, NULL);
}

void complete_target_profiles(const char *partial) {
    const char* targets_dirs[4];
    int i;
    targets_dirs[0] = "/etc/runepkg/targets/";
    targets_dirs[1] = "targets/";
    targets_dirs[2] = runepkg_config_resolve_path(NULL, 2);
    targets_dirs[3] = NULL;

    for (i = 0; i < 3; i++) {
        DIR *d;
        if (!targets_dirs[i]) continue;
        d = opendir(targets_dirs[i]);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (e->d_type == DT_REG || e->d_type == DT_LNK || e->d_type == DT_UNKNOWN) {
                    size_t len = strlen(e->d_name);
                    if (len > 5 && strcmp(e->d_name + len - 5, ".conf") == 0) {
                        char name[64];
                        runepkg_secure_strcpy(name, sizeof(name), e->d_name);
                        name[len - 5] = '\0';
                        if (strncmp(name, partial, strlen(partial)) == 0) {
                            print_candidate(name);
                        }
                    }
                }
            }
            closedir(d);
        }
    }
    if (targets_dirs[2]) free((void*)targets_dirs[2]);
}

static void check_rebuild_autocomplete_index(void) {
    char index_path[PATH_MAX];
    struct stat index_st, db_dir_st, build_dir_st, debs_dir_st, download_dir_st;
    int index_exists;
    int db_dir_stat;
    int build_dir_stat;
    int debs_dir_stat;
    int download_dir_stat;
    bool needs_rebuild = false;

    if (!g_runepkg_db_dir) return;
    snprintf(index_path, sizeof(index_path), "%s/runepkg_autocomplete.bin", g_runepkg_db_dir);

    index_exists = (stat(index_path, &index_st) == 0);
    db_dir_stat = stat(g_runepkg_db_dir, &db_dir_st);
    build_dir_stat = g_build_dir ? stat(g_build_dir, &build_dir_st) : -1;
    debs_dir_stat = g_debs_dir ? stat(g_debs_dir, &debs_dir_st) : -1;
    download_dir_stat = g_download_dir ? stat(g_download_dir, &download_dir_st) : -1;

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

int prefix_search_and_print_ext(const char *prefix, const char *suffix_filter) {
    char index_path[PATH_MAX];
    int fd;
    struct stat st;
    void *mapped;
    AutocompleteHeader *hdr;
    uint32_t *offsets;
    char *names;
    int low, high;
    int first_match = -1;
    int found = 0;
    char last_printed[PATH_MAX];
    size_t plen = prefix ? strlen(prefix) : 0;

    if (!g_runepkg_db_dir) return 0;
    snprintf(index_path, sizeof(index_path), "%s/runepkg_autocomplete.bin", g_runepkg_db_dir);

    check_rebuild_autocomplete_index();

    fd = open(index_path, O_RDONLY);
    if (fd < 0) return 0;

    if (fstat(fd, &st) < 0) { close(fd); return 0; }

    mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) { close(fd); return 0; }

    hdr = (AutocompleteHeader *)mapped;
    if (hdr->magic != 0x52554E45) { munmap(mapped, st.st_size); close(fd); return 0; }

    offsets = (uint32_t *)((char *)mapped + sizeof(AutocompleteHeader));
    names = (char *)mapped + sizeof(AutocompleteHeader) + hdr->entry_count * sizeof(uint32_t);

    low = 0; high = hdr->entry_count - 1;
    if (plen > 0) {
        while (low <= high) {
            int mid = low + (high - low) / 2;
            char *current_name = names + offsets[mid];
            int cmp = strncmp(prefix, current_name, plen);
            if (cmp == 0) {
                first_match = mid; high = mid - 1;
            } else if (cmp < 0) {
                high = mid - 1;
            } else {
                low = mid + 1;
            }
        }
    } else {
        first_match = 0;
    }

    if (first_match == -1 && plen > 0 && low < (int)hdr->entry_count) {
        char *name = names + offsets[low];
        if (strncmp(prefix, name, plen) == 0) first_match = low;
    }

    memset(last_printed, 0, sizeof(last_printed));
    if (first_match != -1) {
        int i;
        for (i = first_match; i < (int)hdr->entry_count; i++) {
            char *name = names + offsets[i];
            bool prefix_has_slash;
            bool name_has_slash;

            if (plen > 0) {
                if (strncmp(prefix, name, plen) != 0) break;
            }
            if (g_completion_printed_count >= MAX_COMPLETION_RESULTS) break;

            prefix_has_slash = (prefix && strchr(prefix, '/') != NULL);
            name_has_slash = (strchr(name, '/') != NULL);

            if (plen > 0) {
                if (prefix_has_slash != name_has_slash) continue;
            }

            if (name_has_slash && prefix_has_slash) {
                const char *search_start = name + plen;
                const char *next_slash = strchr(search_start, '/');

                if (next_slash) {
                    char segment[PATH_MAX];
                    size_t seg_len = (next_slash - name) + 1;
                    if (seg_len < sizeof(segment)) {
                        runepkg_util_safe_strncpy(segment, name, seg_len + 1);
                        segment[seg_len] = '\0';
                        print_candidate(segment);
                        print_candidate(name);
                        found = 1;
                        continue;
                    }
                }
            }

            if (suffix_filter) {
                if (strcmp(suffix_filter, ":pkg") == 0) {
                    size_t nlen = strlen(name);
                    size_t clen = nlen;
                    if (clen > 0 && name[clen-1] == '/') clen--;

                    if (clen > 4 && (strncmp(name + clen - 4, ".deb", 4) == 0 || strncmp(name + clen - 4, ".dsc", 4) == 0)) {
                        continue;
                    }
                    if (clen > 4 && strncmp(name + clen - 4, "-src", 4) == 0) {
                        continue;
                    }
                    print_candidate(name);
                    found = 1;
                    continue;
                }
                {
                    size_t nlen = strlen(name);
                    size_t slen = strlen(suffix_filter);
                    if (nlen >= slen && strcmp(name + nlen - slen, suffix_filter) == 0) {
                        print_candidate(name);
                        found = 1;
                    }
                }
            } else {
                print_candidate(name);
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
    int fd;
    struct stat st;
    void *mapped;
    uint32_t count;
    struct RepoIndexEntry *entries;
    int low, high;
    int first_match = -1;
    size_t prefix_len = prefix ? strlen(prefix) : 0;

    if (!g_runepkg_db_dir) return 0;
    snprintf(index_path, sizeof(index_path), "%s/%s", g_runepkg_db_dir, index_filename);

    fd = open(index_path, O_RDONLY);
    if (fd < 0) return 0;

    if (fstat(fd, &st) < 0) { close(fd); return 0; }
    if (st.st_size < (off_t)sizeof(uint32_t)) { close(fd); return 0; }

    mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) { close(fd); return 0; }

    count = *(uint32_t *)mapped;
    entries = (struct RepoIndexEntry *)((char *)mapped + sizeof(uint32_t));

    low = 0; high = (int)count - 1;

    if (prefix_len > 0) {
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
    } else {
        first_match = 0;
    }

    if (first_match != -1) {
        int i;
        for (i = first_match; i < (int)count; i++) {
            if (prefix_len > 0 && strncmp(prefix, entries[i].name, prefix_len) != 0) break;
            if (g_completion_printed_count >= MAX_COMPLETION_RESULTS) break;
            print_candidate(entries[i].name);
        }
    }

    munmap(mapped, st.st_size);
    close(fd);
    return (first_match != -1) ? 1 : 0;
}

int runepkg_completion_get_repo_suggestions(const char *search_name, char suggestions[][PATH_MAX], int max_suggestions) {
    char index_path[PATH_MAX];
    int fd;
    struct stat st;
    void *mapped;
    uint32_t count;
    struct RepoIndexEntry *entries;
    int found = 0;
    char last_added[64];
    uint32_t i;

    if (!search_name || !suggestions || max_suggestions <= 0 || !g_runepkg_db_dir) return 0;

    snprintf(index_path, sizeof(index_path), "%s/repo_index.bin", g_runepkg_db_dir);

    fd = open(index_path, O_RDONLY);
    if (fd < 0) return 0;

    if (fstat(fd, &st) < 0) { close(fd); return 0; }
    if (st.st_size < (off_t)sizeof(uint32_t)) { close(fd); return 0; }

    mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) { close(fd); return 0; }

    count = *(uint32_t *)mapped;
    entries = (struct RepoIndexEntry *)((char *)mapped + sizeof(uint32_t));

    memset(last_added, 0, sizeof(last_added));

    /* Pass 1: Prefix matches */
    for (i = 0; i < count && found < max_suggestions; i++) {
        if (strncmp(entries[i].name, search_name, strlen(search_name)) == 0) {
            if (strcmp(last_added, entries[i].name) != 0) {
                runepkg_util_safe_strncpy(suggestions[found], entries[i].name, PATH_MAX);
                runepkg_util_safe_strncpy(last_added, entries[i].name, sizeof(last_added));
                found++;
            }
        }
    }

    /* Pass 2: Substring matches */
    if (found < max_suggestions) {
        for (i = 0; i < count && found < max_suggestions; i++) {
            if (strncmp(entries[i].name, search_name, strlen(search_name)) == 0) continue;

            if (strstr(entries[i].name, search_name) != NULL) {
                if (strcmp(last_added, entries[i].name) != 0) {
                    bool already = false;
                    int k;
                    for (k = 0; k < found; k++) {
                        if (strcmp(suggestions[k], entries[i].name) == 0) { already = true; break; }
                    }
                    if (already) continue;

                    runepkg_util_safe_strncpy(suggestions[found], entries[i].name, PATH_MAX);
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
    bool is_path;
    char inferred_cmd[64];
    int i;
    const char *short_opts2[2];
    const char *long_opts6[6];
    const char *all_long_opts4[4];
    const char *short_opts12[12];
    char *buf;
    char *full_line_copy;

    /* Reset completion counters for each new trigger */
    g_completion_printed_count = 0;
    g_completion_seen_count = 0;
    memset(g_current_partial, 0, sizeof(g_current_partial));
    if (partial) runepkg_secure_strcpy(g_current_partial, sizeof(g_current_partial), partial);

    if (should_suppress_completion()) return;

    populate_already_on_cmdline(partial);

    if (comp_point_s) comp_point = atoi(comp_point_s);

    is_path = (partial && (partial[0] == '/' || partial[0] == '.' || strchr(partial, '/') != NULL));

    memset(inferred_cmd, 0, sizeof(inferred_cmd));
    if (comp_line) {
        size_t len = strlen(comp_line);
        size_t use_len = len;
        if (comp_point > 0 && (size_t)comp_point < len) use_len = (size_t)comp_point;

        buf = malloc(use_len + 1);
        if (buf) {
            char *tok;
            const char *last_token = NULL;
            memcpy(buf, comp_line, use_len);
            buf[use_len] = '\0';

            tok = strtok(buf, " \t");
            if (tok) tok = strtok(NULL, " \t");
            while (tok) {
                if (strcmp(tok, "install") == 0 || strcmp(tok, "-i") == 0 || strcmp(tok, "--install") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "install");
                } else if (strcmp(tok, "remove") == 0 || strcmp(tok, "-r") == 0 || strcmp(tok, "--remove") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "remove");
                } else if (strcmp(tok, "list") == 0 || strcmp(tok, "-l") == 0 || strcmp(tok, "-L") == 0 || strcmp(tok, "--list") == 0 || strcmp(tok, "list-files") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "list");
                } else if (strcmp(tok, "status") == 0 || strcmp(tok, "-s") == 0 || strcmp(tok, "--status") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "status");
                } else if (strcmp(tok, "-u") == 0 || strcmp(tok, "--unpack") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "unpack");
                } else if (strcmp(tok, "-b") == 0 || strcmp(tok, "--build") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "build");
                } else if (strcmp(tok, "-m") == 0 || strcmp(tok, "--md5check") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "md5check");
                } else if (strcmp(tok, "source") == 0 || strcmp(tok, "source-depends") == 0 || strcmp(tok, "source-build-depends") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "source");
                } else if (strcmp(tok, "download-only") == 0 || strcmp(tok, "download-depends") == 0 || strcmp(tok, "download-build-depends") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "download-only");
                } else if (strcmp(tok, "buildpkg-split") == 0 || strcmp(tok, "--buildpkg-split") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "source-build");
                } else if (strcmp(tok, "update") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "update");
                } else if (strcmp(tok, "upgrade") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "upgrade");
                } else if (strcmp(tok, "sync") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "sync");
                } else if (strcmp(tok, "search") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "search");
                } else if (strcmp(tok, "info") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "info");
                } else if (strcmp(tok, "switch") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "switch");
                } else if (strcmp(tok, "bootstrap") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "bootstrap");
                } else if (strcmp(tok, "build-toolchain") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "build-toolchain");
                } else if (strcmp(tok, "resolve-tree") == 0) {
                    runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "resolve-tree");
                }
                last_token = tok;
                tok = strtok(NULL, " \t");
            }
            free(buf);

            if (inferred_cmd[0] == '\0' && last_token && last_token[0] == '-') {
                full_line_copy = strdup(comp_line);
                if (full_line_copy) {
                    char *t2 = strtok(full_line_copy, " \t");
                    if (t2) t2 = strtok(NULL, " \t");
                    while (t2) {
                        if (strcmp(t2, "install") == 0 || strcmp(t2, "-i") == 0 || strcmp(t2, "--install") == 0) {
                            runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "install");
                            break;
                        } else if (strcmp(t2, "remove") == 0 || strcmp(t2, "-r") == 0 || strcmp(t2, "--remove") == 0) {
                            runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "remove");
                            break;
                        } else if (strcmp(t2, "list") == 0 || strcmp(t2, "-l") == 0 || strcmp(t2, "-L") == 0 || strcmp(t2, "--list") == 0 || strcmp(t2, "list-files") == 0) {
                            runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "list");
                            break;
                        } else if (strcmp(t2, "status") == 0 || strcmp(t2, "-s") == 0 || strcmp(t2, "--status") == 0) {
                            runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "status");
                            break;
                        } else if (strcmp(t2, "-u") == 0 || strcmp(t2, "--unpack") == 0) {
                            runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "unpack");
                            break;
                        } else if (strcmp(t2, "-b") == 0 || strcmp(t2, "--build") == 0) {
                            runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "build");
                            break;
                        } else if (strcmp(t2, "-m") == 0 || strcmp(t2, "--md5check") == 0) {
                            runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "md5check");
                            break;
                        } else if (strcmp(t2, "source") == 0 || strcmp(t2, "source-depends") == 0 || strcmp(t2, "source-build-depends") == 0) {
                            runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "source");
                            break;
                        } else if (strcmp(t2, "download-only") == 0 || strcmp(t2, "download-depends") == 0 || strcmp(t2, "download-build-depends") == 0) {
                            runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "download-only");
                            break;
                        } else if (strcmp(t2, "buildpkg-split") == 0 || strcmp(t2, "--buildpkg-split") == 0) {
                            runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "source-build");
                            break;
                        } else if (strcmp(t2, "search") == 0) {
                            runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "search");
                            break;
                        } else if (strcmp(t2, "sync") == 0) {
                            runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "sync");
                            break;
                        } else if (strcmp(t2, "-S") == 0 || strcmp(t2, "--search") == 0) {
                            runepkg_secure_strcpy(inferred_cmd, sizeof(inferred_cmd), "search-file");
                            break;
                        }
                        t2 = strtok(NULL, " \t");
                    }
                    free(full_line_copy);
                }
            }
        }
    }

    if (inferred_cmd[0] != '\0') {
        if (strcmp(inferred_cmd, "install") == 0) {
            if (partial[0] == '-') {
                short_opts2[0] = "-f"; short_opts2[1] = "-v";
                for (i = 0; i < 2; i++) if (strncmp(short_opts2[i], partial, strlen(partial))==0) print_candidate(short_opts2[i]);
                if (strncmp(partial, "--", 2) == 0) {
                    long_opts6[0] = "--force"; long_opts6[1] = "--verbose"; long_opts6[2] = "--print-config";
                    long_opts6[3] = "--print-config-file"; long_opts6[4] = "--print-pkglist-file"; long_opts6[5] = "--print-autopool";
                    for (i = 0; i < 6; i++) if (strncmp(long_opts6[i], partial, strlen(partial))==0) print_candidate(long_opts6[i]);
                }
            } else if (is_path) {
                complete_file_paths_ext(partial, g_download_dir, ".deb");
            } else {
                prefix_search_and_print_ext(partial, ":pkg");
                repo_prefix_search_and_print(partial);
                if (partial[0] != '\0') {
                    complete_file_paths_ext(partial, g_download_dir, ".deb");
                }
            }
            return;
        }
        if (strcmp(inferred_cmd, "remove") == 0) {
            if (partial[0] == '-') {
                all_long_opts4[0] = "--remove"; all_long_opts4[1] = "--verbose"; all_long_opts4[2] = "--force"; all_long_opts4[3] = "--help";
                for (i=0;i<4;i++) if (strncmp(all_long_opts4[i], partial, strlen(partial))==0) print_candidate(all_long_opts4[i]);
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
                print_candidate("--help");
                print_candidate("--verbose");
                return;
            }
            if (is_path) {
                complete_file_paths(partial);
            } else {
                prefix_search_and_print_ext(partial, ":pkg");
                repo_prefix_search_and_print(partial);
            }
            return;
        }
        if (strcmp(inferred_cmd, "unpack") == 0) {
            complete_file_paths_ext(partial, g_download_dir, ".deb");
            return;
        }
        if (strcmp(inferred_cmd, "build") == 0) {
            if (is_path) {
                complete_file_paths_ext(partial, g_build_dir, ".dsc");
            } else {
                prefix_search_and_print(partial);
                complete_file_paths_ext(partial, g_build_dir, ".dsc");
            }
            return;
        }
        if (strcmp(inferred_cmd, "md5check") == 0) {
            prefix_search_and_print_ext(partial, ":pkg");
            return;
        }
        if (strcmp(inferred_cmd, "source") == 0) {
            if (is_path) complete_file_paths(partial);
            else {
                repo_src_prefix_search_and_print(partial);
                repo_prefix_search_and_print(partial);
            }
            return;
        }
        if (strcmp(inferred_cmd, "download-only") == 0) {
            if (is_path) complete_file_paths(partial);
            else repo_prefix_search_and_print(partial);
            return;
        }
        if (strcmp(inferred_cmd, "source-build") == 0) {
            if (is_path) {
                complete_file_paths_ext(partial, g_build_dir, ".dsc");
            } else {
                prefix_search_and_print_ext(partial, ".dsc");
                complete_file_paths_ext(partial, g_build_dir, ".dsc");
                repo_src_prefix_search_and_print(partial);
                repo_prefix_search_and_print(partial);
            }
            return;
        }
        if (strcmp(inferred_cmd, "info") == 0) {
            repo_prefix_search_and_print(partial);
            return;
        }
        if (strcmp(inferred_cmd, "search") == 0) {
            repo_prefix_search_and_print(partial);
            return;
        }
        if (strcmp(inferred_cmd, "search-file") == 0) {
            complete_file_paths(partial);
            return;
        }
        if (strcmp(inferred_cmd, "switch") == 0) {
            complete_target_profiles(partial);
            return;
        }
        if (strcmp(inferred_cmd, "bootstrap") == 0 || strcmp(inferred_cmd, "build-toolchain") == 0) {
            int space_count = 0;
            if (comp_line) {
                int cp = comp_point_s ? atoi(comp_point_s) : (int)strlen(comp_line);
                int k;
                for (k = 0; k < cp; k++) if (comp_line[k] == ' ') space_count++;
            }
            if (space_count <= 2) {
                complete_target_profiles(partial);
            } else {
                prefix_search_and_print_ext(partial, ":pkg");
                repo_prefix_search_and_print(partial);
            }
            return;
        }
        if (strcmp(inferred_cmd, "resolve-tree") == 0) {
            prefix_search_and_print_ext(partial, ":pkg");
            repo_prefix_search_and_print(partial);
            return;
        }
    }

    if (strcmp(prev, "runepkg") == 0) {
        if (partial[0] == '-') {
            if (strncmp(partial, "--", 2) == 0) {
                const char *long_opts[] = {
                    "--install", "--remove", "--list", "--status", "--list-files",
                    "--search", "--unpack", "--build", "--md5check", "--buildpkg-split",
                    "--verbose", "--force", "--version", "--help", "--print-config",
                    "--print-config-file", "--print-pkglist-file", "--print-autopool",
                    "--print-profile"
                };
                for (i = 0; i < 19; i++) if (strncmp(long_opts[i], partial, strlen(partial)) == 0) print_candidate(long_opts[i]);
            } else {
                short_opts12[0] = "-i"; short_opts12[1] = "-r"; short_opts12[2] = "-l";
                short_opts12[3] = "-s"; short_opts12[4] = "-L"; short_opts12[5] = "-S";
                short_opts12[6] = "-u"; short_opts12[7] = "-b"; short_opts12[8] = "-m";
                short_opts12[9] = "-v"; short_opts12[10] = "-f"; short_opts12[11] = "-h";
                for (i = 0; i < 12; i++) if (strncmp(short_opts12[i], partial, strlen(partial)) == 0) print_candidate(short_opts12[i]);
            }
        } else {
            const char *sub_cmds[] = {
                "sync", "install", "remove", "list", "status", "list-files", "search",
                "info", "download-only", "download-depends", "download-build-depends",
                "depends", "verify", "update", "upgrade", "source",
                "source-depends", "source-build-depends", "buildpkg-split", "build",
                "switch", "bootstrap", "build-toolchain", "resolve-tree"
            };
            for (i = 0; i < 24; i++) if (strncmp(sub_cmds[i], partial, strlen(partial)) == 0) print_candidate(sub_cmds[i]);
        }
    } else if (partial[0] == '-') {
        if (inferred_cmd[0] != '\0') {
            if (strcmp(inferred_cmd, "install") == 0) {
                short_opts2[0] = "-f"; short_opts2[1] = "-v";
                for (i = 0; i < 2; i++) if (strncmp(short_opts2[i], partial, strlen(partial))==0) print_candidate(short_opts2[i]);
                if (strncmp(partial, "--", 2) == 0) {
                    long_opts6[0] = "--force"; long_opts6[1] = "--verbose"; long_opts6[2] = "--print-config";
                    long_opts6[3] = "--print-config-file"; long_opts6[4] = "--print-pkglist-file"; long_opts6[5] = "--print-autopool";
                    for (i = 0; i < 6; i++) if (strncmp(long_opts6[i], partial, strlen(partial))==0) print_candidate(long_opts6[i]);
                }
            } else {
                print_candidate("--help"); print_candidate("--version"); print_candidate("--verbose"); print_candidate("--force");
            }
        } else {
            print_candidate("--help"); print_candidate("--version"); print_candidate("--verbose"); print_candidate("--force");
        }
        if (prev && prev[0] == '-') {
            complete_file_paths_ext(partial, g_download_dir, ".deb");
            prefix_search_and_print(partial);
        }
    } else if (strcmp(prev, "install") == 0 || strcmp(prev, "-i") == 0 || strcmp(prev, "--install") == 0) {
        if (is_path) {
            complete_file_paths_ext(partial, g_download_dir, ".deb");
        } else {
            prefix_search_and_print_ext(partial, ":pkg");
            repo_prefix_search_and_print(partial);
            if (partial[0] != '\0') {
                complete_file_paths_ext(partial, g_download_dir, ".deb");
            }
        }
    } else if (strcmp(prev, "remove") == 0 || strcmp(prev, "-r") == 0 || strcmp(prev, "--remove") == 0) {
        prefix_search_and_print_ext(partial, ":pkg");
    } else if (strcmp(prev, "list") == 0 || strcmp(prev, "-l") == 0 || strcmp(prev, "-L") == 0 || strcmp(prev, "--list") == 0 || strcmp(prev, "--list-files") == 0) {
        prefix_search_and_print_ext(partial, ":pkg");
    } else if (strcmp(prev, "status") == 0 || strcmp(prev, "-s") == 0 || strcmp(prev, "--status") == 0) {
        if (is_path) complete_file_paths(partial);
        else {
            prefix_search_and_print_ext(partial, ":pkg");
            repo_prefix_search_and_print(partial);
        }
    } else if (strcmp(prev, "source") == 0 || strcmp(prev, "source-depends") == 0 || strcmp(prev, "source-build-depends") == 0) {
        if (is_path) complete_file_paths(partial);
        else {
            repo_src_prefix_search_and_print(partial);
            repo_prefix_search_and_print(partial);
        }
    } else if (strcmp(prev, "download-only") == 0 || strcmp(prev, "download-depends") == 0 || strcmp(prev, "download-build-depends") == 0) {
        if (is_path) complete_file_paths(partial);
        else repo_prefix_search_and_print(partial);
    } else if (strcmp(prev, "buildpkg-split") == 0 || strcmp(prev, "--buildpkg-split") == 0) {
        if (is_path) {
            complete_file_paths_ext(partial, g_build_dir, ".dsc");
        } else {
            prefix_search_and_print_ext(partial, ".dsc");
            complete_file_paths_ext(partial, g_build_dir, ".dsc");
            repo_src_prefix_search_and_print(partial);
            repo_prefix_search_and_print(partial);
        }
    } else if (strcmp(prev, "unpack") == 0 || strcmp(prev, "-u") == 0 || strcmp(prev, "--unpack") == 0) {
        complete_file_paths_ext(partial, g_download_dir, ".deb");
    } else if (strcmp(prev, "build") == 0 || strcmp(prev, "-b") == 0 || strcmp(prev, "--build") == 0) {
        if (is_path) {
            complete_file_paths_ext(partial, g_build_dir, ".dsc");
        } else {
            prefix_search_and_print(partial);
            complete_file_paths_ext(partial, g_build_dir, NULL);
        }
    }
}

void handle_print_autopool(void) {
    char index_path[PATH_MAX];
    int fd;
    struct stat st;
    void *mapped;
    AutocompleteHeader *hdr;
    uint32_t *offsets;
    char *names;
    uint32_t count;
    size_t max_len = 0;
    uint32_t i_val;

    check_rebuild_autocomplete_index();
    print_package_data_header();
    printf("Listing consolidated autocomplete pool entries...\n");

    if (!g_runepkg_db_dir) {
        printf("Error: runepkg database directory not configured.\n");
        return;
    }
    snprintf(index_path, sizeof(index_path), "%s/runepkg_autocomplete.bin", g_runepkg_db_dir);

    fd = open(index_path, O_RDONLY);
    if (fd < 0) {
        printf("Error: Autocomplete index not found: %s\n", index_path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        printf("Error: Cannot stat index file.\n");
        close(fd);
        return;
    }

    mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        printf("Error: Cannot mmap index file.\n");
        close(fd);
        return;
    }

    hdr = (AutocompleteHeader *)mapped;
    if (hdr->magic != 0x52554E45) {
        printf("Error: Invalid index file magic.\n");
        munmap(mapped, st.st_size);
        close(fd);
        return;
    }

    offsets = (uint32_t *)((char *)mapped + sizeof(AutocompleteHeader));
    names = (char *)mapped + sizeof(AutocompleteHeader) + hdr->entry_count * sizeof(uint32_t);

    count = hdr->entry_count;
    for (i_val = 0; i_val < count; i_val++) {
        size_t len = strlen(names + offsets[i_val]);
        if (len > max_len) max_len = len;
    }

    if (count > 0) {
        struct winsize w;
        int width = 80;
        int col_width;
        int cols;
        int rows;
        int r;

        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
            width = w.ws_col;
        }

        col_width = (int)max_len + 2;
        cols = width / col_width;
        if (cols < 1) cols = 1;

        rows = (count + cols - 1) / cols;
        for (r = 0; r < rows; r++) {
            int c;
            for (c = 0; c < cols; c++) {
                int idx = r * cols + c;
                if ((uint32_t)idx < count) {
                    printf("%-*s", (int)col_width, names + offsets[idx]);
                }
            }
            printf("\n");
        }
    }

    munmap(mapped, st.st_size);
    close(fd);
}
