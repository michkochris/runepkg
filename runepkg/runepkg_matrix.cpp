/*****************************************************************************
 * Filename:    runepkg_matrix.cpp
 * Author:      <michkochris@gmail.com>
 * Date:        2026-08-29
 * Description: Target Profile Matrix Engine & Declarative Package Recipes
 * LICENSE:     GPL v3
 ******************************************************************************/

#include "runepkg_matrix.h"
#include <unordered_map>
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

struct RuleSource {
    std::string pkg_name;
    uint32_t pkg_name_hash;
    uint16_t libc_mask;
    uint16_t arch_mask;
    uint32_t rule_flags;
    uint8_t  build_system;
    std::string extra_conf;
    std::string extra_cflags;
    std::string extra_ldflags;
    std::string make_build;
    std::string make_inst;
    std::string pre_conf_hook;
    std::string build_over_hook;
    std::string post_inst_hook;
};

class RuleCompiler {
public:
    RuleCompiler(const std::string& source_dir, const std::string& output_bin)
        : source_dir_(source_dir), output_bin_(output_bin) {}

    bool compile() {
        std::vector<RuleSource> rules;
        if (!scan_directory(source_dir_, rules)) {
            std::cerr << "ERROR: Failed to scan rules directory: " << source_dir_ << std::endl;
            return false;
        }

        if (rules.empty()) {
            std::cout << "Warning: No rules found in " << source_dir_ << std::endl;
        }

        return write_binary(rules);
    }

private:
    std::string source_dir_;
    std::string output_bin_;

