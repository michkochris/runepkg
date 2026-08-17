/*****************************************************************************
 * Filename:    runepkg_building.cpp
 * Author:      <michkochris@gmail.com>
 * Date:        2025-05-12
 * Description: Pure C++ Debian Source Package Builder for runepkg
 * LICENSE:     GPL v3
 ******************************************************************************/

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

#include "runepkg_cpp_ffi.h"

// Bring in C globals/utils
extern "C" {
    #include "runepkg_config.h"
    #include "runepkg_util.h"
    #include "runepkg_handle.h"
    #include "runepkg_storage.h"
}

namespace fs = std::filesystem;

class SourceBuilder {
public:
    SourceBuilder(const std::string& dsc_path) : dsc_path_(dsc_path) {}

    int build(bool split = false) {
        // If dsc_path_ is actually a directory, treat it as an already-extracted source tree.
        if (fs::is_directory(dsc_path_)) {
            source_tree_root_ = dsc_path_;
            // Try to find a .dsc in the same parent dir just in case for metadata
            fs::path parent = fs::path(dsc_path_).parent_path();
            bool dsc_found = false;
            for (const auto& entry : fs::directory_iterator(parent)) {
                if (entry.path().extension() == ".dsc") {
                    std::string old_dsc = dsc_path_;
                    dsc_path_ = entry.path().string();
                    if (parse_dsc()) {
                        dsc_found = true;
                        break;
                    }
                    dsc_path_ = old_dsc;
                }
            }

            // If no .dsc found or parsed, try to get metadata from the tree itself
            if (!dsc_found) {
                if (!parse_metadata_from_tree()) {
                    std::cerr << "ERROR: Could not determine package metadata from directory " << dsc_path_ << std::endl;
                    return -1;
                }
            }

            // For a directory build, the "working dir" is the parent of the source tree
            working_dir_ = fs::path(dsc_path_).parent_path();

            std::cout << "\033[1;34m[build]\033[0m Building from source tree: " << source_name_ << " (" << version_ << ")" << std::endl;
        } else {
            if (unpack() != 0) return -1;
        }

        // --- NEW: Dependency Alchemy Check ---
        if (!build_depends_.empty()) {
            std::cout << "\033[1;34m[ritual]\033[0m Inspecting alchemical ingredients (build dependencies)..." << std::endl;
            char **deps = parse_depends(build_depends_.c_str());
            if (deps) {
                std::vector<std::string> missing;
                for (int i = 0; deps[i]; i++) {
                    // Check both system and runepkg database
                    if (runepkg_main_hash_table && !runepkg_hash_search(runepkg_main_hash_table, deps[i])) {
                        missing.push_back(deps[i]);
                    }
                    free(deps[i]);
                }
                free(deps);

                if (!missing.empty()) {
                    std::cout << "\033[1;33m[warning]\033[0m The following ingredients might be missing from your forge:" << std::endl;
                    for (const auto& m : missing) std::cout << "  - " << m << std::endl;
                    std::cout << "\033[1;33m[warning]\033[0m If the build fails, use 'runepkg download-build-depends " << source_name_ << "' to gather them." << std::endl;
                }
            }
        }

        // Try standard debian/rules first
        if (execute_rules()) {
            if (collect_results()) return 0;
        }

        // Fallback to Native Rune Build if rules failed or dh is missing
        std::cout << "\033[1;33m[build]\033[0m debian/rules failed or requires missing tools. Attempting Native Build..." << std::endl;
        if (build_native(split)) {
            if (collect_results()) return 0;
        }

        return -1;
    }

    int unpack() {
        if (!parse_dsc()) return -1;
        if (!setup_workspace()) return -1;

        // Efficiency: Skip extraction if source tree already exists
        fs::path debian_dir = working_dir_ / "debian";
        if (fs::exists(debian_dir)) {
            std::cout << "\033[1;32m[build]\033[0m Source already extracted at " << working_dir_ << ". Skipping extraction." << std::endl;
            find_source_root();
            return 0;
        }

        if (!extract_source()) return -1;
        return 0;
    }

private:
    std::string dsc_path_;
    std::string source_name_;
    std::string version_;
    std::string build_depends_;
    std::vector<std::string> source_files_;
    fs::path working_dir_;
    fs::path source_tree_root_;

