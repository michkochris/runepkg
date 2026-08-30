/******************************************************************************
 * Filename:    runepkg_bootstrap.cpp
 * Author:      <michkochris@gmail.com>
 * Date:        2026-08-29
 * Description: Universal Stage 1 Destructible Toolchain Bootstrap Orchestrator
 *              & Stage 2 Target Dependency Resolver Forge (Matrix-Aware)
 * LICENSE:     GPL v3
 ******************************************************************************/

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>
#include <thread>
#include <map>
#include <set>
#include <unordered_set>
#include <algorithm>
#include <unistd.h>
#include <dirent.h>
#include <cstring>

#include "runepkg_cpp_ffi.h"

extern "C" {
#include "runepkg_util.h"
#include "runepkg_config.h"
#include "runepkg_handle.h"
#include "runepkg_hash.h"
}

namespace fs = std::filesystem;

class BootstrapEngine {
public:
    BootstrapEngine(const std::string& profile_name) : profile_name_(profile_name) {
        profile_ = runepkg_config_load_profile(profile_name.c_str());

        /* Initialize the Matrix Engine state for this profile */
        matrix_ = RuneMatrixEngine::parse_profile(profile_name);
        if (profile_) {
            if (profile_->arch) matrix_.arch = profile_->arch;
            if (profile_->libc) matrix_.libc = profile_->libc;
            if (profile_->triplet) matrix_.triplet = profile_->triplet;
            if (profile_->sysroot) matrix_.sysroot = profile_->sysroot;
            if (profile_->crosstools) matrix_.crosstools = profile_->crosstools;
            if (profile_->cross_bin) matrix_.cross_bin = profile_->cross_bin;
        }

        unsigned int cores = std::thread::hardware_concurrency();
        if (cores == 0) cores = 1;
        j_arg_ = "-j" + std::to_string(cores);
        host_triplet_ = detect_host_triplet();
    }

    ~BootstrapEngine() {
        if (profile_) runepkg_config_free_profile(profile_);
    }

    bool is_toolchain_ready() {
        if (!profile_ || !profile_->cross_bin || !profile_->triplet) return false;
        std::string gcc_bin = std::string(profile_->cross_bin) + "/" + 
                              std::string(profile_->triplet) + "-gcc";
        return fs::exists(gcc_bin);
    }

    int run() {
        if (!profile_) {
            std::cerr << "\033[1;31mERROR:\033[0m Target profile '" << profile_name_ << "' not found." << std::endl;
            return -1;
        }

        /*
         * [ritual] Environment Sanitization
         * Ensure the bootstrap process is not poisoned by an existing active profile's environment.
         * We need the host compiler to build the cross-toolchain Stage 1.
         */
        unsetenv("CC");
        unsetenv("CXX");
        unsetenv("LD");
        unsetenv("AR");
        unsetenv("RANLIB");
        unsetenv("STRIP");
        unsetenv("CFLAGS");
        unsetenv("CXXFLAGS");
        unsetenv("LDFLAGS");
        unsetenv("PKG_CONFIG_SYSROOT_DIR");
        unsetenv("PKG_CONFIG_LIBDIR");

        /* Path sanitization: remove any existing runepkg toolchain bins from PATH */
        char* current_path = getenv("PATH");
        if (current_path) {
            std::string path_str = current_path;
            std::string cleaned_path = "";
            size_t start = 0, end = 0;
            while ((end = path_str.find(':', start)) != std::string::npos) {
                std::string segment = path_str.substr(start, end - start);
                if (segment.find("/mnt/runepkg/") == std::string::npos) {
                    if (!cleaned_path.empty()) cleaned_path += ":";
                    cleaned_path += segment;
                }
                start = end + 1;
            }
            std::string last = path_str.substr(start);
            if (!last.empty() && last.find("/mnt/runepkg/") == std::string::npos) {
                if (!cleaned_path.empty()) cleaned_path += ":";
                cleaned_path += last;
            }
            setenv("PATH", cleaned_path.c_str(), 1);
        }

        std::string triplet = matrix_.triplet.empty() ? "x86_64-unknown-linux-musl" : matrix_.triplet;

        std::cout << "\033[1;34m[bootstrap]\033[0m Initializing Stage 1 Engine for " << profile_name_ << std::endl;
        std::cout << "  -> Matrix Profile:    " << matrix_.profile_name << " (static=" << (matrix_.is_static ? "yes" : "no") << ")" << std::endl;
        std::cout << "  -> Hardware threads:  " << j_arg_.substr(2) << " (" << j_arg_ << ")" << std::endl;
        std::cout << "  -> Host Triplet:      " << host_triplet_ << std::endl;
        std::cout << "  -> Target Triplet:    " << triplet << std::endl;
        std::cout << "  -> Target Sysroot:    " << (profile_->sysroot ? profile_->sysroot : "undefined") << std::endl;
        std::cout << "  -> Crosstools Prefix: " << (profile_->crosstools ? profile_->crosstools : "undefined") << std::endl;

        if (!setup_sysroot_scaffold()) return -1;

        std::cout << "  -> \033[1;36m[Stage 1A]\033[0m Cross-Binutils" << std::endl;
        if (stage_binutils() != 0) { std::cerr << "FAILED: stage_binutils" << std::endl; return -1; }

        if (profile_->cross_bin) {
            std::string old_path = getenv("PATH") ? getenv("PATH") : "";
            std::string new_path = std::string(profile_->cross_bin) + ":" + old_path;
            setenv("PATH", new_path.c_str(), 1);
            runepkg_log_verbose("Injected cross-bin path into PATH: %s\n", profile_->cross_bin);
        }

        std::cout << "  -> \033[1;36m[Stage 1B]\033[0m GCC Core (Freestanding Bootstrap)" << std::endl;
        if (stage_gcc_core() != 0) { std::cerr << "FAILED: stage_gcc_core" << std::endl; return -1; }

        std::cout << "  -> \033[1;36m[Stage 1C]\033[0m Target Kernel Headers & Libc" << std::endl;
        if (stage_kernel_headers() != 0) { std::cerr << "FAILED: stage_kernel_headers" << std::endl; return -1; }
        if (stage_libc() != 0) { std::cerr << "FAILED: stage_libc" << std::endl; return -1; }

        std::cout << "  -> \033[1;36m[Stage 1D]\033[0m GCC Final Runtime (C/C++ Compiler)" << std::endl;
        if (stage_gcc_final() != 0) { std::cerr << "FAILED: stage_gcc_final" << std::endl; return -1; }

        std::cout << "\033[1;32m[success]\033[0m Stage 1 Destructible Toolchain verified and operational." << std::endl;
        return 0;
    }

