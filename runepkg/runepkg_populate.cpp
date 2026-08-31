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

int main(int argc, char* argv[]) {
    // Default to project-relative paths
    std::string db_dir = "runepkg_db";
    std::string target_rules_dir = "target-rules";

    if (argc > 1) target_rules_dir = argv[1];
    if (argc > 2) db_dir = argv[2];

    if (!fs::exists(db_dir)) {
        // Try absolute path if relative fails
        db_dir = "/var/lib/runepkg_dir/runepkg_db";
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
