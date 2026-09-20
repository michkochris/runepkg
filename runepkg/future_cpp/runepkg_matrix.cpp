/*****************************************************************************
 * Filename:    runepkg_matrix.cpp
 * Author:      <michkochris@gmail.com>
 * Date:        2026-08-29
 * Description: Target Profile Matrix Engine & Declarative Package Recipes
 * LICENSE:     GPL v3
 ******************************************************************************/

#include "runepkg_matrix.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

extern "C" {
    #include "runepkg_config.h"
    #include "runepkg_util.h"
}

#define SYSTEM_RULES_DIR "/etc/runepkg/target-rules"

/* Standard 32-bit FNV-1a Hash */
static uint32_t fnv1a_hash_local(const char *str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}

bool RuneMatrixEngine::is_binary_stale() {
    char *bin_path = runepkg_util_concat_path(g_runepkg_db_dir, "matrix_rules.bin");
    struct stat bin_st;

    if (stat(bin_path, &bin_st) != 0) {
        free(bin_path);
        return true;
    }

    // Check system rules directory recursively
    DIR *dir = opendir(SYSTEM_RULES_DIR);
    if (!dir) {
        free(bin_path);
        return false; // No rules to compile
    }

    bool stale = false;
    std::vector<std::string> stack;
    stack.push_back(SYSTEM_RULES_DIR);

    while (!stack.empty()) {
        std::string current_dir = stack.back();
        stack.pop_back();

        DIR *dp = opendir(current_dir.c_str());
        if (!dp) continue;

        struct dirent *entry;
        while ((entry = readdir(dp)) != NULL) {
            if (entry->d_name[0] == '.') continue;

            std::string full_path = current_dir + "/" + entry->d_name;
            struct stat st;
            if (stat(full_path.c_str(), &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    stack.push_back(full_path);
                } else if (st.st_mtime > bin_st.st_mtime) {
                    stale = true;
                    break;
                }
            }
        }
        closedir(dp);
        if (stale) break;
    }

    closedir(dir);
    free(bin_path);
    return stale;
}

bool RuneMatrixEngine::recompile_binary() {
    std::cout << "\033[1;34m[matrix]\033[0m Synchronizing target forge rules..." << std::endl;
    char *bin_path = runepkg_util_concat_path(g_runepkg_db_dir, "matrix_rules.bin");

    bool success = runepkg_matrix_compile_rules(SYSTEM_RULES_DIR, bin_path);

    free(bin_path);
    return success;
}

static void* g_matrix_mapped = nullptr;
static size_t g_matrix_mapped_size = 0;