    void find_source_root() {
        if (!fs::exists(working_dir_)) return;

        for (const auto& entry : fs::directory_iterator(working_dir_)) {
            if (entry.is_directory()) {
                std::string name = entry.path().filename().string();
                if (name == "debian") {
                    source_tree_root_ = working_dir_;
                    return;
                }
                if (name.find(source_name_) != std::string::npos && fs::exists(entry.path() / "debian")) {
                    source_tree_root_ = entry.path();
                    return;
                }
            }
        }
        source_tree_root_ = working_dir_;
    }

    struct BinaryPackage {
        std::string name;
        std::vector<std::string> control_lines;
        bool version_found = false;
    };

    bool build_native(bool split) {
        std::cout << "\033[1;34m[build]\033[0m Starting Native C++ Build workflow..." << std::endl;

        fs::path temp_install_dir = working_dir_ / "temp_install";
        try {
            fs::create_directories(temp_install_dir);
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Failed to create temp install area: " << e.what() << std::endl;
            return false;
        }

        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd))) return false;
        if (chdir(source_tree_root_.c_str()) != 0) return false;

        // 1. Configure
        if (fs::exists("configure")) {
            std::cout << "  -> Running ./configure --prefix=/usr ..." << std::endl;
            char* argv[] = {(char*)"./configure", (char*)"--prefix=/usr", NULL};
            if (runepkg_util_execute_command("./configure", argv) != 0) {
                std::cerr << "ERROR: Configure failed" << std::endl;
                chdir(cwd);
                return false;
            }
        }

        // 2. Build
        std::cout << "  -> Running make ..." << std::endl;
        char* argv_make[] = {(char*)"make", NULL};
        if (runepkg_util_execute_command("make", argv_make) != 0) {
            std::cerr << "ERROR: Make failed" << std::endl;
            chdir(cwd);
            return false;
        }

        // 3. Install to temp area
        std::cout << "  -> Running make install DESTDIR=" << temp_install_dir << " ..." << std::endl;
        std::string dest_arg = "DESTDIR=" + temp_install_dir.string();
        char* argv_install[] = {(char*)"make", (char*)"install", (char*)dest_arg.c_str(), NULL};
        if (runepkg_util_execute_command("make", argv_install) != 0) {
            std::cerr << "ERROR: Install to temp area failed" << std::endl;
            chdir(cwd);
            return false;
        }

        chdir(cwd);

        // 4. Parse debian/control for binary packages
        std::vector<BinaryPackage> packages;
        fs::path src_control = source_tree_root_ / "debian" / "control";
        if (fs::exists(src_control)) {
            std::ifstream in(src_control);
            std::string line;
            BinaryPackage* current_pkg = nullptr;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();

                if (line.compare(0, 8, "Package:") == 0) {
                    packages.emplace_back();
                    current_pkg = &packages.back();
                    current_pkg->name = line.substr(8);
                    current_pkg->name.erase(0, current_pkg->name.find_first_not_of(" \t"));
                }

                if (current_pkg) {
                    if (line.empty()) {
                        current_pkg = nullptr;
                        continue;
                    }

                    if (line.compare(0, 8, "Version:") == 0) current_pkg->version_found = true;

                    // Clean up unexpanded debhelper variables
                    if (line.find("${") != std::string::npos) {
                        size_t pos;
                        while ((pos = line.find("${")) != std::string::npos) {
                            size_t end_pos = line.find("}", pos);
                            if (end_pos != std::string::npos) {
                                size_t len = end_pos - pos + 1;
                                size_t start = pos;
                                while (start > 0 && (line[start-1] == ' ' || line[start-1] == ',')) { start--; len++; }
                                while (pos + len < line.length() && (line[pos+len] == ' ' || line[pos+len] == ',')) { len++; }
                                line.erase(start, len);
                            } else break;
                        }
                        size_t last_val = line.find_last_not_of(" ,");
                        if (last_val != std::string::npos) line = line.substr(0, last_val + 1);
                        if (line.length() <= 8 || line.back() == ':') continue;
                    }

                    if (line.compare(0, 13, "Architecture:") == 0 && line.find("any") != std::string::npos) {
                        current_pkg->control_lines.push_back("Architecture: amd64");
                    } else {
                        current_pkg->control_lines.push_back(line);
                    }
                }
            }
        }

        if (packages.empty()) {
            std::cerr << "ERROR: No binary packages found in debian/control" << std::endl;
            return false;
        }

        // 5. Create .deb(s)
        if (!split) {
            // Classic behavior: Create a staging area and put everything in the first package
            fs::path classic_staging = working_dir_ / "staging_classic";
            fs::path classic_data = classic_staging / "data";
            fs::create_directories(classic_data);

            std::cout << "  -> Preparing classic build staging area..." << std::endl;
            for (const auto& entry : fs::directory_iterator(temp_install_dir)) {
                try {
                    fs::rename(entry.path(), classic_data / entry.path().filename());
                } catch (...) {
                    fs::copy(entry.path(), classic_data / entry.path().filename(), fs::copy_options::recursive);
                }
            }
            return create_deb_from_pkg(packages[0], classic_staging);
        } else {
            // Split behavior: Create multiple debs
            bool success = true;
            for (auto& pkg : packages) {
                // Determine which files belong to this package
                fs::path pkg_staging = working_dir_ / ("staging_" + pkg.name);
                fs::path pkg_data = pkg_staging / "data";
                fs::path pkg_control = pkg_staging / "control";
                fs::create_directories(pkg_data);
                fs::create_directories(pkg_control);

                // Look for debian/<package>.install
                fs::path install_file = source_tree_root_ / "debian" / (pkg.name + ".install");
                if (fs::exists(install_file)) {
                    std::cout << "  -> Splitting files for " << pkg.name << " using .install file..." << std::endl;
                    std::ifstream ins(install_file);
                    std::string pattern;
                    while (std::getline(ins, pattern)) {
                        if (pattern.empty() || pattern[0] == '#') continue;
                        // Simple glob-to-copy (very basic implementation)
                        std::string src_pattern = pattern;
                        std::string dest_subpath = ".";
                        size_t space = pattern.find_last_of(" \t");
                        if (space != std::string::npos) {
                            src_pattern = pattern.substr(0, space);
                            dest_subpath = pattern.substr(space + 1);
                        }

                        // For now, assume simple relative paths from temp_install_dir
                        try {
                            fs::path full_src = temp_install_dir / src_pattern;
                            fs::path full_dest = pkg_data / dest_subpath;
                            if (fs::exists(full_src)) {
                                if (fs::is_directory(full_src)) fs::copy(full_src, full_dest, fs::copy_options::recursive);
                                else {
                                    fs::create_directories(full_dest.parent_path());
                                    fs::copy(full_src, full_dest);
                                }
                            }
                        } catch (...) {}
                    }
                } else if (&pkg == &packages[0]) {
                    // Fallback for first package: only take everything if no other package has an .install file.
                    // This prevents duplication where packages[0] takes everything and packages[1] takes some.
                    bool any_other_install = false;
                    for (const auto& other : packages) {
                        if (&other == &pkg) continue;
                        if (fs::exists(source_tree_root_ / "debian" / (other.name + ".install"))) {
                            any_other_install = true;
                            break;
                        }
                    }

                    if (!any_other_install) {
                        std::cout << "  -> No other .install files found, taking all installed files for " << pkg.name << "." << std::endl;
                        fs::copy(temp_install_dir, pkg_data, fs::copy_options::recursive);
                    } else {
                        std::cout << "  -> Skipping 'take-all' for " << pkg.name << " to prevent duplication with other split packages." << std::endl;
                    }
                }

                if (!create_deb_from_pkg(pkg, pkg_staging)) success = false;
            }
            return success;
        }
    }

    bool create_deb_from_pkg(BinaryPackage& pkg, const fs::path& staging_dir) {
        fs::path control_dir = staging_dir / "control";
        fs::create_directories(control_dir);
        fs::path dest_control = control_dir / "control";

        std::ofstream out(dest_control);
        for (const auto& l : pkg.control_lines) out << l << "\n";
        if (!pkg.version_found) out << "Version: " << version_ << "\n";
        out.close();

        std::cout << "  -> Assembling " << pkg.name << "..." << std::endl;
        std::string out_deb_name = pkg.name + "_" + version_ + "_amd64.deb";
        fs::path out_deb_path = working_dir_ / out_deb_name;

        if (runepkg_util_create_deb(staging_dir.c_str(), out_deb_path.c_str()) != 0) {
            std::cerr << "ERROR: Assembly failed for " << pkg.name << std::endl;
            return false;
        }
        return true;
    }

    bool parse_metadata_from_tree() {
        // Try the current directory first, then look one level deep for a 'debian' folder
        fs::path search_roots[] = {source_tree_root_, ""};

        // If we can't find debian here, look for a subdirectory that might contain it
        if (!fs::exists(source_tree_root_ / "debian")) {
            for (const auto& entry : fs::directory_iterator(source_tree_root_)) {
                if (entry.is_directory() && fs::exists(entry.path() / "debian")) {
                    source_tree_root_ = entry.path();
                    break;
                }
            }
        }

        fs::path control_path = source_tree_root_ / "debian" / "control";
        fs::path changelog_path = source_tree_root_ / "debian" / "changelog";

        if (fs::exists(control_path)) {
            char *pkg = runepkg_util_get_config_value(control_path.c_str(), "Source", ':');
            if (!pkg) pkg = runepkg_util_get_config_value(control_path.c_str(), "Package", ':');

            if (pkg) {
                source_name_ = pkg;
                free(pkg);
            }
        }

        if (fs::exists(changelog_path)) {
            std::ifstream in(changelog_path);
            std::string line;
            if (std::getline(in, line)) {
                // Format: source (version) ...
                size_t open_paren = line.find('(');
                size_t close_paren = line.find(')', open_paren);
                if (open_paren != std::string::npos && close_paren != std::string::npos) {
                    version_ = line.substr(open_paren + 1, close_paren - open_paren - 1);
                    if (source_name_.empty()) {
                        source_name_ = line.substr(0, open_paren);
                        source_name_.erase(source_name_.find_last_not_of(" \t") + 1);
                    }
                }
            }
        }

        return !source_name_.empty() && !version_.empty();
    }

    bool parse_dsc() {
        std::ifstream file(dsc_path_);
        if (!file.is_open()) {
            std::cerr << "ERROR: Could not open DSC file: " << dsc_path_ << std::endl;
            return false;
        }

        std::string line;
        bool in_files = false;
        while (std::getline(file, line)) {
            // Trim trailing \r if present
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.compare(0, 8, "Source: ") == 0) {
                source_name_ = line.substr(8);
                source_name_.erase(source_name_.find_last_not_of(" \n\r\t") + 1);
            } else if (line.compare(0, 9, "Version: ") == 0) {
                version_ = line.substr(9);
                version_.erase(version_.find_last_not_of(" \n\r\t") + 1);
            } else if (line.compare(0, 15, "Build-Depends: ") == 0) {
                build_depends_ = line.substr(15);
                build_depends_.erase(build_depends_.find_last_not_of(" \n\r\t") + 1);
            } else if (line.compare(0, 6, "Files:") == 0) {
                in_files = true;
            } else if (in_files && !line.empty() && line[0] == ' ') {
                std::stringstream ss(line);
                std::string hash, size, filename;
                ss >> hash >> size >> filename;
                if (!filename.empty()) {
                    // Trim any control chars from filename
                    filename.erase(std::remove_if(filename.begin(), filename.end(), ::iscntrl), filename.end());
                    source_files_.push_back(filename);
                }
            } else if (in_files && !line.empty() && line[0] != ' ') {
                in_files = false;
            }
        }

        if (source_name_.empty() || version_.empty()) {
            std::cerr << "ERROR: Invalid DSC format (Source or Version missing)" << std::endl;
            return false;
        }

        return true;
    }

    bool setup_workspace() {
        if (!g_build_dir) {
            std::cerr << "ERROR: build_dir not configured" << std::endl;
            return false;
        }

        // Sanitize version for filesystem paths (colons break Makefiles)
        std::string safe_version = version_;
        std::replace(safe_version.begin(), safe_version.end(), ':', '_');

        working_dir_ = fs::path(g_build_dir) / (source_name_ + "-" + safe_version + "-src");
        try {
            if (!fs::exists(working_dir_)) {
                fs::create_directories(working_dir_);
            }
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Failed to create workspace: " << e.what() << std::endl;
            return false;
        }

        return true;
    }

    bool extract_source() {
        fs::path dsc_dir = fs::path(dsc_path_).parent_path();

        std::cout << "\033[1;34m[build]\033[0m Extracting source " << source_name_ << " (" << version_ << ") to " << working_dir_ << "..." << std::endl;

        for (const auto& f : source_files_) {
            fs::path src = dsc_dir / f;
            if (!fs::exists(src)) {
                std::cerr << "ERROR: Source component missing: " << src << std::endl;
                return false;
            }

            // Detect valid archives (skip signatures like .asc)
            bool is_archive = false;
            if (f.find(".tar.") != std::string::npos && f.find(".asc") == std::string::npos) {
                is_archive = true;
            }

            if (is_archive) {
                std::cout << "  -> Extracting " << f << "..." << std::endl;
                // Extract tarballs
                char* argv[] = {(char*)"tar", (char*)"--force-local", (char*)"-xf", (char*)src.c_str(), (char*)"-C", (char*)working_dir_.c_str(), NULL};
                if (runepkg_util_execute_command("tar", argv) != 0) {
                    std::cerr << "ERROR: Failed to extract " << f << std::endl;
                    return false;
                }
            } else if (f.find(".diff.gz") != std::string::npos) {
                // TODO: Handle legacy diff.gz if needed
                std::cout << "WARNING: .diff.gz detected, patching not yet implemented in pure C++" << std::endl;
            } else {
                runepkg_log_verbose("Skipping non-archive source file: %s\n", f.c_str());
            }
        }

        // Find the source tree root (where the code was extracted)
        for (const auto& entry : fs::directory_iterator(working_dir_)) {
            if (entry.is_directory()) {
                std::string name = entry.path().filename().string();
                if (name == "debian") continue;

                // Prefer directory that matches source name
                if (name.find(source_name_) != std::string::npos) {
                    source_tree_root_ = entry.path();
                    break;
                }
                // Fallback to first non-debian directory
                if (source_tree_root_.empty()) source_tree_root_ = entry.path();
            }
        }

        if (source_tree_root_.empty()) {
            // If no other directory found, check if debian/ exists in working_dir_
            // If so, working_dir_ itself might be the root (flat layout)
            if (fs::exists(working_dir_ / "debian")) {
                source_tree_root_ = working_dir_;
            } else {
                std::cerr << "ERROR: Could not find extracted source tree" << std::endl;
                return false;
            }
        }

        std::cout << "  -> Found source tree root: " << source_tree_root_ << std::endl;

        // IMPORTANT: Modern Debian packages often put 'debian/' in a separate tarball
        // If 'debian/' directory doesn't exist inside source_tree_root_ but exists in working_dir_, move it.
        fs::path debian_dir = source_tree_root_ / "debian";
        fs::path outside_debian = working_dir_ / "debian";
        if (!fs::exists(debian_dir) && fs::exists(outside_debian)) {
            std::cout << "  -> Integrating debian/ directory into source tree..." << std::endl;
            try {
                fs::rename(outside_debian, debian_dir);
            } catch (const std::exception& e) {
                std::cerr << "ERROR: Failed to integrate debian/ directory: " << e.what() << std::endl;
                return false;
            }
        }

        return true;
    }

    bool execute_rules() {
        std::cout << "\033[1;34m[build]\033[0m Starting compilation..." << std::endl;

        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd))) return false;

        if (chdir(source_tree_root_.c_str()) != 0) {
            std::cerr << "ERROR: Failed to enter source tree" << std::endl;
            return false;
        }

        // Run debian/rules binary
        // Note: debian/rules is expected to be executable.
        char* argv[] = {(char*)"debian/rules", (char*)"binary", NULL};
        if (runepkg_util_execute_command("/usr/bin/make", argv) != 0) {
            // Try direct execution if make fails (sometimes rules is just a script)
            if (runepkg_util_execute_command("./debian/rules", argv + 1) != 0) {
                std::cerr << "ERROR: Build failed (debian/rules binary)" << std::endl;
                chdir(cwd);
                return false;
            }
        }

        chdir(cwd);
        return true;
    }

    bool collect_results() {
        if (!g_debs_dir) return false;

        std::cout << "\033[1;34m[build]\033[0m Collecting built packages..." << std::endl;

        int found = 0;
        // debian/rules binary usually puts .debs in the parent directory of the source tree
        for (const auto& entry : fs::directory_iterator(working_dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".deb") {
                fs::path dest = fs::path(g_debs_dir) / entry.path().filename();
                try {
                    fs::rename(entry.path(), dest);
                    std::cout << "\033[1;32m[build]\033[0m Successfully built package: " << dest.string() << std::endl;
                    found++;
                } catch (const std::exception& e) {
                    std::cerr << "ERROR: Failed to move " << entry.path().filename() << " to " << g_debs_dir << ": " << e.what() << std::endl;
                }
            }
        }

        if (found == 0) {
            std::cerr << "WARNING: No .deb files found after build" << std::endl;
            return false;
        }

        // IMPORTANT: Rebuild autocomplete index immediately so 'runepkg -i' can find them
        runepkg_storage_build_autocomplete_index();

        return true;
    }
};

extern "C" int runepkg_source_build(const char *dsc_path) {
    if (!dsc_path) return -1;

    SourceBuilder builder(dsc_path);
    return builder.build(false);
}

extern "C" int runepkg_source_build_split(const char *dsc_path) {
    if (!dsc_path) return -1;

    SourceBuilder builder(dsc_path);
    return builder.build(true);
}

extern "C" int runepkg_source_unpack(const char *dsc_path) {
    if (!dsc_path) return -1;

    SourceBuilder builder(dsc_path);
    return builder.unpack();
}