    bool scan_directory(const fs::path& dir, std::vector<RuleSource>& rules) {
        if (!fs::exists(dir)) return false;

        for (const auto& entry : fs::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".txt") {
                RuleSource rule;
                if (parse_rule_file(entry.path(), rule)) {
                    rules.push_back(rule);
                }
            }
        }
        return true;
    }

    uint16_t parse_libc_mask(const char* val) {
        if (!val) return TARGET_LIBC_ANY;
        std::string s(val);
        uint16_t mask = 0;
        if (s.find("musl") != std::string::npos) mask |= TARGET_LIBC_MUSL;
        if (s.find("glibc") != std::string::npos) mask |= TARGET_LIBC_GLIBC;
        if (mask == 0) return TARGET_LIBC_ANY;
        return mask;
    }

    uint16_t parse_arch_mask(const char* val) {
        if (!val) return TARGET_ARCH_ANY;
        std::string s(val);
        uint16_t mask = 0;
        if (s.find("x86_64") != std::string::npos) mask |= TARGET_ARCH_X86_64;
        if (s.find("aarch64") != std::string::npos) mask |= TARGET_ARCH_AARCH64;
        if (s.find("armv7") != std::string::npos) mask |= TARGET_ARCH_ARMV7;
        if (s.find("riscv64") != std::string::npos) mask |= TARGET_ARCH_RISCV64;
        if (mask == 0) return TARGET_ARCH_ANY;
        return mask;
    }

    uint32_t parse_rule_flags(const char* val) {
        if (!val) return 0;
        std::string s(val);
        uint32_t flags = 0;
        if (s.find("FORCE_STATIC") != std::string::npos) flags |= RULE_FLAG_FORCE_STATIC;
        if (s.find("DISABLE_LTO") != std::string::npos) flags |= RULE_FLAG_DISABLE_LTO;
        if (s.find("INJECT_FTS_SHIM") != std::string::npos) flags |= RULE_FLAG_INJECT_FTS_SHIM;
        if (s.find("INJECT_ARG_MAX") != std::string::npos) flags |= RULE_FLAG_INJECT_ARG_MAX;
        if (s.find("NO_SHARED_LIBS") != std::string::npos) flags |= RULE_FLAG_NO_SHARED_LIBS;
        if (s.find("OVERRIDE_AUTOTOOLS") != std::string::npos) flags |= RULE_FLAG_OVERRIDE_AUTOTOOLS;
        if (s.find("NEEDS_PTHREAD") != std::string::npos) flags |= RULE_FLAG_NEEDS_PTHREAD;
        return flags;
    }

    uint8_t parse_build_system(const char* val) {
        if (!val) return 0; // Default to Autotools
        std::string s(val);
        if (s.find("cmake") != std::string::npos || s.find("CMAKE") != std::string::npos) return 1;
        if (s.find("meson") != std::string::npos || s.find("MESON") != std::string::npos) return 2;
        if (s.find("make") != std::string::npos || s.find("MAKE") != std::string::npos) return 3;
        return 0;
    }

    bool parse_rule_file(const fs::path& path, RuleSource& rule) {
        char* pkg = runepkg_util_get_config_value(path.c_str(), "package", '=');
        if (!pkg) {
            // Fallback to filename if 'package' key is missing
            rule.pkg_name = path.stem().string();
        } else {
            rule.pkg_name = pkg;
            free(pkg);
        }

        rule.pkg_name_hash = fnv1a_hash_local(rule.pkg_name.c_str());

        char* libc = runepkg_util_get_config_value(path.c_str(), "libc", '=');
        rule.libc_mask = parse_libc_mask(libc);
        free(libc);

        char* arch = runepkg_util_get_config_value(path.c_str(), "arch", '=');
        rule.arch_mask = parse_arch_mask(arch);
        free(arch);

        char* flags = runepkg_util_get_config_value(path.c_str(), "flags", '=');
        rule.rule_flags = parse_rule_flags(flags);
        free(flags);

        char* bsys = runepkg_util_get_config_value(path.c_str(), "build_system", '=');
        rule.build_system = parse_build_system(bsys);
        free(bsys);

        char* conf = runepkg_util_get_config_value(path.c_str(), "configure_args", '=');
        if (conf) { rule.extra_conf = conf; free(conf); }

        char* cflags = runepkg_util_get_config_value(path.c_str(), "cflags", '=');
        if (cflags) { rule.extra_cflags = cflags; free(cflags); }

        char* ldflags = runepkg_util_get_config_value(path.c_str(), "ldflags", '=');
        if (ldflags) { rule.extra_ldflags = ldflags; free(ldflags); }

        char* mbuild = runepkg_util_get_config_value(path.c_str(), "make_build_targets", '=');
        if (mbuild) { rule.make_build = mbuild; free(mbuild); }

        char* minst = runepkg_util_get_config_value(path.c_str(), "make_install_targets", '=');
        if (minst) { rule.make_inst = minst; free(minst); }

        char* preconf = runepkg_util_get_config_value(path.c_str(), "pre_configure", '=');
        if (preconf) { rule.pre_conf_hook = preconf; free(preconf); }

        char* buildover = runepkg_util_get_config_value(path.c_str(), "build_override", '=');
        if (buildover) { rule.build_over_hook = buildover; free(buildover); }

        char* postinst = runepkg_util_get_config_value(path.c_str(), "post_install", '=');
        if (postinst) { rule.post_inst_hook = postinst; free(postinst); }

        return true;
    }

    bool write_binary(const std::vector<RuleSource>& rules) {
        std::ofstream out(output_bin_, std::ios::binary);
        if (!out) return false;

        uint32_t bucket_count = (rules.size() * 2) + 1; // Simplistic hash table sizing
        uint32_t header_size = sizeof(RuneMatrixHeader);
        uint32_t buckets_size = bucket_count * sizeof(uint32_t);
        uint32_t records_offset = header_size + buckets_size;

        RuneMatrixHeader hdr;
        hdr.magic = RUNE_MATRIX_MAGIC;
        hdr.version = RUNE_MATRIX_VERSION;
        hdr.entry_size = sizeof(RuneMatrixRecord);
        hdr.bucket_count = bucket_count;
        hdr.record_count = rules.size();

        // We'll calculate string_pool_off after writing records

        std::vector<uint32_t> buckets(bucket_count, 0);
        std::vector<RuneMatrixRecord> records;
        std::string string_pool = "";

        auto add_string = [&](const std::string& s) -> uint32_t {
            if (s.empty()) return 0;
            uint32_t off = string_pool.length();
            string_pool += s;
            string_pool += '\0';
            return off;
        };

        for (const auto& r : rules) {
            RuneMatrixRecord rec;
            memset(&rec, 0, sizeof(rec));
            rec.pkg_name_hash = r.pkg_name_hash;
            rec.pkg_name_offset = add_string(r.pkg_name);
            rec.libc_mask = r.libc_mask;
            rec.arch_mask = r.arch_mask;
            rec.rule_flags = r.rule_flags;
            rec.build_system = r.build_system;
            rec.extra_conf_off = add_string(r.extra_conf);
            rec.extra_cflags_off = add_string(r.extra_cflags);
            rec.extra_ldflags_off = add_string(r.extra_ldflags);
            rec.make_build_off = add_string(r.make_build);
            rec.make_inst_off = add_string(r.make_inst);
            rec.pre_conf_off = add_string(r.pre_conf_hook);
            rec.build_override_off = add_string(r.build_over_hook);
            rec.post_inst_off = add_string(r.post_inst_hook);

            uint32_t bucket_idx = r.pkg_name_hash % bucket_count;
            rec.next_record_off = buckets[bucket_idx];

            uint32_t current_rec_off = records_offset + (records.size() * sizeof(RuneMatrixRecord));
            buckets[bucket_idx] = current_rec_off;

            records.push_back(rec);
        }

        hdr.string_pool_off = records_offset + (records.size() * sizeof(RuneMatrixRecord));

        out.write((char*)&hdr, sizeof(hdr));
        out.write((char*)buckets.data(), buckets_size);
        out.write((char*)records.data(), records.size() * sizeof(RuneMatrixRecord));
        out.write(string_pool.data(), string_pool.length());

        out.close();
        std::cout << "\033[1;32m[compiler]\033[0m Matrix DB generated: " << output_bin_
                  << " (" << rules.size() << " rules, " << hdr.string_pool_off + string_pool.length() << " bytes)" << std::endl;
        return true;
    }
};

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

    RuleCompiler compiler(SYSTEM_RULES_DIR, bin_path);
    bool success = compiler.compile();

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