    std::string resolve_or_fetch_dsc(const std::string& pkg_name) {
        if (fs::exists(pkg_name)) return pkg_name;

        /* Resolve binary alias to canonical source name */
        std::string source_target = pkg_name;
        char *src_resolved = runepkg_repo_find_source_for_binary(pkg_name.c_str());
        if (src_resolved) {
            source_target = src_resolved;
            free(src_resolved);
        }

        std::string build_d = get_active_build_dir();
        const std::vector<std::string> search_dirs = {
            build_d,
            "sources",
            "debs",
            "."
        };

        for (const auto& d : search_dirs) {
            if (d.empty() || !fs::exists(d)) continue;
            for (const auto& entry : fs::directory_iterator(d)) {
                if (!entry.is_regular_file()) continue;
                std::string fname = entry.path().filename().string();
                std::string prefix1 = pkg_name + "_";
                std::string prefix2 = source_target + "_";
                if (((fname == pkg_name || fname.rfind(prefix1, 0) == 0) ||
                     (fname == source_target || fname.rfind(prefix2, 0) == 0)) &&
                    fname.find(".dsc") != std::string::npos) {
                    return entry.path().string();
                }
            }
        }

        std::cout << "\033[1;33m[ritual]\033[0m Source for '" << source_target << "' not found locally. Downloading from repositories..." << std::endl;
        if (runepkg_repo_source_download(source_target.c_str()) == 0) {
            for (const auto& d : search_dirs) {
                if (d.empty() || !fs::exists(d)) continue;
                for (const auto& entry : fs::directory_iterator(d)) {
                    if (!entry.is_regular_file()) continue;
                    std::string fname = entry.path().filename().string();
                    std::string prefix1 = pkg_name + "_";
                    std::string prefix2 = source_target + "_";
                    if (((fname == pkg_name || fname.rfind(prefix1, 0) == 0) ||
                         (fname == source_target || fname.rfind(prefix2, 0) == 0)) &&
                        fname.find(".dsc") != std::string::npos) {
                        return entry.path().string();
                    }
                }
            }
        }

        return "";
    }

    int forge_target_with_resolver(const std::string& pkg_name) {
        std::cout << "\n\033[1;34m[resolver]\033[0m Inspecting minimal target tree for '\033[1;32m" 
                  << pkg_name << "\033[0m'..." << std::endl;

        inject_target_env_hacks();

        RuneTargetPlan *plan = nullptr;
        if (runepkg_resolver_resolve_target(pkg_name.c_str(), &plan) == 0 && plan) {
            std::cout << "  -> Discovered \033[1;36m" << plan->node_count 
                      << "\033[0m rune(s) in topological order." << std::endl;

            if (!satisfy_host_dependencies(plan)) {
                std::cerr << "\033[1;31mERROR:\033[0m Build aborted due to unfulfilled host dependencies." << std::endl;
                runepkg_resolver_free_plan(plan);
                return -1;
            }

            for (int i = 0; i < plan->node_count; i++) {
                std::string current_pkg = plan->nodes[i].package_name;
                
                if (current_pkg != pkg_name) {
                    if (profile_ && profile_->sysroot) {
                        try {
                            fs::path staged_marker = fs::path(profile_->sysroot).parent_path() / "staged" / current_pkg;
                            if (fs::exists(staged_marker)) {
                                std::cout << "  -> \033[1;32m[skip]\033[0m Dependency '\033[1;36m"
                                          << current_pkg << "\033[0m' already forged and staged." << std::endl;
                                continue;
                            }
                        } catch (...) {}
                    }

                    std::cout << "\n\033[1;35m[sysroot-stage]\033[0m Forging dependency: \033[1;36m"
                              << current_pkg << "\033[0m (" << plan->nodes[i].version << ")" << std::endl;
                    std::string dep_dsc = resolve_or_fetch_dsc(current_pkg);
                    if (!dep_dsc.empty()) {
                        if (runepkg_source_build_sysroot(dep_dsc.c_str()) != 0) {
                            std::cerr << "ERROR: Failed building dependency: " << current_pkg << std::endl;
                            runepkg_resolver_free_plan(plan);
                            return -1;
                        }
                    } else {
                        std::cerr << "ERROR: Failed to obtain DSC for dependency: " << current_pkg << std::endl;
                        runepkg_resolver_free_plan(plan);
                        return -1;
                    }
                }
            }

            runepkg_resolver_free_plan(plan);
        } else {
            std::cout << "\033[1;33m[warning]\033[0m Resolver found no extra prerequisites. Proceeding to target..." << std::endl;
        }

        std::cout << "\n\033[1;32m[forge-target]\033[0m Forging primary rune: \033[1;32m" << pkg_name << "\033[0m..." << std::endl;
        std::string target_dsc = resolve_or_fetch_dsc(pkg_name);
        if (target_dsc.empty()) {
            std::cerr << "ERROR: Could not locate or download DSC for: " << pkg_name << std::endl;
            return -1;
        }

        return runepkg_source_build(target_dsc.c_str());
    }

private:
    std::string profile_name_;
    TargetProfile* profile_;
    ProfileMatrix matrix_;
    std::string j_arg_;
    std::string host_triplet_;