const RuneMatrixRecord* RuneMatrixEngine::find_record(const std::string& pkg_name) {
    if (is_binary_stale()) {
        if (g_matrix_mapped) {
            munmap(g_matrix_mapped, g_matrix_mapped_size);
            g_matrix_mapped = nullptr;
        }
        recompile_binary();
    }

    if (!g_matrix_mapped) {
        char *bin_path = runepkg_util_concat_path(g_runepkg_db_dir, "matrix_rules.bin");
        int fd = open(bin_path, O_RDONLY);
        if (fd < 0) {
            free(bin_path);
            return nullptr;
        }

        struct stat st;
        fstat(fd, &st);
        g_matrix_mapped_size = st.st_size;
        g_matrix_mapped = mmap(NULL, g_matrix_mapped_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        free(bin_path);

        if (g_matrix_mapped == MAP_FAILED) {
            g_matrix_mapped = nullptr;
            return nullptr;
        }
    }

    const RuneMatrixHeader *hdr = (const RuneMatrixHeader *)g_matrix_mapped;
    if (hdr->magic != RUNE_MATRIX_MAGIC || hdr->version != RUNE_MATRIX_VERSION) {
        return nullptr;
    }

    uint32_t hash = fnv1a_hash_local(pkg_name.c_str());
    const uint32_t *buckets = (const uint32_t *)((const uint8_t *)g_matrix_mapped + sizeof(RuneMatrixHeader));
    uint32_t bucket_idx = hash % hdr->bucket_count;
    uint32_t record_offset = buckets[bucket_idx];

    const char *strings = (const char *)g_matrix_mapped + hdr->string_pool_off;

    while (record_offset != 0) {
        const RuneMatrixRecord *rec = (const RuneMatrixRecord *)((const uint8_t *)g_matrix_mapped + record_offset);
        if (rec->pkg_name_hash == hash && strcmp(strings + rec->pkg_name_offset, pkg_name.c_str()) == 0) {
            return rec;
        }
        record_offset = rec->next_record_off;
    }

    return nullptr;
}

ProfileMatrix RuneMatrixEngine::parse_profile(const std::string& name) {
    ProfileMatrix m;
    m.profile_name = name;

    m.is_static = (name.find("static") != std::string::npos);
    m.is_pie = (name.find("pie") != std::string::npos);

    // Direct mapping from active targets/*.conf if loaded
    if (g_active_profile && name == (g_active_profile->name ? g_active_profile->name : "")) {
        if (g_active_profile->arch) m.arch = g_active_profile->arch;
        if (g_active_profile->libc) m.libc = g_active_profile->libc;
        if (g_active_profile->triplet) m.triplet = g_active_profile->triplet;
        if (g_active_profile->sysroot) m.sysroot = g_active_profile->sysroot;
        if (g_active_profile->crosstools) m.crosstools = g_active_profile->crosstools;
        if (g_active_profile->cross_bin) m.cross_bin = g_active_profile->cross_bin;
    }

    // Fallback heuristics if fields are still empty
    if (m.arch.empty()) {
        if (name.find("x86_64") != std::string::npos || name.find("amd64") != std::string::npos) m.arch = "x86_64";
        else if (name.find("aarch64") != std::string::npos || name.find("arm64") != std::string::npos) m.arch = "aarch64";
        else if (name.find("armhf") != std::string::npos || name.find("armv7") != std::string::npos) m.arch = "armhf";
        else if (name.find("riscv64") != std::string::npos) m.arch = "riscv64";
        else m.arch = "x86_64";
    }

    if (m.libc.empty()) {
        if (name.find("musl") != std::string::npos) m.libc = "musl";
        else m.libc = "glibc";
    }

    if (m.triplet.empty()) {
        if (m.libc == "musl") m.triplet = m.arch + "-unknown-linux-musl";
        else m.triplet = m.arch + "-linux-gnu";
    }

    return m;
}

BuildSystem RuneMatrixEngine::detect_build_system(const fs::path& src_dir) {
    if (fs::exists(src_dir / "configure") || fs::exists(src_dir / "configure.ac") || fs::exists(src_dir / "autogen.sh")) {
        return BuildSystem::AUTOTOOLS;
    }
    if (fs::exists(src_dir / "CMakeLists.txt")) return BuildSystem::CMAKE;
    if (fs::exists(src_dir / "meson.build")) return BuildSystem::MESON;
    return BuildSystem::CUSTOM_MAKE;
}

static void expand_recipe_vars(RuneMatrixRecipe& recipe, const ProfileMatrix& matrix) {
    auto expand = [&](const std::string& s) -> std::string {
        if (s.empty()) return "";
        // Set environment variables for runepkg_util_expand_vars
        if (!matrix.triplet.empty()) setenv("TARGET_TRIPLE", matrix.triplet.c_str(), 1);
        if (!matrix.sysroot.empty()) setenv("SYSROOT_DIR", matrix.sysroot.c_str(), 1);
        if (!matrix.crosstools.empty()) setenv("TOOLCHAIN_DIR", matrix.crosstools.c_str(), 1);
        if (!matrix.arch.empty()) setenv("TARGET_ARCH", matrix.arch.c_str(), 1);
        if (!matrix.libc.empty()) setenv("TARGET_LIBC", matrix.libc.c_str(), 1);
        setenv("STATIC_BUILD", matrix.is_static ? "1" : "0", 1);

        char* expanded = runepkg_util_expand_vars(s.c_str());
        if (expanded) {
            std::string res(expanded);
            free(expanded);
            return res;
        }
        return s;
    };

    auto split_push = [](std::vector<std::string>& vec, const std::string& input) {
        if (input.empty()) return;
        std::string current;
        bool in_quotes = false;
        for (size_t i = 0; i < input.length(); ++i) {
            char c = input[i];
            if (c == '\"') {
                in_quotes = !in_quotes;
            } else if (std::isspace(c) && !in_quotes) {
                if (!current.empty()) {
                    vec.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) vec.push_back(current);
    };

    std::vector<std::string> raw_conf = recipe.extra_conf_args;
    std::vector<std::string> raw_cflags = recipe.extra_cflags;
    std::vector<std::string> raw_ldflags = recipe.extra_ldflags;
    std::vector<std::string> raw_make_build = recipe.make_build_targets;
    std::vector<std::string> raw_make_inst = recipe.make_install_targets;

    recipe.extra_conf_args.clear();
    recipe.extra_cflags.clear();
    recipe.extra_ldflags.clear();
    recipe.make_build_targets.clear();
    recipe.make_install_targets.clear();

    for (const auto& s : raw_conf) split_push(recipe.extra_conf_args, expand(s));
    for (const auto& s : raw_cflags) split_push(recipe.extra_cflags, expand(s));
    for (const auto& s : raw_ldflags) split_push(recipe.extra_ldflags, expand(s));
    for (const auto& s : raw_make_build) split_push(recipe.make_build_targets, expand(s));
    for (const auto& s : raw_make_inst) split_push(recipe.make_install_targets, expand(s));

    recipe.pre_configure_shell = expand(recipe.pre_configure_shell);
    recipe.build_override_shell = expand(recipe.build_override_shell);
    recipe.post_install_shell = expand(recipe.post_install_shell);
}

const RuneMatrixRecipe* RuneMatrixEngine::get_recipe(const std::string& pkg_name) {
    // 1. Try Binary DB first
    const RuneMatrixRecord* rec = find_record(pkg_name);
    if (rec && g_matrix_mapped) {
        static RuneMatrixRecipe binary_recipe;
        const RuneMatrixHeader *hdr = (const RuneMatrixHeader *)g_matrix_mapped;
        const char *strings = (const char *)g_matrix_mapped + hdr->string_pool_off;

        binary_recipe.package_name = strings + rec->pkg_name_offset;
        binary_recipe.build_system = (BuildSystem)rec->build_system;

        binary_recipe.extra_conf_args.clear();
        if (rec->extra_conf_off != 0) {
            binary_recipe.extra_conf_args.push_back(strings + rec->extra_conf_off);
        }

        binary_recipe.extra_cflags.clear();
        if (rec->extra_cflags_off != 0) {
            binary_recipe.extra_cflags.push_back(strings + rec->extra_cflags_off);
        }

        binary_recipe.extra_ldflags.clear();
        if (rec->extra_ldflags_off != 0) {
            binary_recipe.extra_ldflags.push_back(strings + rec->extra_ldflags_off);
        }

        binary_recipe.make_build_targets.clear();
        if (rec->make_build_off != 0) {
            binary_recipe.make_build_targets.push_back(strings + rec->make_build_off);
        }

        binary_recipe.make_install_targets.clear();
        if (rec->make_inst_off != 0) {
            binary_recipe.make_install_targets.push_back(strings + rec->make_inst_off);
        }

        binary_recipe.pre_configure_shell = (rec->pre_conf_off != 0) ? (strings + rec->pre_conf_off) : "";
        binary_recipe.build_override_shell = (rec->build_override_off != 0) ? (strings + rec->build_override_off) : "";
        binary_recipe.post_install_shell = (rec->post_inst_off != 0) ? (strings + rec->post_inst_off) : "";

        // Expansion needs a matrix. Use active profile if available.
        if (g_active_profile) {
            ProfileMatrix m = parse_profile(g_active_state.active_target ? g_active_state.active_target : "");
            expand_recipe_vars(binary_recipe, m);
        }

        return &binary_recipe;
    }

    return nullptr;
}
