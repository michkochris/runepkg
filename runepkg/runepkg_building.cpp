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

    int build() {
        if (!parse_dsc()) return -1;
        if (!setup_workspace()) return -1;
        if (!extract_source()) return -1;

        // Try standard debian/rules first
        if (execute_rules()) {
            if (collect_results()) return 0;
        }

        // Fallback to Native Rune Build if rules failed or dh is missing
        std::cout << "\033[1;33m[build]\033[0m debian/rules failed or requires missing tools. Attempting Native Build..." << std::endl;
        if (build_native()) {
            if (collect_results()) return 0;
        }

        return -1;
    }

private:
    std::string dsc_path_;
    std::string source_name_;
    std::string version_;
    std::vector<std::string> source_files_;
    fs::path working_dir_;
    fs::path source_tree_root_;

    bool build_native() {
        std::cout << "\033[1;34m[build]\033[0m Starting Native C++ Build workflow..." << std::endl;

        fs::path staging_dir = working_dir_ / "staging";
        fs::path data_dir = staging_dir / "data";
        fs::path control_dir = staging_dir / "control";

        try {
            fs::create_directories(data_dir);
            fs::create_directories(control_dir);
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Failed to create staging area: " << e.what() << std::endl;
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

        // 3. Install to staging
        std::cout << "  -> Running make install DESTDIR=" << data_dir << " ..." << std::endl;
        std::string dest_arg = "DESTDIR=" + data_dir.string();
        char* argv_install[] = {(char*)"make", (char*)"install", (char*)dest_arg.c_str(), NULL};
        if (runepkg_util_execute_command("make", argv_install) != 0) {
            std::cerr << "ERROR: Install to staging failed" << std::endl;
            chdir(cwd);
            return false;
        }

        chdir(cwd);

        // 4. Prepare Control file
        fs::path src_control = source_tree_root_ / "debian" / "control";
        fs::path dest_control = control_dir / "control";
        if (fs::exists(src_control)) {
            std::cout << "  -> Preparing control metadata..." << std::endl;
            // For now, simple copy and basic adjustment if needed
            // TODO: In a more advanced version, we should parse the first stanza of debian/control
            // and write a proper binary control file.
            try {
                // Read debian/control and extract the first BINARY package's metadata
                // (Skip the Source: stanza at the top)
                std::ifstream in(src_control);
                std::ofstream out(dest_control);
                std::string line;
                bool in_binary_stanza = false;
                bool version_found = false;

                while (std::getline(in, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();

                    if (!in_binary_stanza && line.compare(0, 8, "Package:") == 0) {
                        in_binary_stanza = true;
                    }

                    if (in_binary_stanza) {
                        if (line.empty()) break; // End of first binary stanza

                        if (line.compare(0, 8, "Version:") == 0) version_found = true;

                        // Clean up unexpanded debhelper variables (e.g. ${shlibs:Depends})
                        if (line.find("${") != std::string::npos) {
                            size_t pos;
                            while ((pos = line.find("${")) != std::string::npos) {
                                size_t end_pos = line.find("}", pos);
                                if (end_pos != std::string::npos) {
                                    size_t len = end_pos - pos + 1;
                                    // Remove leading comma and space if present
                                    size_t start = pos;
                                    while (start > 0 && (line[start-1] == ' ' || line[start-1] == ',')) {
                                        start--;
                                        len++;
                                    }
                                    // Remove trailing comma and space if present
                                    while (pos + len < line.length() && (line[pos+len] == ' ' || line[pos+len] == ',')) {
                                        len++;
                                    }
                                    line.erase(start, len);
                                } else break;
                            }
                            // If line becomes just "Depends:" or ends in comma, clean it up
                            size_t last_val = line.find_last_not_of(" ,");
                            if (last_val != std::string::npos) {
                                line = line.substr(0, last_val + 1);
                            }
                            if (line.length() <= 8 || line.back() == ':') continue;
                        }

                        // Replace 'Architecture: any' with actual architecture
                        if (line.compare(0, 13, "Architecture:") == 0 && line.find("any") != std::string::npos) {
                            out << "Architecture: amd64\n";
                        } else {
                            out << line << "\n";
                        }
                    }
                }

                // Inject Version if missing from binary stanza (common in debian/control)
                if (!version_found) {
                    out << "Version: " << version_ << "\n";
                }
            } catch (const std::exception& e) {
                std::cerr << "ERROR: Failed to prepare control file: " << e.what() << std::endl;
                return false;
            }
        }

        // 5. Build .deb using core runepkg logic
        std::cout << "  -> Assembling .deb package..." << std::endl;
        std::string out_deb_name = source_name_ + "_" + version_ + "_amd64.deb";
        fs::path out_deb_path = working_dir_ / out_deb_name;

        if (runepkg_util_create_deb(staging_dir.c_str(), out_deb_path.c_str()) != 0) {
            std::cerr << "ERROR: Assembly failed" << std::endl;
            return false;
        }

        return true;
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

        working_dir_ = fs::path(g_build_dir) / (source_name_ + "-" + version_ + "-src");
        try {
            if (fs::exists(working_dir_)) fs::remove_all(working_dir_);
            fs::create_directories(working_dir_);
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
                char* argv[] = {(char*)"tar", (char*)"-xf", (char*)src.c_str(), (char*)"-C", (char*)working_dir_.c_str(), NULL};
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
    return builder.build();
}