    std::string get_active_base_dir() {
        if (g_runepkg_base_dir && strlen(g_runepkg_base_dir) > 0) {
            return std::string(g_runepkg_base_dir);
        }
        return "/var/lib/runepkg_dir";
    }

    std::string get_active_install_dir() {
        return get_active_base_dir() + "/install_dir";
    }

    std::string get_active_build_dir() {
        return get_active_base_dir() + "/build_dir";
    }

    std::string get_active_log_dir() {
        return get_active_base_dir() + "/logs";
    }

    bool is_tool_available_on_host(const std::string& tool, const std::unordered_set<std::string>& host_pkgs) {
        if (tool == "debhelper-compat") {
            return host_pkgs.count("debhelper") > 0 || host_pkgs.count("debhelper-compat") > 0;
        }

        if (host_pkgs.count(tool) > 0) return true;

        char* path_env = getenv("PATH");
        if (path_env) {
            std::string path_str = path_env;
            size_t start = 0, end = 0;
            while ((end = path_str.find(':', start)) != std::string::npos) {
                fs::path p = path_str.substr(start, end - start);
                if (fs::exists(p / tool)) return true;
                start = end + 1;
            }
            if (start < path_str.length()) {
                fs::path p = path_str.substr(start);
                if (fs::exists(p / tool)) return true;
            }
        }

        std::string inst_dir = get_active_install_dir();
        fs::path local_bin = fs::path(inst_dir) / "usr" / "bin" / tool;
        if (fs::exists(local_bin)) return true;

        return false;
    }

    bool satisfy_host_dependencies(RuneTargetPlan* plan) {
        if (!plan) return true;

        /* Refresh host package state */
        runepkg_host_dpkg_sync();

        std::unordered_set<std::string> host_installed_pkgs;
        std::string host_root = get_active_base_dir() + "/runepkg_db/host";
        if (fs::exists(host_root)) {
            for (const auto& entry : fs::directory_iterator(host_root)) {
                if (entry.is_directory()) {
                    std::string dirname = entry.path().filename().string();
                    size_t dash = dirname.find_last_of('-');
                    if (dash != std::string::npos) {
                        host_installed_pkgs.insert(dirname.substr(0, dash));
                    }
                }
            }
        }

        std::set<std::string> missing;
        for (int i = 0; i < plan->node_count; i++) {
            for (int j = 0; j < plan->nodes[i].host_tools_count; j++) {
                std::string tool = plan->nodes[i].host_tools_required[j];
                if (!is_tool_available_on_host(tool, host_installed_pkgs)) {
                    missing.insert(tool);
                }
            }
        }

        if (missing.empty()) return true;

        std::cout << "  -> \033[1;34m[host-depends]\033[0m Missing host tools detected: ";
        for (const auto& t : missing) std::cout << t << " ";
        std::cout << std::endl;

        std::vector<const char*> c_tools;
        for (const auto& t : missing) {
            if (t != "debhelper-compat") c_tools.push_back(t.c_str());
        }

        if (c_tools.empty()) return true;

        /* Non-recursive download: resolve only uninstalled missing tools via resolver */
        std::cout << "  -> \033[1;33m[ritual]\033[0m Fetching host packages via runepkg..." << std::endl;
        if (runepkg_repo_download_multiple(c_tools.data(), c_tools.size(), false) != 0) {
            std::cerr << "\033[1;31m[-] FATAL:\033[0m Failed downloading host tools." << std::endl;
            return false;
        }

        /* Check if system dpkg is available to register host packages */
        bool has_dpkg = (access("/usr/bin/dpkg", X_OK) == 0);
        if (has_dpkg) {
            std::cout << "  -> \033[1;34m[host-dpkg]\033[0m Unpacking host tools with dpkg -i..." << std::endl;
            std::string ddir = g_download_dir ? g_download_dir : (get_active_base_dir() + "/download_dir");
            std::string dpkg_cmd = "dpkg -i --force-depends " + ddir + "/*.deb >/dev/null 2>&1";
            int dpkg_res = system(dpkg_cmd.c_str());
            if (dpkg_res != 0) {
                std::string dpkg_retry = "dpkg -i --force-depends " + ddir + "/*.deb";
                if (system(dpkg_retry.c_str()) != 0) {
                }
            }
        } else {
            std::cout << "  -> \033[1;34m[install]\033[0m Installing host tools to prefix..." << std::endl;
            runepkg_repo_install_multiple(c_tools.data(), c_tools.size());
        }

        runepkg_host_dpkg_sync();

        std::string inst_dir = get_active_install_dir();
        std::string local_bin = inst_dir + "/usr/bin:" + inst_dir + "/bin";
        char* cur_path = getenv("PATH");
        if (cur_path) setenv("PATH", (local_bin + ":" + cur_path).c_str(), 1);
        else setenv("PATH", local_bin.c_str(), 1);

        std::string local_perl = inst_dir + "/usr/share/perl5:" + inst_dir + "/usr/lib/perl5";
        char* cur_perl = getenv("PERL5LIB");
        if (cur_perl) setenv("PERL5LIB", (local_perl + ":" + cur_perl).c_str(), 1);
        else setenv("PERL5LIB", local_perl.c_str(), 1);

        return true;
    }

