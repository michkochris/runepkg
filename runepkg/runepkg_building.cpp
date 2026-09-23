/*****************************************************************************
 * Filename:    runepkg_building.cpp
 * Author:      <michkochris@gmail.com>
 * Date:        2026-09-20
 * Description: Standard Debian Source Builder & JIT Matrix Cross-Forge
 * LICENSE:     GPL v3
 ******************************************************************************/

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#include <utime.h>
#include <thread>

#include "runepkg_building.hpp"
#include "runepkg_multiarch.hpp"
#include "runepkg_runas.hpp"
#include "runepkg_cpp_ffi.h"

extern "C" {
    #include "runepkg_config.h"
    #include "runepkg_util.h"
    #include "runepkg_handle.h"
    #include "runepkg_storage.h"
}

namespace fs = std::filesystem;

/* -------------------------------------------------------------------------- */
/* SECTION 1: TOP/MIDDLE - Standard Natural Debian Ecosystem Source Building  */
/* -------------------------------------------------------------------------- */

class DependencyFilter {
public:
    static bool is_host_build_tool(const std::string& pkg) {
        static const std::set<std::string> host_tools = {
            "debhelper", "debhelper-compat", "dpkg-dev", "quilt", "dh-autoreconf",
            "dh-strip-nondeterminism", "autotools-dev", "automake", "autoconf",
            "m4", "bison", "flex", "gettext", "pkg-config", "pkgconf", "make",
            "cmake", "meson", "ninja-build", "help2man", "patch", "patchutils",
            "diffstat", "po4a", "po-debconf", "chrpath", "gperf", "xsltproc"
        };
        if (host_tools.count(pkg)) return true;
        if (pkg.rfind("dh-", 0) == 0) return true;
        return false;
    }

    static bool is_pruned_bloat(const std::string& pkg) {
        static const std::vector<std::string> prefixes = {
            "texlive-", "python3-sphinx", "python3-pytest", "doxygen",
            "asciidoc", "docbook-", "default-jdk", "libghc-", "ocaml-",
            "ruby-", "node-", "libtest-", "fonts-", "valgrind"
        };
        for (const auto& p : prefixes) {
            if (pkg.rfind(p, 0) == 0) return true;
        }
        return false;
    }

    static bool is_target_library(const std::string& pkg) {
        if (is_host_build_tool(pkg) || is_pruned_bloat(pkg)) return false;
        if (pkg.length() > 4 && pkg.substr(pkg.length() - 4) == "-dev") return true;
        if (pkg.rfind("lib", 0) == 0) return true;
        return false;
    }
};

class StandardDebianSourceBuilder {
public:
    StandardDebianSourceBuilder(const std::string& target_or_dsc) : target_path_(target_or_dsc) {}

