/*****************************************************************************
 * Filename:    runepkg_populate.cpp
 * Author:      <michkochris@gmail.com>
 * Date:        2026-08-31
 * Description: Populates target-rules/ with canonical source
 *              recipes and creates relative symlinks for all binary aliases.
 *              Targeted at the project source tree for JIT enhancement.
 * LICENSE:     GPL v3
 ******************************************************************************/

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>
#include <algorithm>
#include <cctype>

extern "C" {
#include "runepkg_config.h"
#include "runepkg_util.h"
}

// Satisfy external symbols from runepkg_util.o and other objects
extern "C" {
    bool g_verbose_mode = false;
    bool g_debug_mode = false;
    bool g_auto_confirm_deps = false;
    bool g_auto_confirm_siblings = false;
    bool g_asked_siblings = false;
    bool g_completion_mode = false;
}

namespace fs = std::filesystem;

static std::string get_debian_prefix(const std::string& pkg_name) {
    if (pkg_name.empty()) return "0-9";
    std::string lower = pkg_name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.rfind("lib", 0) == 0 && lower.length() >= 4) {
        return lower.substr(0, 4);
    }
    if (std::isdigit(static_cast<unsigned char>(lower[0]))) return "0-9";
    if (std::isalpha(static_cast<unsigned char>(lower[0]))) return std::string(1, lower[0]);
    return "0-9";
}

struct SourceRecord {
    std::string source_name;
    std::set<std::string> binaries_produced;
};

static std::map<std::string, SourceRecord> harvest_sources_with_binaries(const std::string& db_dir) {
    std::map<std::string, SourceRecord> source_map;

    std::string src_list_file = db_dir + "/repo_src_files.txt";
    std::ifstream flist(src_list_file);
    std::vector<std::string> file_paths;

    if (flist.is_open()) {
        std::string line;
        while (std::getline(flist, line)) {
            if (!line.empty()) {
                if (line.back() == '\r') line.pop_back();
                file_paths.push_back(line);
            }
        }
        flist.close();
    }

    for (const auto& path : file_paths) {
        std::ifstream src_file(path);
        if (!src_file.is_open()) continue;

        std::string line;
        std::string current_src;
        std::string current_binaries;

        while (std::getline(src_file, line)) {
            if (line.empty() || line == "\r") {
                if (!current_src.empty()) {
                    SourceRecord& rec = source_map[current_src];
                    rec.source_name = current_src;

                    // Parse comma-separated Binary: list
                    if (!current_binaries.empty()) {
                        std::stringstream ss(current_binaries);
                        std::string bin;
                        while (std::getline(ss, bin, ',')) {
                            bin.erase(0, bin.find_first_not_of(" \t"));
                            bin.erase(bin.find_last_not_of(" \t\r\n") + 1);
                            if (!bin.empty() && bin != current_src) {
                                rec.binaries_produced.insert(bin);
                            }
                        }
                    }
                }
                current_src.clear();
                current_binaries.clear();
                continue;
            }

            if (line.compare(0, 9, "Package: ") == 0) {
                current_src = line.substr(9);
                while (!current_src.empty() && (current_src.back() == '\r' || current_src.back() == ' ')) {
                    current_src.pop_back();
                }
            } else if (line.compare(0, 8, "Binary: ") == 0) {
                current_binaries = line.substr(8);
            }
        }
    }

    return source_map;
}

static std::string generate_draft_template(const std::string& pkg_name) {
    std::string tmpl;
    tmpl += "# runepkg target-rule draft template\n";
    tmpl += "[package]\n";
    tmpl += "name = " + pkg_name + "\n";
    tmpl += "build_system = autotools\n\n";
    tmpl += "[flags]\n";
    tmpl += "cflags = -O2 -D_FILE_OFFSET_BITS=64\n";
    tmpl += "cxxflags = -O2\n";
    tmpl += "ldflags =\n\n";
    tmpl += "[autotools]\n";
    tmpl += "conf_args = --prefix=/usr --sysconfdir=/etc --localstatedir=/var\n\n";
    tmpl += "[make]\n";
    tmpl += "targets = all\n";
    tmpl += "install_targets = install\n\n";
    tmpl += "[hooks]\n";
    tmpl += "pre_configure = sanitize_timestamps\n";
    return tmpl;
}