    void inject_target_env_hacks() {
        if (!profile_) return;

        std::string sysroot = profile_->sysroot ? profile_->sysroot : "";

        if (matrix_.is_static) {
            std::cout << "  -> \033[1;34m[hack]\033[0m Injecting static linking specs." << std::endl;
            setenv("LDFLAGS", "-static -static-libgcc -static-libstdc++", 1);
        } else {
            std::string pt_interp = "/lib64/ld-linux-x86-64.so.2";
            if (matrix_.libc == "musl") {
                pt_interp = "/lib/ld-musl-" + matrix_.arch + ".so.1";
            } else if (matrix_.arch == "aarch64") {
                pt_interp = "/lib/ld-linux-aarch64.so.1";
            }
            std::cout << "  -> \033[1;34m[hack]\033[0m Predicting Debian PT_INTERP: " << pt_interp << std::endl;
            std::string ldflags = "-Wl,--dynamic-linker=" + pt_interp;
            if (!sysroot.empty()) {
                ldflags += " -Wl,-rpath-link=" + sysroot + "/usr/lib/" + matrix_.triplet + " -Wl,-rpath-link=" + sysroot + "/usr/lib -Wl,-rpath-link=" + sysroot + "/lib";
            }
            setenv("LDFLAGS", ldflags.c_str(), 1);
        }

        if (!sysroot.empty()) {
            std::string extra_cflags = "-isystem " + sysroot + "/usr/include/" + matrix_.triplet + " -isystem " + sysroot + "/usr/include";

            char* existing_cflags = getenv("CFLAGS");
            if (existing_cflags) {
                std::string new_cflags = std::string(existing_cflags) + " " + extra_cflags;
                setenv("CFLAGS", new_cflags.c_str(), 1);
            } else {
                setenv("CFLAGS", extra_cflags.c_str(), 1);
            }

            char* existing_cxxflags = getenv("CXXFLAGS");
            if (existing_cxxflags) {
                std::string new_cxxflags = std::string(existing_cxxflags) + " " + extra_cflags;
                setenv("CXXFLAGS", new_cxxflags.c_str(), 1);
            } else {
                setenv("CXXFLAGS", extra_cflags.c_str(), 1);
            }
        }
    }