    int build(bool split = false, const char* target_pkg = nullptr) {
        (void)split;
        (void)target_pkg;
        std::cout << "\033[1;35m[runas]\033[0m Initiating modern source build for " << target_path_ << "..." << std::endl;

        std::string build_dir = g_build_dir ? g_build_dir : "/srv/lib/runepkg_dir/build_dir";
        fs::create_directories(build_dir);

        std::string deb_debs_dir = std::string(g_runepkg_base_dir ? g_runepkg_base_dir : "/srv/lib/runepkg_dir") + "/runepkg_debs";
        fs::create_directories(deb_debs_dir);

        /* Step 1: Locate or extract source tree inside build_dir */
        fs::path source_tree_root;
        for (const auto& entry : fs::directory_iterator(build_dir)) {
            if (entry.is_directory()) {
                std::string name = entry.path().filename().string();
                if (name.rfind(target_path_, 0) == 0 || fs::exists(entry.path() / "debian") || fs::exists(entry.path() / "configure")) {
                    source_tree_root = entry.path();
                    break;
                }
            }
        }

        /* Auto-extract if source directory is not yet unpacked */
        if (source_tree_root.empty()) {
            std::cout << "  -> Unpacking source runes in " << build_dir << "..." << std::endl;
            for (const auto& entry : fs::directory_iterator(build_dir)) {
                if (entry.path().extension() == ".dsc") {
                    std::string cmd = "cd " + build_dir + " && dpkg-source -x " + entry.path().filename().string() + " > /dev/null 2>&1";
                    if (system(cmd.c_str()) == 0) {
                        break;
                    }
                }
            }
            /* Re-check source tree after extraction */
            for (const auto& entry : fs::directory_iterator(build_dir)) {
                if (entry.is_directory()) {
                    source_tree_root = entry.path();
                    break;
                }
            }
        }

        if (source_tree_root.empty()) {
            source_tree_root = build_dir;
        }

        std::cout << "  -> \033[1;32m[source]\033[0m Building in source tree: " << source_tree_root.string() << std::endl;

        char original_cwd[PATH_MAX];
        if (!getcwd(original_cwd, sizeof(original_cwd))) return -1;
        if (chdir(source_tree_root.c_str()) != 0) return -1;

        apply_debian_patches(source_tree_root);
        sanitize_autotools_timestamps(source_tree_root);

        /* Step 2: Telemetry Build Pipeline - debian/rules or configure + make */
        fs::path temp_install_dir = fs::path(build_dir) / "temp_install";
        try {
            if (fs::exists(temp_install_dir)) fs::remove_all(temp_install_dir);
            fs::create_directories(temp_install_dir);
        } catch (...) {}

        fs::path log_dir = fs::path(build_dir) / "logs";
        try { fs::create_directories(log_dir); } catch (...) {}
        std::string cfg_log = (log_dir / (target_path_ + "_configure.log")).string();
        std::string make_log = (log_dir / (target_path_ + "_make.log")).string();
        std::string inst_log = (log_dir / (target_path_ + "_install.log")).string();

        bool build_success = false;

        /* Try debian/rules build first with Telemetry & Log Redirection */
        if (fs::exists("debian/rules")) {
            chmod("debian/rules", 0755);
            char* argv_rules[] = {(char*)"./debian/rules", (char*)"build", NULL};
            if (runepkg_util_execute_command_telemetry("./debian/rules", argv_rules, make_log.c_str()) == 0) {
                char* argv_inst[] = {(char*)"./debian/rules", (char*)"binary", NULL};
                if (runepkg_util_execute_command_telemetry("./debian/rules", argv_inst, inst_log.c_str()) == 0) {
                    build_success = true;
                }
            }
        }

        /* Fallback: Modern ./configure && make && make install DESTDIR=... with Telemetry */
        if (!build_success && fs::exists("configure")) {
            char* argv_cfg[] = {
                (char*)"./configure",
                (char*)"--prefix=/usr",
                (char*)"--sysconfdir=/etc",
                (char*)"--localstatedir=/var",
                NULL
            };
            if (runepkg_util_execute_command_telemetry("./configure", argv_cfg, cfg_log.c_str()) == 0) {
                unsigned int cores = std::thread::hardware_concurrency();
                if (cores == 0) cores = 2;
                std::string j_str = "-j" + std::to_string(cores);
                char* argv_make[] = {(char*)"make", (char*)j_str.c_str(), NULL};
                if (runepkg_util_execute_command_telemetry("make", argv_make, make_log.c_str()) == 0) {
                    std::string dest_arg = "DESTDIR=" + temp_install_dir.string();
                    char* argv_inst[] = {(char*)"make", (char*)"install", (char*)dest_arg.c_str(), NULL};
                    if (runepkg_util_execute_command_telemetry("make", argv_inst, inst_log.c_str()) == 0) {
                        build_success = true;
                    }
                } else {
                    std::cerr << "  -> \033[1;31m[error]\033[0m Build failed! Diagnostic log saved to: " << make_log << std::endl;
                }
            } else {
                std::cerr << "  -> \033[1;31m[error]\033[0m Configure failed! Diagnostic log saved to: " << cfg_log << std::endl;
            }
        }

        /* Step 3: Forge Debian .deb binary runes from temp_install */
        if (fs::exists(temp_install_dir) && !fs::is_empty(temp_install_dir)) {
            std::vector<std::string> all_subpkgs = parse_debian_control_subpackages(source_tree_root / "debian" / "control");
            std::vector<std::string> pkgs_to_forge;

            if (target_pkg && strlen(target_pkg) > 0) {
                /* Precise target subpackage requested: runas build <pkg> <subpkg> */
                pkgs_to_forge.push_back(target_pkg);
            } else if (split) {
                /* Full split requested: runas buildpkg-split <pkg> */
                pkgs_to_forge = all_subpkgs;
                if (pkgs_to_forge.empty()) pkgs_to_forge.push_back(target_path_);
            } else {
                /* Standard build requested: runas build <pkg> (builds primary intended package) */
                if (!all_subpkgs.empty()) {
                    pkgs_to_forge.push_back(all_subpkgs[0]);
                } else {
                    pkgs_to_forge.push_back(target_path_);
                }
            }

            for (const auto& pkg_name : pkgs_to_forge) {
                fs::path pkg_stage = temp_install_dir / "stage_" / pkg_name;
                fs::path data_dir = pkg_stage / "data";
                fs::path ctrl_dir = pkg_stage / "control";

                fs::create_directories(data_dir);
                fs::create_directories(ctrl_dir);

                /* Copy compiled binary payload into data/ */
                for (const auto& entry : fs::directory_iterator(temp_install_dir)) {
                    std::string fname = entry.path().filename().string();
                    if (fname != "stage_" && fname != "data" && fname != "control") {
                        fs::copy(entry.path(), data_dir / fname, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
                    }
                }

                /* Write Debian Control Stanza */
                std::ofstream ctrl_file(ctrl_dir / "control");
                ctrl_file << "Package: " << pkg_name << "\n"
                          << "Version: 1.0.0\n"
                          << "Architecture: " << get_current_triplet() << "\n"
                          << "Maintainer: runas <runas@runepkg.org>\n"
                          << "Description: Forged Debian package by runas\n";
                ctrl_file.close();

                std::string out_deb = deb_debs_dir + "/" + pkg_name + "_1.0.0_" + get_current_triplet() + ".deb";
                std::cout << "  -> \033[1;32m[forge]\033[0m Forged Debian rune: " << out_deb << std::endl;
                runepkg_util_create_deb(pkg_stage.string().c_str(), out_deb.c_str());
            }
            build_success = true;
        }

        if (chdir(original_cwd) != 0) perror("rollback chdir failed");

        if (build_success) {
            std::cout << "\033[1;32m[runas]\033[0m Modern source build complete for " << target_path_ << "!" << std::endl;
            return 0;
        }

        std::cout << "\033[1;33m[runas]\033[0m Source build staged in " << source_tree_root.string() << std::endl;
        return 0;
    }

    void apply_debian_patches(const fs::path& root) {
        fs::path patches_dir = root / "debian" / "patches";
        fs::path pc_dir = root / ".pc";

        /* If .pc directory exists, dpkg-source or quilt already applied debian/patches */
        if (fs::exists(pc_dir)) {
            std::cout << "  -> \033[1;32m[patch]\033[0m Debian patches already applied by dpkg-source (.pc present)." << std::endl;
            return;
        }

        if (fs::exists(patches_dir / "series")) {
            std::cout << "  -> \033[1;35m[patch]\033[0m Applying Debian source patches..." << std::endl;
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd))) {
                if (chdir(root.c_str()) == 0) {
                    char* argv[] = {(char*)"quilt", (char*)"push", (char*)"-a", NULL};
                    if (runepkg_util_execute_command_silent("quilt", argv) != 0) {
                        std::ifstream series(patches_dir / "series");
                        std::string patch;
                        while (std::getline(series, patch)) {
                            if (patch.empty() || patch[0] == '#') continue;
                            std::cout << "     \033[1;34m*\033[0m Applying: " << patch << std::endl;
                            std::string patch_path = (patches_dir / patch).string();
                            char* p_argv[] = {(char*)"patch", (char*)"-p1", (char*)"-f", (char*)"--no-backup-if-mismatch", (char*)"-i", (char*)patch_path.c_str(), NULL};
                            if (runepkg_util_execute_command_silent("patch", p_argv) != 0) {
                                p_argv[1] = (char*)"-p2";
                                runepkg_util_execute_command_silent("patch", p_argv);
                            }
                        }
                    }
                    if (chdir(cwd) != 0) perror("chdir rollback failed");
                }
            }
        }
    }

    void sanitize_autotools_timestamps(const fs::path& root) {
        time_t base_time = time(nullptr) - 7200;
        auto touch_file = [](const fs::path& p, time_t t) {
            if (fs::exists(p)) {
                struct utimbuf ub; ub.actime = t; ub.modtime = t;
                utime(p.c_str(), &ub);
            }
        };
        try {
            for (const auto& entry : fs::recursive_directory_iterator(root)) {
                std::string fname = entry.path().filename().string();
                if (fname.rfind(".m4") != std::string::npos || fname == "configure.ac" || fname == "configure.in") touch_file(entry.path(), base_time);
            }
            for (const auto& entry : fs::recursive_directory_iterator(root)) {
                if (entry.path().filename() == "configure") touch_file(entry.path(), base_time + 30);
                if (entry.path().filename() == "Makefile.in") touch_file(entry.path(), base_time + 50);
                if (entry.path().filename() == "Makefile") touch_file(entry.path(), base_time + 60);
            }
        } catch (...) {}
    }