static void usage(const char* prog) {
    std::cout << "\033[1;36mrunepkg_populate\033[0m - Canonical Rule Scaffolder\n"
              << "Usage: " << prog << " [OPTIONS] [TARGET_RULES_DIR]\n\n"
              << "Options:\n"
              << "  --confirm               Mandatory flag to acknowledge and start scaffold generation\n"
              << "  --db-dir=<dir>          Path to runepkg_db (default: from config)\n"
              << "  -h, --help              Display this help menu\n\n"
              << "Description:\n"
              << "  This tool scans repository metadata and creates thousands of package\n"
              << "  rule templates in the target-rules directory. It is a high-volume\n"
              << "  operation that should only be run when initializing or refreshing\n"
              << "  the rule database.\n\n"
              << "Example:\n"
              << "  " << prog << " --confirm /etc/runepkg/target-rules\n";
}

int main(int argc, char* argv[]) {
    // Initialize runepkg configuration
    runepkg_init_paths();
    runepkg_config_load();

    bool confirmed = false;
    std::string db_dir = g_runepkg_db_dir ? g_runepkg_db_dir : "runepkg_db";
    std::string target_rules_dir = "/etc/runepkg/target-rules"; // Default canonical location

    // Try to resolve target-rules relative to base dir if possible
    if (g_runepkg_base_dir) {
        fs::path base(g_runepkg_base_dir);
        fs::path rules = base / "target-rules";
        if (fs::exists(rules)) {
            target_rules_dir = rules.string();
        }
    }

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--confirm") confirmed = true;
        else if (arg == "-h" || arg == "--help") { usage(argv[0]); return 0; }
        else if (arg.rfind("--db-dir=", 0) == 0) db_dir = arg.substr(9);
        else if (arg[0] != '-') target_rules_dir = arg;
    }

    if (!confirmed) {
        std::cerr << "\033[1;31m[safety]\033[0m No --confirm flag provided. Aborting high-volume operation.\n";
        usage(argv[0]);
        return 1;
    }

    std::cout << "\033[1;34m[scaffold]\033[0m Scanning sources and binary maps from " << db_dir << "..." << std::endl;
    auto source_map = harvest_sources_with_binaries(db_dir);

    if (source_map.empty()) {
        std::cerr << "\033[1;31m[error]\033[0m No sources found. Please run 'runepkg update' first." << std::endl;
        return 1;
    }

    std::cout << "  -> Found \033[1;32m" << source_map.size() << "\033[0m source package definitions." << std::endl;

    size_t sources_created = 0;
    size_t aliases_linked = 0;

    for (const auto& [src_name, rec] : source_map) {
        std::string src_prefix = get_debian_prefix(src_name);
        fs::path src_dir = fs::path(target_rules_dir) / src_prefix / src_name;
        fs::path src_file = src_dir / "build.txt";

        try {
            fs::create_directories(src_dir);
            if (!fs::exists(src_file)) {
                std::ofstream out(src_file);
                if (out.is_open()) {
                    out << generate_draft_template(src_name);
                    out.close();
                    sources_created++;
                }
            }

            // Create relative symlinks for every binary package produced by this source
            for (const auto& bin_name : rec.binaries_produced) {
                std::string bin_prefix = get_debian_prefix(bin_name);
                fs::path bin_dir = fs::path(target_rules_dir) / bin_prefix / bin_name;
                fs::create_directories(bin_dir);

                fs::path alias_link = bin_dir / "build.txt";
                if (!fs::exists(alias_link) && !fs::is_symlink(alias_link)) {
                    // Compute relative path back to the canonical source build.txt
                    fs::path rel_target = fs::relative(src_file, bin_dir);
                    std::error_code ec;
                    fs::create_symlink(rel_target, alias_link, ec);
                    if (!ec) aliases_linked++;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "  -> \033[1;31m[error]\033[0m " << e.what() << std::endl;
        }
    }

    std::cout << "\033[1;32m[success]\033[0m Scaffold generation complete." << std::endl;
    std::cout << "  -> Canonical source rules created: " << sources_created << std::endl;
    std::cout << "  -> Binary alias symlinks created:   " << aliases_linked << std::endl;

    return 0;
}