    std::string detect_host_triplet() {
        char buffer[128];
        std::string result = "";
        FILE* pipe = popen("gcc -dumpmachine 2>/dev/null", "r");
        if (pipe) {
            if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
                result = buffer;
                result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
                result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
            }
            pclose(pipe);
        }
        return result.empty() ? "x86_64-pc-linux-gnu" : result;
    }

    bool setup_sysroot_scaffold() {
        if (!profile_->sysroot || !profile_->crosstools) {
            std::cerr << "ERROR: Invalid sysroot or crosstools path in profile." << std::endl;
            return false;
        }

        try {
            fs::path sysroot = profile_->sysroot;
            fs::path crosstools = profile_->crosstools;
            std::string triplet = matrix_.triplet.empty() ? "x86_64-unknown-linux-musl" : matrix_.triplet;

            fs::create_directories(sysroot / "usr" / "lib" / triplet);
            fs::create_directories(sysroot / "usr" / "include" / triplet);
            fs::create_directories(crosstools / triplet / "lib");
            fs::create_directories(crosstools / triplet / "include");

            auto force_symlink = [](const fs::path& target, const fs::path& link_path) {
                if (fs::exists(link_path) || fs::is_symlink(link_path)) {
                    fs::remove(link_path);
                }
                fs::create_directory_symlink(target, link_path);
            };

            force_symlink("usr/bin", sysroot / "bin");
            force_symlink("usr/sbin", sysroot / "sbin");
            force_symlink("usr/lib", sysroot / "lib");
            force_symlink("usr/include", sysroot / "include");

            return true;
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Failed initializing sysroot scaffold: " << e.what() << std::endl;
            return false;
        }
    }

    void sync_target_sysroot_links() {
        try {
            std::string triplet = matrix_.triplet.empty() ? "x86_64-unknown-linux-musl" : matrix_.triplet;
            fs::path triplet_dir = fs::path(profile_->crosstools) / triplet;
            fs::path sysroot_usr_lib = fs::path(profile_->sysroot) / "usr" / "lib";
            fs::path sysroot_lib = fs::path(profile_->sysroot) / "lib";
            fs::path sysroot_usr_inc = fs::path(profile_->sysroot) / "usr" / "include";

            std::vector<fs::path> ssp_targets = {
                sysroot_usr_lib / "libssp_nonshared.a",
                sysroot_usr_lib / "libssp.a",
                sysroot_lib / "libssp_nonshared.a",
                sysroot_lib / "libssp.a",
                triplet_dir / "lib" / "libssp_nonshared.a",
                triplet_dir / "lib" / "libssp.a"
            };

            for (const auto& ssp_file : ssp_targets) {
                if (!fs::exists(ssp_file)) {
                    std::ofstream ar_file(ssp_file, std::ios::binary);
                    if (ar_file.is_open()) {
                        ar_file << "!<arch>\n";
                        ar_file.close();
                    }
                }
            }

            fs::path triplet_sys_inc = triplet_dir / "sys-include";
            if (fs::exists(sysroot_usr_inc) && !fs::exists(triplet_sys_inc)) {
                fs::create_directory_symlink(sysroot_usr_inc, triplet_sys_inc);
            }

            if (fs::exists(sysroot_usr_lib)) {
                for (const auto& entry : fs::directory_iterator(sysroot_usr_lib)) {
                    std::string fname = entry.path().filename().string();
                    fs::path dst = triplet_dir / "lib" / fname;
                    
                    std::error_code ec;
                    if (fs::is_symlink(entry.path())) {
                        fs::path target = fs::read_symlink(entry.path(), ec);
                        if (!ec && !fs::exists(dst)) fs::create_symlink(target, dst, ec);
                    } else if (!fs::exists(dst)) {
                        fs::copy_file(entry.path(), dst, fs::copy_options::overwrite_existing, ec);
                    }
                }
            }
        } catch (...) {}
    }

    void reset_build_directory(const fs::path& dir_path) {
        if (fs::exists(dir_path)) {
            std::cout << "  -> \033[1;33m[clean]\033[0m Resetting workspace " << dir_path.filename() << "..." << std::endl;
            try {
                fs::remove_all(dir_path);
            } catch (...) {}
        }
        fs::create_directories(dir_path);
    }

    std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        char buf[20];
        struct std::tm* tm_info = std::localtime(&now_c);
        if (tm_info) {
            std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", tm_info);
            return std::string(buf);
        }
        return "00000000000000";
    }

    void show_log_tail(const std::string& log_path, int lines = 20) {
        std::ifstream file(log_path);
        if (!file.is_open()) return;

        std::vector<std::string> buffer;
        std::string line;
        while (std::getline(file, line)) {
            buffer.push_back(line);
            if (buffer.size() > (size_t)lines) buffer.erase(buffer.begin());
        }

        std::cerr << "\033[1;31m[error]\033[0m Last " << lines << " lines of " << log_path << ":" << std::endl;
        for (const auto& l : buffer) std::cerr << "  " << l << std::endl;
    }

    int execute_step(const std::string& step_name, const fs::path& work_dir, const std::string& cmd, const std::vector<std::string>& args) {
        std::string stamp_dir = "/mnt/runepkg/" + profile_name_;
        fs::path stamp_file = fs::path(stamp_dir) / (step_name + ".stamp");

        if (fs::exists(stamp_file)) {
            std::cout << "  -> \033[1;32m[skip]\033[0m Step '" << step_name << "' already completed (stamp found)." << std::endl;
            return 0;
        }

        std::string log_dir_path = get_active_log_dir();
        fs::create_directories(log_dir_path);
        std::string log_name = "bootstrap_" + step_name + "_" + get_timestamp() + ".log";
        fs::path log_path = fs::path(log_dir_path) / log_name;

        std::cout << "  -> \033[1;36m[" << step_name << "]\033[0m Running in " << work_dir.filename() << "..." << std::endl;

        fs::path old_cwd = fs::current_path();
        if (!work_dir.empty()) {
            try {
                fs::current_path(work_dir);
            } catch (...) {
                std::cerr << "ERROR: Failed to enter directory: " << work_dir << std::endl;
                return -1;
            }
        }

        std::vector<char*> c_args;
        c_args.push_back(const_cast<char*>(cmd.c_str()));
        for (const auto& arg : args) c_args.push_back(const_cast<char*>(arg.c_str()));
        c_args.push_back(nullptr);

        if (cmd.find("./") != std::string::npos || cmd.find("../") != std::string::npos) {
            fs::path cmd_path = fs::absolute(cmd);
            if (fs::exists(cmd_path)) chmod(cmd_path.c_str(), 0755);
        }

        int ret = runepkg_util_execute_command_to_file(cmd.c_str(), c_args.data(), log_path.c_str());
        fs::current_path(old_cwd);

        if (ret != 0) {
            std::cerr << "\033[1;31mERROR:\033[0m Step '" << step_name << "' failed with code " << ret << std::endl;
            show_log_tail(log_path.string());
            return ret;
        }

        fs::create_directories(stamp_dir);
        std::ofstream stamp(stamp_file);
        stamp << time(NULL);

        return 0;
    }

    fs::path find_source(const std::string& pattern) {
        std::string bdir = get_active_build_dir();
        if (!fs::exists(bdir)) return "";

        for (const auto& entry : fs::directory_iterator(bdir)) {
            if (!entry.is_directory()) continue;
            std::string name = entry.path().filename().string();

            if (name.find(pattern) != std::string::npos) {
                std::vector<fs::path> candidates = {entry.path()};
                try {
                    for (const auto& sub : fs::directory_iterator(entry.path())) {
                        if (sub.is_directory()) candidates.push_back(sub.path());
                    }
                } catch (...) {}

                for (const auto& cand : candidates) {
                    if (fs::exists(cand / "configure") ||
                        fs::exists(cand / "Makefile") ||
                        fs::exists(cand / "debian")) {
                        runepkg_log_verbose("find_source: Found root candidate for '%s' at %s\n", pattern.c_str(), cand.c_str());
                        return cand;
                    }
                }
            }
        }
        return "";
    }

    int get_patch_strip_level(const fs::path& patch_path) {
        std::ifstream file(patch_path);
        if (!file.is_open()) return 1;
        std::string line;
        while (std::getline(file, line)) {
            if (line.compare(0, 6, "--- a/") == 0) {
                if (line.compare(6, 4, "src/") == 0) return 2;
                return 1;
            }
            if (line.compare(0, 4, "--- ") == 0) {
                if (line.compare(4, 4, "src/") == 0) return 2;
                return 1;
            }
            if (file.tellg() > 4096) break;
        }
        return 1;
    }

    void patch_gcc_specs(const fs::path& gcc_root) {
        fs::path t_linux64 = gcc_root / "gcc" / "config" / "i386" / "t-linux64";
        fs::path linux64_h = gcc_root / "gcc" / "config" / "i386" / "linux64.h";

        if (fs::exists(t_linux64)) {
            char* sed_argv[] = {(char*)"sed", (char*)"-e", (char*)"/m64=/s/lib64/lib/", (char*)"-i", (char*)t_linux64.c_str(), NULL};
            runepkg_util_execute_command_silent("sed", sed_argv);
        }

        if (fs::exists(linux64_h)) {
            std::string musl_interp = "/lib/ld-musl-" + matrix_.arch + ".so.1";
            std::string sed_expr = "s@/lib64/ld-linux-x86-64.so.2@" + musl_interp + "@g";
            char* sed_argv[] = {
                (char*)"sed",
                (char*)"-e", (char*)sed_expr.c_str(),
                (char*)"-e", (char*)"s@/lib64@/lib@g",
                (char*)"-e", (char*)"s@/usr/lib64@/usr/lib@g",
                (char*)"-i", (char*)linux64_h.c_str(),
                NULL
            };
            runepkg_util_execute_command_silent("sed", sed_argv);
        }
    }

    fs::path prepare_gcc_source(const fs::path& src_dir) {
        std::cout << "  -> GCC source container detected. Unpacking inner runes..." << std::endl;
        bool unpacked = false;
        fs::path inner_root;

        for (const auto& entry : fs::directory_iterator(src_dir)) {
            std::string filename = entry.path().filename().string();
            if (filename.find("gcc-") == 0 && filename.find(".tar") != std::string::npos) {
                std::cout << "     * Unpacking " << filename << "..." << std::endl;
                char* argv[] = {(char*)"tar", (char*)"--force-local", (char*)"-xf", (char*)entry.path().c_str(), (char*)"-C", (char*)src_dir.c_str(), NULL};
                if (runepkg_util_execute_command("tar", argv) == 0) {
                    unpacked = true;
                    break;
                }
            }
        }

        if (!unpacked) return "";

        for (const auto& entry : fs::recursive_directory_iterator(src_dir)) {
            if (entry.is_directory() && fs::exists(entry.path() / "configure") && fs::exists(entry.path() / "gcc")) {
                inner_root = entry.path();
                break;
            }
        }

        if (inner_root.empty()) inner_root = src_dir;

        fs::path patches_dir = src_dir / "debian" / "patches";
        std::vector<std::string> patch_list;

        if (fs::exists(patches_dir / "series")) {
            std::ifstream series(patches_dir / "series");
            std::string line;
            while (std::getline(series, line)) {
                if (!line.empty() && line[0] != '#') patch_list.push_back(line);
            }
        } else if (fs::exists(src_dir / "debian" / "rules.patch")) {
            std::cout << "  -> Generating patch series from rules.patch..." << std::endl;
            std::ifstream rules(src_dir / "debian" / "rules.patch");
            std::string line;
            bool in_patches = false;
            while (std::getline(rules, line)) {
                if (line.find("debian_patches =") != std::string::npos || line.find("debian_patches +=") != std::string::npos) {
                    in_patches = true;
                    continue;
                }
                if (in_patches) {
                    size_t first = line.find_first_not_of(" \t\\");
                    size_t last = line.find_last_not_of(" \t\\");
                    if (first != std::string::npos) {
                        std::string patch = line.substr(first, last - first + 1);
                        if (!patch.empty() && patch.find('=') == std::string::npos) {
                             if (fs::exists(patches_dir / (patch + ".diff"))) patch += ".diff";
                             else if (fs::exists(patches_dir / (patch + ".patch"))) patch += ".patch";
                             patch_list.push_back(patch);
                        }
                    }
                    if (line.find('\\') == std::string::npos) in_patches = false;
                }
            }
        }

        if (!patch_list.empty()) {
            std::cout << "  -> Applying " << patch_list.size() << " Debian patches to " << inner_root.filename() << "..." << std::endl;
            for (const auto& patch : patch_list) {
                fs::path patch_path = patches_dir / patch;
                if (fs::exists(patch_path)) {
                    int p_level = get_patch_strip_level(patch_path);
                    std::string p_arg = "-p" + std::to_string(p_level);

                    char* p_argv[] = {(char*)"patch", (char*)p_arg.c_str(), (char*)"-f", (char*)"--no-backup-if-mismatch", (char*)"-i", (char*)patch_path.c_str(), (char*)"-d", (char*)inner_root.c_str(), NULL};

                    if (runepkg_util_execute_command_silent("patch", p_argv) != 0) {
                        int alt_level = (p_level == 1) ? 2 : 1;
                        std::string alt_arg = "-p" + std::to_string(alt_level);
                        p_argv[1] = (char*)alt_arg.c_str();
                        runepkg_util_execute_command_silent("patch", p_argv);
                    }
                }
            }
        }

        patch_gcc_specs(inner_root);
        return inner_root;
    }

    void stub_distro_defaults(const fs::path& src, const fs::path& build_dir) {
        fs::path src_stub = src / "gcc" / "distro-defaults.h";
        if (!fs::exists(src_stub)) {
            std::ofstream stub(src_stub);
            stub << "#ifndef DISTRO_DEFAULTS_H\n#define DISTRO_DEFAULTS_H\n/* runepkg bootstrap stub */\n#endif\n";
            stub.close();
        }

        fs::path build_gcc_dir = build_dir / "gcc";
        fs::create_directories(build_gcc_dir);
        fs::path build_stub = build_gcc_dir / "distro-defaults.h";
        if (!fs::exists(build_stub)) {
            std::ofstream stub(build_stub);
            stub << "#ifndef DISTRO_DEFAULTS_H\n#define DISTRO_DEFAULTS_H\n/* runepkg bootstrap stub */\n#endif\n";
            stub.close();
        }
    }

    fs::path ensure_source(const std::string& pkg_name, const std::string& pattern) {
        fs::path src = find_source(pattern);
        if (src.empty()) {
            std::cout << "\033[1;33m[ritual]\033[0m Source for '" << pkg_name << "' not found. Unearthing from repositories..." << std::endl;
            if (runepkg_repo_source_download(pkg_name.c_str()) != 0) return "";
            src = find_source(pattern);
        }

        if (src.empty()) return "";

        if (pattern.find("gcc") != std::string::npos && !fs::exists(src / "configure")) {
            fs::path real_root = prepare_gcc_source(src);
            if (real_root.empty()) {
                std::cerr << "ERROR: Failed to prepare GCC source tree." << std::endl;
                return "";
            }
            return real_root;
        }

        return src;
    }

    int stage_binutils() {
        fs::path src = ensure_source("binutils", "binutils");
        if (src.empty()) return -1;

        fs::path build_dir = src / "build_runepkg";
        reset_build_directory(build_dir);

        std::string triplet = matrix_.triplet.empty() ? "x86_64-unknown-linux-musl" : matrix_.triplet;

        std::vector<std::string> conf_args = {
            "--prefix=" + std::string(profile_->crosstools),
            "--build=" + host_triplet_,
            "--host=" + host_triplet_,
            "--target=" + triplet,
            "--with-sysroot=" + std::string(profile_->sysroot),
            "--disable-nls",
            "--disable-werror",
            "--disable-gprofng",
            "--enable-plugins"
        };

        if (execute_step("binutils_configure", build_dir, "../configure", conf_args) != 0) return -1;
        if (execute_step("binutils_make", build_dir, "make", {j_arg_}) != 0) return -1;
        if (execute_step("binutils_install", build_dir, "make", {"install"}) != 0) return -1;
        return 0;
    }

    int stage_gcc_core() {
        fs::path src = ensure_source("gcc-12", "gcc");
        if (src.empty()) return -1;

        fs::path build_dir = src / "build_bootstrap";
        reset_build_directory(build_dir);
        stub_distro_defaults(src, build_dir);

        std::string triplet = matrix_.triplet.empty() ? "x86_64-unknown-linux-musl" : matrix_.triplet;

        std::vector<std::string> conf_args = {
            "--prefix=" + std::string(profile_->crosstools),
            "--build=" + host_triplet_,
            "--host=" + host_triplet_,
            "--target=" + triplet,
            "--without-headers",
            "--with-newlib",
            "--enable-languages=c",
            "--disable-threads",
            "--disable-shared",
            "--disable-multilib",
            "--disable-nls",
            "--disable-werror",
            "--disable-libatomic",
            "--disable-libgomp",
            "--disable-libquadmath",
            "--disable-libssp",
            "--disable-libvtv",
            "--disable-libstdcxx",
            "--disable-libsanitizer"
        };

        if (execute_step("gcc_boot_conf", build_dir, "../configure", conf_args) != 0) return -1;
        if (execute_step("gcc_boot_make", build_dir, "make", {j_arg_, "all-gcc", "all-target-libgcc"}) != 0) return -1;
        if (execute_step("gcc_boot_inst", build_dir, "make", {"install-gcc", "install-target-libgcc"}) != 0) return -1;
        return 0;
    }

    int stage_kernel_headers() {
        fs::path src = ensure_source("linux", "linux");
        if (src.empty()) return -1;

        std::string hdr_path = "INSTALL_HDR_PATH=" + std::string(profile_->sysroot) + "/usr";
        if (execute_step("kernel_headers", src, "make", {"headers_install", hdr_path}) != 0) return -1;
        return 0;
    }

    int stage_libc() {
        if (matrix_.libc != "musl") {
             std::cout << "  -> Skipping libc build (not musl)" << std::endl;
             return 0;
        }

        fs::path src = ensure_source("musl", "musl");
        if (src.empty()) return -1;

        if (fs::exists(src / "config.mak")) {
            std::cout << "  -> \033[1;33m[clean]\033[0m Resetting musl tree..." << std::endl;
            char* clean_argv[] = {(char*)"make", (char*)"distclean", NULL};
            runepkg_util_execute_command_silent("make", clean_argv);
        }

        std::string triplet = matrix_.triplet.empty() ? "x86_64-unknown-linux-musl" : matrix_.triplet;
        std::string cc_val = std::string(profile_->cross_bin) + "/" + triplet + "-gcc";
        
        std::vector<std::string> conf_args = {
            "CC=" + cc_val,
            "--prefix=/usr",
            "--syslibdir=/usr/lib",
            "--target=" + triplet,
            "--enable-static",
            "--enable-shared"
        };

        if (execute_step("musl_configure", src, "./configure", conf_args) != 0) return -1;
        if (execute_step("musl_make", src, "make", {j_arg_}) != 0) return -1;

        std::string dest_arg = "DESTDIR=" + std::string(profile_->sysroot);
        if (execute_step("musl_install", src, "make", {dest_arg, "install"}) != 0) return -1;

        sync_target_sysroot_links();
        return 0;
    }

    int stage_gcc_final() {
        fs::path src = ensure_source("gcc-12", "gcc");
        if (src.empty()) return -1;

        sync_target_sysroot_links();

        fs::path build_dir = src / "build_final";
        reset_build_directory(build_dir);
        stub_distro_defaults(src, build_dir);

        std::string triplet_str = matrix_.triplet.empty() ? "x86_64-unknown-linux-musl" : matrix_.triplet;
        fs::path toolexeclib = fs::path(profile_->crosstools) / triplet_str / "lib";

        std::vector<std::string> conf_args = {
            "--prefix=" + std::string(profile_->crosstools),
            "--build=" + host_triplet_,
            "--host=" + host_triplet_,
            "--target=" + triplet_str,
            "--with-sysroot=" + std::string(profile_->sysroot),
            "--with-build-sysroot=" + std::string(profile_->sysroot),
            "--with-native-system-header-dir=/usr/include",
            "--with-toolexeclibdir=" + toolexeclib.string(),
            "--enable-languages=c,c++",
            "--enable-threads=posix",
            matrix_.is_static ? "--disable-shared" : "--enable-shared",
            "--enable-static",
            "--disable-nls",
            "--disable-multilib",
            "--disable-werror",
            "--disable-libstdcxx-pch",
            "--disable-libsanitizer",
            "--disable-libssp",
            "--disable-libquadmath",
            "--disable-libvtv",
            "--disable-libgomp",
            "--disable-libatomic",
            "CFLAGS_FOR_TARGET=-O2 -g",
            "CXXFLAGS_FOR_TARGET=-O2 -g"
        };

        if (execute_step("gcc_final_conf", build_dir, "../configure", conf_args) != 0) return -1;
        if (execute_step("gcc_final_make_gcc", build_dir, "make", {j_arg_, "all-gcc"}) != 0) return -1;
        if (execute_step("gcc_final_make_libgcc", build_dir, "make", {j_arg_, "all-target-libgcc"}) != 0) return -1;
        if (execute_step("gcc_final_inst_libgcc", build_dir, "make", {"install-target-libgcc"}) != 0) return -1;

        fs::path libgcc_path = toolexeclib / "libgcc.a";
        fs::path libgcc_eh_path = toolexeclib / "libgcc_eh.a";
        if (fs::exists(libgcc_path) && !fs::exists(libgcc_eh_path)) {
            std::error_code ec;
            fs::create_symlink("libgcc.a", libgcc_eh_path, ec);
        }

        sync_target_sysroot_links();

        if (execute_step("gcc_final_make_libstdcxx", build_dir, "make", {j_arg_, "all-target-libstdc++-v3"}) != 0) return -1;
        if (execute_step("gcc_final_inst", build_dir, "make", {"install-gcc", "install-target-libstdc++-v3"}) != 0) return -1;

        return 0;
    }
};

