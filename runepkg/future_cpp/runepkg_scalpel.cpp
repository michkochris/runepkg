/*****************************************************************************
 * Filename:    runepkg_scalpel.cpp
 * Author:      <michkochris@gmail.com>
 * Date:        2026-08-31
 * Description: Multi-Field Scalpel & Rule Mitigator for runepkg
 *              Surgically filters packages by language, profile, build system,
 *              or section, and generates custom @pkglist manifests.
 * LICENSE:     GPL v3
 ******************************************************************************/

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <filesystem>
#include <algorithm>
#include <regex>
#include <cstring>

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

struct PkgMetadata {
    std::string name;
    std::string section;
    std::string priority;
    std::string build_deps;
    std::string standards_version;
    std::vector<std::string> binaries;
};

struct FilterOptions {
    std::string db_dir;
    std::string target_rules_dir;
    std::string out_file = "curated_targets.txt";

    FilterOptions() {
        // Strictly honor professional registry settings for the database
        if (g_runepkg_db_dir) {
            db_dir = g_runepkg_db_dir;
        } else {
            db_dir = "./runepkg_db";
        }

        // Default to the professional system-wide rules path
        target_rules_dir = "/etc/runepkg/target-rules";
    }

    // Filtering Flags
    bool profile_embedded_c = false;    // Pure C/C++ (drops Python, Rust, Go, Java, Haskell)
    bool profile_base_posix = false;     // Essential base POSIX core tools only
    bool profile_bootstrap_dev = false;  // Base POSIX + Toolchain + Core Libs
    bool filter_autotools_only = false;  // Packages strictly using dh-autoreconf/autoconf
    bool filter_cmake_only = false;      // Packages using cmake
    bool filter_meson_only = false;      // Packages using meson
    bool include_libs = true;            // Include library packages
    bool prune_rules_tree = false;       // Delete non-matching directories from target-rules/

    std::vector<std::string> match_sections;
    std::vector<std::string> match_names;
    std::vector<std::string> exclude_names;
};

class ScalpelEngine {
public:
    static bool is_banned_runtime(const std::string& name, const std::string& bdep) {
        static const std::vector<std::string> tokens = {
            "python", "python3", "libghc-", "golang-", "rust-", "cargo",
            "node-", "nodejs", "ruby-", "perl-", "openjdk", "default-jdk",
            "elpa-", "texlive-", "fonts-", "gir1.2-", "gnome-", "plasma-",
            "qt5-", "qt6-", "kf5-", "kf6-", "octave-", "r-cran-", "erlang-",
            "ocaml-", "gambas3-", "php-", "tcl-", "tk-", "guile-"
        };
        for (const auto& t : tokens) {
            if (name.find(t) != std::string::npos) return true;
        }

        static const std::vector<std::string> bdeps = {
            "ghc", "rustc", "cargo", "golang-go", "default-jdk", "gnome-pkg-tools",
            "dh-python", "dh-golang", "dh-cargo", "gem2deb", "dh-ocaml", "dh-elpa",
            "dh-r", "dh-php"
        };
        for (const auto& b : bdeps) {
            if (bdep.find(b) != std::string::npos) return true;
        }
        return false;
    }

    static bool is_base_posix(const std::string& name, const std::string& prio) {
        if (prio == "required" || prio == "important" || prio == "standard") return true;
        static const std::set<std::string> core_pkgs = {
            "coreutils", "bash", "dash", "grep", "sed", "gawk", "diffutils",
            "findutils", "tar", "gzip", "bzip2", "xz-utils", "file", "make",
            "patch", "ncurses", "util-linux", "shadow", "e2fsprogs", "kmod"
        };
        return core_pkgs.count(name) > 0;
    }

