/*****************************************************************************
 * Filename:    runepkg_compiler.cpp
 * Author:      <michkochris@gmail.com>
 * Date:        2026-08-31
 * Description: Optimized JIT Target Profile Rule Compiler
 * LICENSE:     GPL v3
 ******************************************************************************/

#include "runepkg_matrix.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>
#include <map>
#include <cstring>
#include <algorithm>

extern "C" {
    #include "runepkg_config.h"
    #include "runepkg_util.h"
}

namespace fs = std::filesystem;

/* Standard 32-bit FNV-1a Hash */
static uint32_t fnv1a_hash(const char *str) {
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

class OptimizedRuleCompiler {
public:
    OptimizedRuleCompiler(const std::string& source_dir, const std::string& output_bin)
        : source_dir_(source_dir), output_bin_(output_bin) {}

    bool compile() {
        std::vector<RuleSource> rules;
        if (!scan_directory(source_dir_, rules)) {
            std::cerr << "ERROR: Failed to scan rules directory: " << source_dir_ << std::endl;
            return false;
        }

        if (rules.empty()) {
            std::cout << "\033[1;33m[warning]\033[0m No rules found in " << source_dir_ << std::endl;
            // We still write an empty header if directory exists but is empty
            return write_binary(rules);
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

    uint16_t parse_libc_mask(const std::string& s) {
        if (s.empty() || s == "any") return (uint16_t)TARGET_LIBC_ANY;
        uint16_t mask = 0;
        if (s.find("musl") != std::string::npos) mask |= (uint16_t)TARGET_LIBC_MUSL;
        if (s.find("glibc") != std::string::npos) mask |= (uint16_t)TARGET_LIBC_GLIBC;
        return (mask == 0) ? (uint16_t)TARGET_LIBC_ANY : mask;
    }

    uint16_t parse_arch_mask(const std::string& s) {
        if (s.empty() || s == "any") return (uint16_t)TARGET_ARCH_ANY;
        uint16_t mask = 0;
        if (s.find("x86_64") != std::string::npos) mask |= (uint16_t)TARGET_ARCH_X86_64;
        if (s.find("aarch64") != std::string::npos) mask |= (uint16_t)TARGET_ARCH_AARCH64;
        if (s.find("armv7") != std::string::npos) mask |= (uint16_t)TARGET_ARCH_ARMV7;
        if (s.find("riscv64") != std::string::npos) mask |= (uint16_t)TARGET_ARCH_RISCV64;
        return (mask == 0) ? (uint16_t)TARGET_ARCH_ANY : mask;
    }

    uint32_t parse_rule_flags(const std::string& s) {
        if (s.empty()) return 0;
        uint32_t flags = 0;
        if (s.find("FORCE_STATIC") != std::string::npos) flags |= (uint32_t)RULE_FLAG_FORCE_STATIC;
        if (s.find("DISABLE_LTO") != std::string::npos) flags |= (uint32_t)RULE_FLAG_DISABLE_LTO;
        if (s.find("INJECT_FTS_SHIM") != std::string::npos) flags |= (uint32_t)RULE_FLAG_INJECT_FTS_SHIM;
        if (s.find("INJECT_ARG_MAX") != std::string::npos) flags |= (uint32_t)RULE_FLAG_INJECT_ARG_MAX;
        if (s.find("NO_SHARED_LIBS") != std::string::npos) flags |= (uint32_t)RULE_FLAG_NO_SHARED_LIBS;
        if (s.find("OVERRIDE_AUTOTOOLS") != std::string::npos) flags |= (uint32_t)RULE_FLAG_OVERRIDE_AUTOTOOLS;
        if (s.find("NEEDS_PTHREAD") != std::string::npos) flags |= (uint32_t)RULE_FLAG_NEEDS_PTHREAD;
        return flags;
    }

    uint8_t parse_build_system(const std::string& s) {
        if (s.find("cmake") != std::string::npos) return 1;
        if (s.find("meson") != std::string::npos) return 2;
        if (s.find("make") != std::string::npos) return 3;
        return 0; // Default: Autotools
    }

    bool parse_rule_file(const fs::path& path, RuleSource& rule) {
        std::ifstream in(path);
        if (!in.is_open()) return false;

        std::map<std::string, std::string> kv;
        std::string line;
        while (std::getline(in, line)) {
            // Trim
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            size_t last = line.find_last_not_of(" \t\r\n");
            if (last != std::string::npos) line = line.substr(0, last + 1);

            if (line.empty() || line[0] == '#' || line[0] == '[') continue;

            size_t sep = line.find('=');
            if (sep != std::string::npos) {
                std::string key = line.substr(0, sep);
                std::string val = line.substr(sep + 1);

                // Trim key
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                // Trim value
                val.erase(0, val.find_first_not_of(" \t"));
                val.erase(val.find_last_not_of(" \t") + 1);

                kv[key] = val;
            }
        }

        if (kv.count("package")) rule.pkg_name = kv["package"];
        else if (kv.count("name")) rule.pkg_name = kv["name"];
        else rule.pkg_name = path.stem().string();

        rule.pkg_name_hash = fnv1a_hash(rule.pkg_name.c_str());
        rule.libc_mask = parse_libc_mask(kv["libc"]);
        rule.arch_mask = parse_arch_mask(kv["arch"]);
        rule.rule_flags = parse_rule_flags(kv["flags"]);
        rule.build_system = parse_build_system(kv["build_system"]);

        rule.extra_conf = kv.count("configure_args") ? kv["configure_args"] : (kv.count("conf_args") ? kv["conf_args"] : "");
        rule.extra_cflags = kv["cflags"];
        rule.extra_ldflags = kv["ldflags"];
        rule.make_build = kv.count("make_build_targets") ? kv["make_build_targets"] : (kv.count("targets") ? kv["targets"] : "");
        rule.make_inst = kv.count("make_install_targets") ? kv["make_install_targets"] : (kv.count("install_targets") ? kv["install_targets"] : "");
        rule.pre_conf_hook = kv["pre_configure"];
        rule.build_over_hook = kv["build_override"];
        rule.post_inst_hook = kv["post_install"];

        return true;
    }

    bool write_binary(const std::vector<RuleSource>& rules) {
        std::ofstream out(output_bin_, std::ios::binary);
        if (!out) return false;

        uint32_t bucket_count = (rules.size() * 2) + 1;
        uint32_t header_size = sizeof(RuneMatrixHeader);
        uint32_t buckets_size = bucket_count * sizeof(uint32_t);
        uint32_t records_offset = header_size + buckets_size;

        RuneMatrixHeader hdr;
        hdr.magic = RUNE_MATRIX_MAGIC;
        hdr.version = RUNE_MATRIX_VERSION;
        hdr.entry_size = sizeof(RuneMatrixRecord);
        hdr.bucket_count = bucket_count;
        hdr.record_count = rules.size();

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
        std::cout << "\033[1;32m[matrix]\033[0m JIT Rule Matrix optimized: " << output_bin_
                  << " (" << rules.size() << " rules, " << hdr.string_pool_off + string_pool.length() << " bytes)" << std::endl;
        return true;
    }
};

extern "C" bool runepkg_matrix_compile_rules(const char* rules_dir, const char* output_bin) {
    OptimizedRuleCompiler compiler(rules_dir, output_bin);
    return compiler.compile();
}