extern "C" int handle_switch(const char *target) {
    const char *profile_name;
    TargetProfile *profile;

    if (!target || strcmp(target, "--reset") == 0) {
        std::cout << "\033[1;34m[switch]\033[0m Resetting active target context to host/native." << std::endl;
        runepkg_config_save_active_state(nullptr);
        return 0;
    }

    profile_name = target;
    if (strncmp(target, "--target=", 9) == 0) {
        profile_name = target + 9;
    }

    std::cout << "\033[1;34m[switch]\033[0m Activating profile: " << profile_name << std::endl;

    profile = runepkg_config_load_profile(profile_name);
    if (!profile) {
        std::cerr << "\033[1;31mError:\033[0m Profile '" << profile_name << "' not found in system or user locations." << std::endl;
        return -1;
    }

    if (profile->sysroot) runepkg_util_create_dir_recursive(profile->sysroot, 0755);
    if (profile->crosstools) runepkg_util_create_dir_recursive(profile->crosstools, 0755);

    if (runepkg_config_save_active_state(profile) != 0) {
        std::cerr << "Error: Failed to atomically save active target state." << std::endl;
        runepkg_config_free_profile(profile);
        return -1;
    }

    std::cout << "Status: READY (Target context activated)" << std::endl;
    runepkg_config_free_profile(profile);
    return 0;
}