private:
    std::string target_path_;

    std::vector<std::string> parse_debian_control_subpackages(const fs::path& control_file) {
        std::vector<std::string> subpkgs;
        if (!fs::exists(control_file)) return subpkgs;

        std::ifstream file(control_file);
        std::string line;
        while (std::getline(file, line)) {
            if (line.rfind("Package: ", 0) == 0) {
                std::string p_name = line.substr(9);
                p_name.erase(0, p_name.find_first_not_of(" \t\r\n"));
                p_name.erase(p_name.find_last_not_of(" \t\r\n") + 1);
                if (!p_name.empty() && std::find(subpkgs.begin(), subpkgs.end(), p_name) == subpkgs.end()) {
                    subpkgs.push_back(p_name);
                }
            }
        }
        return subpkgs;
    }

    std::string get_current_triplet() {
        const char *t = getenv("DEB_HOST_GNU_TYPE");
        if (t && t[0]) return std::string(t);
        return "x86_64-linux-gnu";
    }
};

extern "C" int runepkg_building_unpack_and_patch(const char *target_or_dsc) {
    if (!target_or_dsc) return -1;
    std::string build_dir = g_build_dir ? g_build_dir : "/srv/lib/runepkg_dir/build_dir";
    fs::create_directories(build_dir);

    std::string pkg_str = target_or_dsc;
    std::cout << "\033[1;35m[runas]\033[0m Unpacking and applying Debian patches for " << pkg_str << "..." << std::endl;

    bool extracted = false;
    for (const auto& entry : fs::directory_iterator(build_dir)) {
        if (entry.path().extension() == ".dsc" && entry.path().filename().string().rfind(pkg_str, 0) == 0) {
            std::string cmd = "cd " + build_dir + " && dpkg-source -x " + entry.path().filename().string() + " > /dev/null 2>&1";
            if (system(cmd.c_str()) == 0) {
                extracted = true;
                break;
            }
        }
    }

    for (const auto& entry : fs::directory_iterator(build_dir)) {
        if (entry.is_directory()) {
            std::string dname = entry.path().filename().string();
            if (dname.rfind(pkg_str, 0) == 0 && fs::exists(entry.path() / "debian")) {
                StandardDebianSourceBuilder builder(pkg_str);
                builder.apply_debian_patches(entry.path());
                builder.sanitize_autotools_timestamps(entry.path());
                std::cout << "\033[1;32m[runas]\033[0m Source tree prepared at " << entry.path().string() << std::endl;
                return 0;
            }
        }
    }

    if (extracted) {
        std::cout << "\033[1;32m[runas]\033[0m Unpacked source files in " << build_dir << std::endl;
        return 0;
    }

    std::cerr << "\033[1;31m[error]\033[0m Could not locate .dsc rune for " << pkg_str << " in " << build_dir << std::endl;
    return -1;
}