    static bool evaluate(const PkgMetadata& pkg, const FilterOptions& opt) {
        // Profile: Bootstrap Dev (LFS-style minimal toolchain)
        if (opt.profile_bootstrap_dev) {
            // 1. Always include Base POSIX
            if (is_base_posix(pkg.name, pkg.priority)) return true;

            // 2. Reject Bloat
            if (is_banned_runtime(pkg.name, pkg.build_deps)) return false;

            // 3. Include Toolchain & Essential Build Sections
            static const std::set<std::string> dev_sections = {
                "devel", "libdevel", "libs", "base", "admin", "utils"
            };

            // Heuristic for core toolchain and dev headers
            bool is_dev = (pkg.section.find("devel") != std::string::npos ||
                           pkg.section.find("libdevel") != std::string::npos);

            // Keep only if it's in a relevant section and not banned
            if (dev_sections.count(pkg.section) > 0 || is_dev) {
                // Additional filter: avoid GUI/X11 bloat in bootstrap
                if (pkg.name.find("x11") != std::string::npos ||
                    pkg.name.find("wayland") != std::string::npos ||
                    pkg.name.find("gtk") != std::string::npos ||
                    pkg.name.find("qt") != std::string::npos) return false;

                return true;
            }
            return false;
        }

        // Name Exclusions
        for (const auto& ex : opt.exclude_names) {
            if (pkg.name.find(ex) != std::string::npos) return false;
        }

        // Name Inclusions (if specified)
        if (!opt.match_names.empty()) {
            bool matched = false;
            for (const auto& m : opt.match_names) {
                if (pkg.name.find(m) != std::string::npos) { matched = true; break; }
            }
            if (!matched) return false;
        }

        // Section Matching
        if (!opt.match_sections.empty()) {
            bool sec_matched = false;
            for (const auto& s : opt.match_sections) {
                if (pkg.section.find(s) != std::string::npos) { sec_matched = true; break; }
            }
            if (!sec_matched) return false;
        }

        // Profile: Base POSIX
        if (opt.profile_base_posix && !is_base_posix(pkg.name, pkg.priority)) {
            return false;
        }

        // Profile: Embedded C/C++
        if (opt.profile_embedded_c && is_banned_runtime(pkg.name, pkg.build_deps)) {
            return false;
        }

        // Build System Archetypes
        if (opt.filter_autotools_only) {
            if (pkg.build_deps.find("autoconf") == std::string::npos &&
                pkg.build_deps.find("automake") == std::string::npos &&
                pkg.build_deps.find("dh-autoreconf") == std::string::npos) {
                return false;
            }
        }
        if (opt.filter_cmake_only && pkg.build_deps.find("cmake") == std::string::npos) {
            return false;
        }
        if (opt.filter_meson_only && pkg.build_deps.find("meson") == std::string::npos) {
            return false;
        }

        // Libraries Filter
        if (!opt.include_libs && (pkg.section.find("libs") != std::string::npos || pkg.name.rfind("lib", 0) == 0)) {
            return false;
        }

        return true;
    }
};

static std::map<std::string, PkgMetadata> load_sources(const std::string& db_dir) {
    std::map<std::string, PkgMetadata> results;
    std::string src_list_file = db_dir + "/repo_src_files.txt";
    std::ifstream flist(src_list_file);
    if (!flist.is_open()) return results;

    std::string line;
    std::vector<std::string> file_paths;
    while (std::getline(flist, line)) {
        if (!line.empty()) {
            if (line.back() == '\r') line.pop_back();
            file_paths.push_back(line);
        }
    }

    for (const auto& path : file_paths) {
        std::ifstream src_file(path);
        if (!src_file.is_open()) continue;

        PkgMetadata current;
        while (std::getline(src_file, line)) {
            if (line.empty() || line == "\r") {
                if (!current.name.empty()) {
                    results[current.name] = current;
                }
                current = PkgMetadata();
                continue;
            }

            if (line.compare(0, 9, "Package: ") == 0) {
                current.name = line.substr(9);
                current.name.erase(current.name.find_last_not_of(" \r\t\n") + 1);
            } else if (line.compare(0, 9, "Section: ") == 0) {
                current.section = line.substr(9);
                current.section.erase(current.section.find_last_not_of(" \r\t\n") + 1);
            } else if (line.compare(0, 10, "Priority: ") == 0) {
                current.priority = line.substr(10);
                current.priority.erase(current.priority.find_last_not_of(" \r\t\n") + 1);
            } else if (line.compare(0, 15, "Build-Depends: ") == 0 ||
                       line.compare(0, 20, "Build-Depends-Arch: ") == 0) {
                size_t colon = line.find(':');
                current.build_deps += " " + line.substr(colon + 1);
            }
        }
    }
    return results;
}

static void usage(const char* prog) {
    std::cout << "\033[1;36mrunepkg_scalpel\033[0m - Professional Package Filter & Rules Pruner\n"
              << "Usage: " << prog << " [OPTIONS]\n\n"
              << "Description:\n"
              << "  A professional tool for surgical repo management. It prioritizes\n"
              << "  the system configuration registry to find the active runepkg_db.\n\n"
              << "Profiles & Domains:\n"
              << "  --bootstrap-dev         Surgically isolate Debian-style Bootstrap + Minimal Dev tools\n"
              << "  --embedded-c            Keep only pure C/C++ sources (drop Python, Rust, Go, Java, etc.)\n"
              << "  --posix-base            Surgically isolate required/standard POSIX base OS packages\n"
              << "  --no-libs               Exclude standalone library sources\n\n"
              << "Build System Scalpels:\n"
              << "  --autotools             Match only packages utilizing Autotools / autoreconf\n"
              << "  --cmake                 Match only packages utilizing CMake\n"
              << "  --meson                 Match only packages utilizing Meson / Ninja\n\n"
              << "Field Filters:\n"
              << "  --section=<sec>         Filter by Debian section (e.g., utils, admin, net, editors)\n"
              << "  --match=<pattern>       Include package names containing pattern\n"
              << "  --exclude=<pattern>     Exclude package names containing pattern\n\n"
              << "Actions & Output:\n"
              << "  -o, --output=<file>     Output file path for curated @pkglist (default: curated_targets.txt)\n"
              << "  --prune-rules           Prune non-matching package folders from target-rules/\n"
              << "  --rules-dir=<dir>       Path to target-rules tree (default: /etc/runepkg/target-rules)\n"
              << "  --db-dir=<dir>          Path to runepkg_db (default: resolved via Registry)\n"
              << "  -h, --help              Display this help menu\n\n"
              << "Example:\n"
              << "  " << prog << " --embedded-c --autotools --prune-rules -o clean_rules.txt\n";
}