extern "C" void handle_print_profile(void) {
    if (!g_active_state.active_target) {
        std::cout << "Active Profile:  (none / host-native)" << std::endl;
        return;
    }

    std::cout << "Active Profile:  " << g_active_state.active_target << std::endl;
    std::cout << "Target Triplet:  " << (g_active_state.active_triplet ? g_active_state.active_triplet : "unknown") << std::endl;
    std::cout << "Sysroot:         " << (g_active_state.active_sysroot ? g_active_state.active_sysroot : "unknown") << std::endl;
    std::cout << "Crosstools:      " << (g_active_state.active_crosstools ? g_active_state.active_crosstools : "unknown") << std::endl;

    if (g_active_state.activated_at > 0) {
        char time_buf[64];
        time_t t = (time_t)g_active_state.activated_at;
        struct tm *tm_info = std::localtime(&t);
        if (tm_info) {
            std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
            std::cout << "Activated At:    " << time_buf << std::endl;
        }
    }

    std::cout << "Status:          READY (Toolchain Context Active)" << std::endl;
}

extern "C" int handle_build_toolchain(const char *target) {
    return handle_build_toolchain_engine(target);
}

extern "C" int handle_build_toolchain_with_targets(const char *target, const char **pkg_names, int count) {
    return handle_build_toolchain_targets(target, pkg_names, count);
}