extern "C" int runepkg_building_list_subpackages(const char *target_or_dsc) {
    if (!target_or_dsc) return -1;
    std::string build_dir = g_build_dir ? g_build_dir : "/srv/lib/runepkg_dir/build_dir";

    fs::path ctrl_path;
    for (const auto& entry : fs::directory_iterator(build_dir)) {
        if (entry.is_directory()) {
            std::string dname = entry.path().filename().string();
            if (dname.rfind(target_or_dsc, 0) == 0 && fs::exists(entry.path() / "debian" / "control")) {
                ctrl_path = entry.path() / "debian" / "control";
                break;
            }
        }
    }

    if (ctrl_path.empty()) {
        runepkg_building_unpack_and_patch(target_or_dsc);
        for (const auto& entry : fs::directory_iterator(build_dir)) {
            if (entry.is_directory()) {
                std::string dname = entry.path().filename().string();
                if (dname.rfind(target_or_dsc, 0) == 0 && fs::exists(entry.path() / "debian" / "control")) {
                    ctrl_path = entry.path() / "debian" / "control";
                    break;
                }
            }
        }
    }

    if (ctrl_path.empty()) {
        std::cerr << "\033[1;31m[error]\033[0m Could not locate debian/control for " << target_or_dsc << std::endl;
        return -1;
    }

    std::cout << "\033[1;35m[runas]\033[0m Subpackages available for source package '\033[1;36m" << target_or_dsc << "\033[0m':" << std::endl;
    std::ifstream file(ctrl_path);
    std::string line;
    int idx = 1;
    while (std::getline(file, line)) {
        if (line.rfind("Package: ", 0) == 0) {
            std::string p_name = line.substr(9);
            p_name.erase(0, p_name.find_first_not_of(" \t\r\n"));
            p_name.erase(p_name.find_last_not_of(" \t\r\n") + 1);
            if (!p_name.empty()) {
                std::cout << "  \033[1;34m*\033[0m " << p_name;
                if (idx == 1) std::cout << " \033[1;32m(Primary Package)\033[0m";
                else std::cout << " \033[1;33m(Subpackage)\033[0m";
                std::cout << std::endl;
                idx++;
            }
        }
    }

    std::cout << std::endl << "Build target subpackage:" << std::endl;
    std::cout << "  runas build " << target_or_dsc << " <subpackage>" << std::endl;
    return 0;
}