int main(int argc, char* argv[]) {
    // Initialize runepkg configuration
    runepkg_init_paths();
    runepkg_config_load();

    if (argc == 1) {
        usage(argv[0]);
        return 0;
    }

    FilterOptions opt;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--bootstrap-dev") opt.profile_bootstrap_dev = true;
        else if (arg == "--embedded-c") opt.profile_embedded_c = true;
        else if (arg == "--posix-base") opt.profile_base_posix = true;
        else if (arg == "--autotools") opt.filter_autotools_only = true;
        else if (arg == "--cmake") opt.filter_cmake_only = true;
        else if (arg == "--meson") opt.filter_meson_only = true;
        else if (arg == "--no-libs") opt.include_libs = false;
        else if (arg == "--prune-rules") opt.prune_rules_tree = true;
        else if (arg.rfind("--section=", 0) == 0) opt.match_sections.push_back(arg.substr(10));
        else if (arg.rfind("--match=", 0) == 0) opt.match_names.push_back(arg.substr(8));
        else if (arg.rfind("--exclude=", 0) == 0) opt.exclude_names.push_back(arg.substr(10));
        else if (arg.rfind("--output=", 0) == 0) opt.out_file = arg.substr(9);
        else if (arg == "-o" && i + 1 < argc) opt.out_file = argv[++i];
        else if (arg.rfind("--rules-dir=", 0) == 0) opt.target_rules_dir = arg.substr(12);
        else if (arg.rfind("--db-dir=", 0) == 0) opt.db_dir = arg.substr(9);
        else if (arg == "-h" || arg == "--help") { usage(argv[0]); return 0; }
        else {
            std::cerr << "Unknown option: " << arg << " (see --help)\n";
            return 1;
        }
    }

    std::cout << "\033[1;34m[scalpel]\033[0m Ingesting source database from " << opt.db_dir << "...\n";
    auto all_sources = load_sources(opt.db_dir);
    if (all_sources.empty()) {
        std::cerr << "\033[1;31m[error]\033[0m No source packages found. Run 'runepkg update' first.\n";
        return 1;
    }

    std::cout << "  -> Scanned \033[1;32m" << all_sources.size() << "\033[0m upstream source stanzas.\n";
    std::cout << "\033[1;34m[scalpel]\033[0m Applying field filters & scalpels...\n";

    std::set<std::string> curated;
    for (const auto& [name, meta] : all_sources) {
        if (ScalpelEngine::evaluate(meta, opt)) {
            curated.insert(name);
        }
    }

    std::cout << "  -> Retained \033[1;32m" << curated.size() << "\033[0m matching packages.\n";

    // 1. Output Manifest File
    std::ofstream out(opt.out_file);
    if (out.is_open()) {
        for (const auto& pkg : curated) out << pkg << "\n";
        out.close();
        std::cout << "\033[1;32m[success]\033[0m Manifest written to: " << opt.out_file << "\n";
    }

    // 2. Optional In-Place Rule Tree Prune
    if (opt.prune_rules_tree && fs::exists(opt.target_rules_dir)) {
        std::cout << "\033[1;34m[prune]\033[0m Surgically cleaning target-rules at " << opt.target_rules_dir << "...\n";
        size_t pruned = 0;
        for (const auto& entry : fs::recursive_directory_iterator(opt.target_rules_dir)) {
            if (entry.is_directory()) {
                std::string dirname = entry.path().filename().string();
                if (dirname.length() > 3 && curated.find(dirname) == curated.end()) {
                    try {
                        fs::remove_all(entry.path());
                        pruned++;
                    } catch (...) {}
                }
            }
        }
        std::cout << "  -> Pruned \033[1;33m" << pruned << "\033[0m extraneous package rule directories.\n";
    }

    return 0;
}