extern "C" int handle_build_toolchain_engine(const char* target_name) {
    if (!target_name) return -1;

    /* Always switch context to the desired target first */
    std::string switch_arg = std::string("--target=") + target_name;
    handle_switch(switch_arg.c_str());

    BootstrapEngine engine(target_name);
    g_bootstrap_mode = true;
    int res = engine.run();
    g_bootstrap_mode = false;
    return res;
}

extern "C" int handle_build_toolchain_targets(const char* target_name, const char** pkg_names, int count) {
    if (!target_name) return -1;

    /* Always switch context to the desired target first */
    std::string switch_arg = std::string("--target=") + target_name;
    handle_switch(switch_arg.c_str());

    BootstrapEngine engine(target_name);

    if (!engine.is_toolchain_ready()) {
        std::cout << "\033[1;33m[toolchain]\033[0m Cross-compiler not found. Initiating bootstrap..." << std::endl;
        g_bootstrap_mode = true;
        int res = engine.run();
        g_bootstrap_mode = false;

        if (res != 0) {
            std::cerr << "ERROR: Failed to bootstrap toolchain for " << target_name << std::endl;
            return -1;
        }
    } else {
        std::cout << "\033[1;32m[toolchain]\033[0m Active cross-compiler verified for " << target_name << std::endl;
    }

    /* Refresh switch to ensure all forged packages use the newly built toolchain */
    handle_switch(switch_arg.c_str());

    for (int i = 0; i < count; i++) {
        if (engine.forge_target_with_resolver(pkg_names[i]) != 0) {
            std::cerr << "ERROR: Failed forging target package: " << pkg_names[i] << std::endl;
            return -1;
        }
    }

    return 0;
}