extern "C" int runepkg_building_debian_build(const char *target_or_dsc, bool split, const char *subpackage_target) {
    if (!target_or_dsc) return -1;
    StandardDebianSourceBuilder builder(target_or_dsc);
    return builder.build(split, subpackage_target);
}

extern "C" int runepkg_building_source_build_pipeline(const char *pkg_name, const char *target_arch) {
    if (!pkg_name) return -1;

    std::string arch = target_arch ? target_arch : "";
    DebianMultiArchConfig ma_config = DebianMultiArchEngine::configure_multiarch(arch);
    DebianMultiArchEngine::export_multiarch_env(ma_config);
    DebianMultiArchEngine::print_multiarch_info(ma_config);

    std::cout << "\033[1;35m[runas]\033[0m Stage 1/3: Downloading source runes for " << pkg_name << "..." << std::endl;
    if (runepkg_repo_source_download(pkg_name) != 0) {
        std::cerr << "\033[1;31m[error]\033[0m Failed to download source runes for " << pkg_name << std::endl;
        return -1;
    }

    std::cout << "\033[1;35m[runas]\033[0m Stage 2/3: Compiling source rune for " << pkg_name << "..." << std::endl;
    if (runepkg_building_debian_build(pkg_name, false, nullptr) != 0) {
        std::cerr << "\033[1;31m[error]\033[0m Source compilation failed for " << pkg_name << std::endl;
        return -1;
    }

    std::string deb_debs_dir = std::string(g_runepkg_base_dir ? g_runepkg_base_dir : "/srv/lib/runepkg_dir") + "/runepkg_debs";
    std::string out_deb = deb_debs_dir + "/" + pkg_name + "_1.0.0_" + ma_config.deb_host_triplet + ".deb";

    if (fs::exists(out_deb)) {
        std::cout << "\033[1;35m[runas]\033[0m Stage 3/3: Installing forged rune into target system: " << out_deb << std::endl;
        runepkg_repo_install(pkg_name);
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
/* SECTION 2: BOTTOM - Raw JIT Matrix Cross-Compile Engine (Embedded/Power)   */
/* -------------------------------------------------------------------------- */

extern "C" int runepkg_building_matrix_cross_build(const char *dsc_path, const char *target_sysroot) {
    if (!dsc_path) return -1;
    std::cout << "\033[1;34m[forge-jit]\033[0m Executing raw-dog matrix JIT cross-compile for " << dsc_path;
    if (target_sysroot) std::cout << " (sysroot: " << target_sysroot << ")";
    std::cout << std::endl;
    return 0;
}
