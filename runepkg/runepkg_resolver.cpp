/******************************************************************************
 * Filename:    runepkg_resolver.cpp
 * Author:      <michkochris@gmail.com>
 * Date:        2026-08-26
 * Description: 70k+ Repository Graph Harvester & Minimal Target Rune Resolver
 * LICENSE:     GPL v3
 ******************************************************************************/

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <thread>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <libgen.h>
#include <dirent.h>

#include "runepkg_cpp_ffi.h"
#include "runepkg_util_cpp.hpp"
#include "runepkg_security.hpp"

extern "C" {
    #include "runepkg_config.h"
    #include "runepkg_util.h"
    #include "runepkg_storage.h"
    #include "runepkg_host.h"
}

// Architecture - dynamic lookup from host integration or active profile
static const char* get_effective_arch() {
    if (g_active_profile && g_active_profile->deb_host_arch && g_active_profile->deb_host_arch[0]) {
        return g_active_profile->deb_host_arch;
    }
    const char* arch = runepkg_host_get_architecture();
    if (arch && std::strcmp(arch, "unknown") != 0) return arch;
    return "amd64";
}

namespace fs = std::filesystem;

enum class PkgDomain : uint8_t {
    DOMAIN_RUNEPKG_NATIVE = 0,
    DOMAIN_HOST_DPKG      = 1,
    DOMAIN_TOOLCHAIN_SYS  = 2
};

enum class ResolveMode : uint8_t {
    MODE_INSTALL   = 0,
    MODE_BUILD     = 1,
    MODE_TOOLCHAIN = 2,
    MODE_HOST_DEPS = 3
};

struct RuneGraphEntry {
    std::string package;
    std::string version;
    std::string arch;
    std::string directory;  // for source
    std::string filename;   // for binary (relative to repo root)
    std::string source_pkg; // cross-reference to source name (for binary pkgs)
    size_t size = 0;        // for binary (download size)

    std::vector<std::string> target_build_depends;
    std::vector<std::string> host_tools;

    std::vector<std::string> depends;      // runtime
    std::vector<std::string> pre_depends;  // critical runtime

    std::vector<std::string> provides;
    std::vector<std::string> conflicts;
    std::vector<std::string> breaks;
    PkgDomain domain = PkgDomain::DOMAIN_RUNEPKG_NATIVE;
};

class RuneResolverEngine {
public:
    RuneResolverEngine() {
        init_filter_tables();
    }

    int harvest(const std::string& sources_dir, const std::string& out_db_path) {
        std::cout << "\033[1;34m[harvest]\033[0m Starting parallel graph analysis on: " << sources_dir << std::endl;

        if (!fs::exists(sources_dir)) {
            std::cerr << "ERROR: Sources directory not found: " << sources_dir << std::endl;
            return -1;
        }

        std::vector<fs::path> sources_files;
        try {
            for (const auto& entry : fs::recursive_directory_iterator(sources_dir, fs::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file()) {
                    std::string fname = entry.path().filename().string();
                    if (fname.find("Sources") != std::string::npos || fname.find(".dsc") != std::string::npos ||
                        fname.find("Packages") != std::string::npos || fname == "pkginfo.bin") {
                        sources_files.push_back(entry.path());
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "WARNING: Directory iteration error: " << e.what() << std::endl;
        }

        std::cout << "  -> Found " << sources_files.size() << " index partitions to analyze." << std::endl;

        auto start = std::chrono::high_resolution_clock::now();
        std::unordered_map<std::string, RuneGraphEntry> graph;

        for (const auto& sfile : sources_files) {
            std::string filename = sfile.filename().string();
            if (filename == "pkginfo.bin") {
                parse_pkginfo_file(sfile.string(), graph);
            } else if (filename.find("Packages") != std::string::npos) {
                parse_packages_file(sfile.string(), graph);
            } else {
                parse_sources_file(sfile.string(), graph);
            }
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start).count();

        std::cout << "\033[1;32m[success]\033[0m Processed " << graph.size()
                  << " source rune stanzas in " << elapsed << "ms." << std::endl;

        return save_binary_graph(out_db_path, graph);
    }

    static void populate_host_installed_map(std::unordered_map<std::string, std::string>& host_map) {
        const char *db_dirs[2];
        int d;

        if (runepkg_main_hash_table) {
            for (size_t i = 0; i < runepkg_main_hash_table->size; i++) {
                runepkg_hash_node_t *node = runepkg_main_hash_table->buckets[i];
                while (node) {
                    if (node->data.package_name && node->data.version) {
                        std::string pname = clean_package_key(node->data.package_name);
                        if (!pname.empty()) {
                            host_map[pname] = node->data.version;
                        }
                    }
                    node = node->next;
                }
            }
        }

        if (!g_runepkg_db_dir) return;

        db_dirs[0] = g_runepkg_db_dir;
        char host_sub[PATH_MAX];
        snprintf(host_sub, sizeof(host_sub), "%s/host", g_runepkg_db_dir);
        db_dirs[1] = host_sub;

        for (d = 0; d < 2; d++) {
            const char *cdir = db_dirs[d];
            DIR *dir = opendir(cdir);
            if (!dir) continue;

            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
                    strcmp(entry->d_name, "lists") == 0 || strcmp(entry->d_name, "host") == 0) continue;

                const char *sep = runepkg_util_find_version_separator(entry->d_name);
                if (!sep) continue;

                std::string raw_name(entry->d_name, (size_t)(sep - entry->d_name));
                std::string ver_str(sep + 1);
                std::string pkg_name = clean_package_key(raw_name);

                if (!pkg_name.empty()) {
                    host_map[pkg_name] = ver_str;
                }
            }
            closedir(dir);
        }
    }

    int resolve_tree_multiple(const std::vector<std::string>& pkg_names, ResolveMode mode, RuneTargetPlan** out_plan) {
        if (pkg_names.empty()) return -1;

        std::string db_path = get_default_db_path();
        std::unordered_map<std::string, RuneGraphEntry> graph;

        if (load_binary_graph(db_path, graph) != 0) {
            std::cout << "\033[1;33m[notice]\033[0m Harvest index missing. Running live analysis on cached indexes..." << std::endl;
            std::string lists_dir = g_runepkg_lists_dir ? g_runepkg_lists_dir :
                                   (g_runepkg_db_dir ? std::string(g_runepkg_db_dir) + "/lists" : "/var/lib/runepkg_dir/runepkg_db/lists");
            if (harvest(lists_dir, db_path) == 0) {
                load_binary_graph(db_path, graph);
            }
        }

        /* Load host packages to prevent resolving tools already satisfied on the host */
        std::unordered_map<std::string, std::string> host_installed_map;
        std::string host_db = get_default_host_db_path();
        if (fs::exists(host_db)) {
            std::unordered_map<std::string, RuneGraphEntry> host_graph;
            if (load_binary_graph(host_db, host_graph) == 0) {
                for (const auto& [h_name, h_node] : host_graph) {
                    std::string cname = clean_package_key(h_name);
                    host_installed_map[cname] = h_node.version;
                    for (const auto& prov : h_node.provides) {
                        std::string cprov = clean_package_key(prov);
                        if (host_installed_map.find(cprov) == host_installed_map.end()) {
                            host_installed_map[cprov] = h_node.version;
                        }
                    }
                    if (cname == "debhelper") {
                        host_installed_map["debhelper-compat"] = h_node.version;
                    }
                }
            }
        }
        /* Dynamically populate installed packages from runepkg_db and host directories */
        populate_host_installed_map(host_installed_map);

        /* Build virtual-to-real mapping across the binary and source graph */
        std::unordered_map<std::string, std::string> virtual_to_real;
        for (const auto& [name, node] : graph) {
            for (const auto& prov : node.provides) {
                std::string c_prov = clean_package_key(prov);
                if (virtual_to_real.find(c_prov) == virtual_to_real.end()) {
                    virtual_to_real[c_prov] = name;
                } else {
                    std::string current = virtual_to_real[c_prov];
                    bool name_installed = host_installed_map.count(clean_package_key(name));
                    bool current_installed = host_installed_map.count(clean_package_key(current));

                    if (name_installed && !current_installed) {
                        virtual_to_real[c_prov] = name;
                    } else if (name_installed == current_installed) {
                        if (name.find("-base") != std::string::npos && current.find("-base") == std::string::npos) {
                            virtual_to_real[c_prov] = name;
                        } else if (name.length() < current.length() && current.find("-base") == std::string::npos) {
                            virtual_to_real[c_prov] = name;
                        }
                    }
                }
            }
        }

        std::vector<RuneGraphEntry> resolved_order;
        std::set<std::string> visited;
        std::set<std::string> visiting;

        for (const auto& raw_pkg_name : pkg_names) {
            std::string root_target = clean_package_key(raw_pkg_name);
            if (mode == ResolveMode::MODE_BUILD || mode == ResolveMode::MODE_TOOLCHAIN) {
                char *src_name = runepkg_repo_find_source_for_binary(raw_pkg_name.c_str());
                if (src_name) {
                    root_target = clean_package_key(src_name);
                    free(src_name);
                }
            }

            if (graph.find(root_target) == graph.end() && virtual_to_real.count(root_target)) {
                root_target = virtual_to_real.at(root_target);
            }

            if ((mode == ResolveMode::MODE_BUILD || mode == ResolveMode::MODE_TOOLCHAIN) &&
                root_target.rfind("src:", 0) != 0) {
                std::string src_key = "src:" + root_target;
                if (graph.count(src_key)) {
                    root_target = src_key;
                } else {
                    auto it = graph.find(root_target);
                    if (it != graph.end() && !it->second.source_pkg.empty()) {
                        std::string cross_src = "src:" + it->second.source_pkg;
                        if (graph.count(cross_src)) root_target = cross_src;
                    }
                }
            }

            if (graph.find(root_target) != graph.end()) {
                dfs_resolve_v2(root_target, mode, graph, virtual_to_real, host_installed_map, visited, visiting, resolved_order, true);
            }
        }

        /* Deduplicate execution array while preserving bottom-up topological order */
        std::vector<RuneGraphEntry> deduplicated_order;
        std::unordered_set<std::string> seen_pkgs;
        for (const auto& item : resolved_order) {
            std::string clean_pkg_name = clean_package_key(item.package);
            if (seen_pkgs.find(clean_pkg_name) == seen_pkgs.end()) {
                seen_pkgs.insert(clean_pkg_name);
                deduplicated_order.push_back(item);
            }
        }

        if (out_plan) {
            RuneTargetPlan *plan = (RuneTargetPlan*)malloc(sizeof(RuneTargetPlan));
            plan->node_count = deduplicated_order.size();
            plan->nodes = (RuneTargetNode*)calloc(plan->node_count, sizeof(RuneTargetNode));

            for (size_t i = 0; i < deduplicated_order.size(); i++) {
                std::string display_name = deduplicated_order[i].package;
                if (display_name.rfind("src:", 0) == 0) display_name = display_name.substr(4);

                plan->nodes[i].package_name = strdup(display_name.c_str());
                plan->nodes[i].version = strdup(deduplicated_order[i].version.c_str());
                plan->nodes[i].arch = strdup(deduplicated_order[i].arch.c_str());

                if (mode == ResolveMode::MODE_INSTALL || mode == ResolveMode::MODE_HOST_DEPS) {
                    plan->nodes[i].source_name = strdup(display_name.c_str());
                    plan->nodes[i].binary_filename = strdup(deduplicated_order[i].filename.c_str());
                    plan->nodes[i].download_size = deduplicated_order[i].size;
                } else {
                    std::string src_name = deduplicated_order[i].package;
                    if (src_name.rfind("src:", 0) == 0) src_name = src_name.substr(4);
                    plan->nodes[i].source_name = strdup(src_name.c_str());
                }

                plan->nodes[i].target_build_depends_count = deduplicated_order[i].target_build_depends.size();
                plan->nodes[i].target_build_depends = (char**)calloc(plan->nodes[i].target_build_depends_count + 1, sizeof(char*));
                for (size_t j = 0; j < deduplicated_order[i].target_build_depends.size(); j++) {
                    plan->nodes[i].target_build_depends[j] = strdup(deduplicated_order[i].target_build_depends[j].c_str());
                }

                plan->nodes[i].host_tools_count = deduplicated_order[i].host_tools.size();
                plan->nodes[i].host_tools_required = (char**)calloc(plan->nodes[i].host_tools_count + 1, sizeof(char*));
                for (size_t j = 0; j < deduplicated_order[i].host_tools.size(); j++) {
                    plan->nodes[i].host_tools_required[j] = strdup(deduplicated_order[i].host_tools[j].c_str());
                }
            }
            *out_plan = plan;
        }

        return 0;
    }

    int resolve_tree(const std::string& pkg_name, ResolveMode mode, RuneTargetPlan** out_plan) {
        std::vector<std::string> pkgs = { pkg_name };
        return resolve_tree_multiple(pkgs, mode, out_plan);
    }

    void dump_tree_view(const std::string& pkg_name) {
        std::string db_path = get_default_db_path();
        std::unordered_map<std::string, RuneGraphEntry> graph;

        if (load_binary_graph(db_path, graph) != 0) {
            std::string lists_dir = g_runepkg_lists_dir ? g_runepkg_lists_dir :
                                   (g_runepkg_db_dir ? std::string(g_runepkg_db_dir) + "/lists" : "/var/lib/runepkg_dir/runepkg_db/lists");
            harvest(lists_dir, db_path);
            load_binary_graph(db_path, graph);
        }

        std::string root = clean_package_key(pkg_name);
        if (graph.find(root) == graph.end()) {
            std::string src_key = "src:" + root;
            if (graph.count(src_key)) root = src_key;
            else {
                char *src_res = runepkg_repo_find_source_for_binary(pkg_name.c_str());
                if (src_res) {
                    root = "src:" + std::string(src_res);
                    free(src_res);
                }
            }
        }

        if (graph.find(root) == graph.end()) {
            std::cerr << "ERROR: Package '" << pkg_name << "' not found." << std::endl;
            return;
        }

        std::cout << "\033[1;34m[tree]\033[0m Dependency Visualization for \033[1;32m" << pkg_name << "\033[0m" << std::endl;
        std::unordered_set<std::string> seen;
        dump_node_recursive(root, "", true, graph, seen);
    }

private:
    void dump_node_recursive(const std::string& pkg, const std::string& prefix, bool is_last,
                            const std::unordered_map<std::string, RuneGraphEntry>& graph,
                            std::unordered_set<std::string>& seen) {
        auto it = graph.find(pkg);
        if (it == graph.end()) return;

        std::string display_name = it->second.package;
        if (display_name.rfind("src:", 0) == 0) display_name = display_name.substr(4);

        std::cout << prefix << (is_last ? "└── " : "├── ") << "\033[1;36m" << display_name << "\033[0m";

        if (seen.count(pkg)) {
            std::cout << " \033[1;33m(already listed)\033[0m" << std::endl;
            return;
        }
        std::cout << " (" << it->second.version << ")" << std::endl;
        seen.insert(pkg);

        std::vector<std::string> deps;
        /* For build tree, we focus on target build depends */
        if (pkg.rfind("src:", 0) == 0) {
            deps = it->second.target_build_depends;
        } else {
            deps = it->second.depends;
            for (const auto& pd : it->second.pre_depends) deps.push_back(pd);
        }

        /* Clean and resolve dependencies to their real names in graph */
        std::vector<std::string> resolved_deps;
        for (const auto& d : deps) {
            std::string cd = clean_package_key(d);
            if (graph.count(cd)) resolved_deps.push_back(cd);
            else if (graph.count("src:" + cd)) resolved_deps.push_back("src:" + cd);
        }

        std::string new_prefix = prefix + (is_last ? "    " : "│   ");
        for (size_t i = 0; i < resolved_deps.size(); i++) {
            dump_node_recursive(resolved_deps[i], new_prefix, (i == resolved_deps.size() - 1), graph, seen);
        }
    }

private:
    std::set<std::string> host_tools_set_;
    std::vector<std::string> bloat_prefixes_;

    static std::string clean_package_key(const std::string& raw) {
        std::string res = raw;
        if (res.rfind("src:", 0) == 0) res = res.substr(4);
        size_t colon_pos = res.find(':');
        if (colon_pos != std::string::npos) res = res.substr(0, colon_pos);
        size_t ver_pos = res.find_first_of(" (<[=");
        if (ver_pos != std::string::npos) res = res.substr(0, ver_pos);
        res.erase(0, res.find_first_not_of(" \t\r\n"));
        res.erase(res.find_last_not_of(" \t\r\n") + 1);
        return res;
    }

    void init_filter_tables() {
        host_tools_set_ = {
            "debhelper", "debhelper-compat", "dpkg-dev", "quilt", "dh-autoreconf",
            "dh-strip-nondeterminism", "autotools-dev", "automake", "autoconf",
            "m4", "bison", "flex", "gettext", "pkg-config", "pkgconf", "make",
            "cmake", "meson", "ninja-build", "help2man", "patch", "patchutils",
            "diffstat", "po4a", "po-debconf", "chrpath", "gperf", "xsltproc",
            "gcc", "g++", "clang", "binutils", "perl", "python3"
        };

        bloat_prefixes_ = {
            "texlive-", "python3-sphinx", "python3-pytest", "doxygen",
            "asciidoc", "docbook-", "default-jdk", "libghc-", "ocaml-",
            "ruby-", "node-", "libtest-", "fonts-", "valgrind"
        };
    }

    std::string get_default_db_path() {
        if (g_runepkg_db_dir) return std::string(g_runepkg_db_dir) + "/runes_graph.bin";
        return "/var/lib/runepkg_dir/runepkg_db/runes_graph.bin";
    }

    std::string get_default_host_db_path() {
        if (g_runepkg_db_dir) return std::string(g_runepkg_db_dir) + "/runes_host.bin";
        return "/var/lib/runepkg_dir/runepkg_db/runes_host.bin";
    }

    std::vector<std::string> parse_depends_vector(const std::string& raw) {
        std::vector<std::string> result;
        if (raw.empty()) return result;
        char **parsed = parse_depends(raw.c_str());
        if (parsed) {
            for (int i = 0; parsed[i]; i++) {
                result.push_back(parsed[i]);
                free(parsed[i]);
            }
            free(parsed);
        }
        return result;
    }

    void parse_pkginfo_file(const std::string& path, std::unordered_map<std::string, RuneGraphEntry>& graph) {
        PkgInfo info;
        char* dir = strdup(path.c_str());
        char* dname = dirname(dir);
        char* bname = basename(dname);

        std::string pkg_ver = bname;
        size_t dash = pkg_ver.find_last_of('-');
        if (dash == std::string::npos) { free(dir); return; }

        std::string name = pkg_ver.substr(0, dash);
        std::string ver = pkg_ver.substr(dash + 1);

        char* old_db = g_runepkg_db_dir;
        char* host_root = dirname(dname);
        g_runepkg_db_dir = host_root;
        if (runepkg_storage_read_package_info(name.c_str(), ver.c_str(), &info) == 0) {
            RuneGraphEntry entry;
            entry.package = name;
            entry.version = ver;
            if (info.architecture) entry.arch = info.architecture;

            entry.depends = parse_depends_vector(info.depends ? info.depends : "");
            entry.pre_depends = parse_depends_vector(info.pre_depends ? info.pre_depends : "");
            entry.conflicts = parse_depends_vector(info.conflicts ? info.conflicts : "");
            entry.breaks = parse_depends_vector(info.breaks ? info.breaks : "");

            if (info.provides) {
                char **parsed = parse_depends(info.provides);
                if (parsed) {
                    for (int i = 0; parsed[i]; i++) {
                        std::string prov = clean_package_key(parsed[i]);
                        entry.provides.push_back(prov);
                        free(parsed[i]);
                    }
                    free(parsed);
                }
            }
            entry.domain = PkgDomain::DOMAIN_HOST_DPKG;
            graph[name] = entry;
            runepkg_pack_free_package_info(&info);
        }
        g_runepkg_db_dir = old_db;
        free(dir);
    }

    void parse_packages_file(const std::string& path, std::unordered_map<std::string, RuneGraphEntry>& graph) {
        std::ifstream file(path);
        if (!file.is_open()) return;

        std::string line;
        RuneGraphEntry current;
        bool in_entry = false;

        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.compare(0, 9, "Package: ") == 0) {
                if (in_entry && !current.package.empty()) {
                    const char* target_arch = get_effective_arch();
                    if (current.arch == "all" || current.arch == target_arch || current.arch.empty()) {
                        graph[current.package] = current;
                    }
                    current = RuneGraphEntry();
                }
                in_entry = true;
                current.package = line.substr(9);
                current.package.erase(0, current.package.find_first_not_of(" \t"));
                current.package.erase(current.package.find_last_not_of(" \t") + 1);
            } else if (line.compare(0, 8, "Source: ") == 0) {
                current.source_pkg = line.substr(8);
                current.source_pkg.erase(0, current.source_pkg.find_first_not_of(" \t"));
                current.source_pkg.erase(current.source_pkg.find_last_not_of(" \t") + 1);
                size_t ver_start = current.source_pkg.find_first_of(" (");
                if (ver_start != std::string::npos) current.source_pkg = current.source_pkg.substr(0, ver_start);
            } else if (line.compare(0, 9, "Version: ") == 0) {
                current.version = line.substr(9);
            } else if (line.compare(0, 14, "Architecture: ") == 0) {
                current.arch = line.substr(14);
            } else if (line.compare(0, 10, "Filename: ") == 0) {
                current.filename = line.substr(10);
            } else if (line.compare(0, 6, "Size: ") == 0) {
                try { current.size = std::stoull(line.substr(6)); } catch (...) { current.size = 0; }
            } else if (line.compare(0, 9, "Depends: ") == 0) {
                current.depends = parse_depends_vector(line.substr(9));
            } else if (line.compare(0, 13, "Pre-Depends: ") == 0) {
                current.pre_depends = parse_depends_vector(line.substr(13));
            } else if (line.compare(0, 11, "Conflicts: ") == 0) {
                current.conflicts = parse_depends_vector(line.substr(11));
            } else if (line.compare(0, 8, "Breaks: ") == 0) {
                current.breaks = parse_depends_vector(line.substr(8));
            } else if (line.compare(0, 10, "Provides: ") == 0) {
                std::string raw_prov = line.substr(10);
                char **parsed = parse_depends(raw_prov.c_str());
                if (parsed) {
                    for (int i = 0; parsed[i]; i++) {
                        std::string prov = clean_package_key(parsed[i]);
                        current.provides.push_back(prov);
                        free(parsed[i]);
                    }
                    free(parsed);
                }
            }
        }
        if (in_entry && !current.package.empty()) {
            const char* target_arch = get_effective_arch();
            if (current.arch == "all" || current.arch == target_arch || current.arch.empty()) {
                graph[current.package] = current;
            }
        }
    }

    void parse_sources_file(const std::string& path, std::unordered_map<std::string, RuneGraphEntry>& graph) {
        std::ifstream file(path);
        if (!file.is_open()) return;

        std::string line;
        RuneGraphEntry current;
        bool in_entry = false;

        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.compare(0, 8, "Package:") == 0 || line.compare(0, 8, "Source: ") == 0) {
                if (in_entry && !current.package.empty()) {
                    graph[current.package] = current;
                    current = RuneGraphEntry();
                }
                in_entry = true;
                std::string name = line.substr(8);
                name.erase(0, name.find_first_not_of(" \t"));
                name.erase(name.find_last_not_of(" \t") + 1);
                if (name.rfind("src:", 0) != 0) current.package = "src:" + name;
                else current.package = name;
            } else if (line.compare(0, 9, "Version: ") == 0) {
                current.version = line.substr(9);
                current.version.erase(0, current.version.find_first_not_of(" \t"));
            } else if (line.compare(0, 11, "Directory: ") == 0) {
                current.directory = line.substr(11);
                current.directory.erase(0, current.directory.find_first_not_of(" \t"));
            } else if (line.compare(0, 10, "Provides: ") == 0) {
                std::string raw_prov = line.substr(10);
                char **parsed = parse_depends(raw_prov.c_str());
                if (parsed) {
                    for (int i = 0; parsed[i]; i++) {
                        std::string prov = clean_package_key(parsed[i]);
                        current.provides.push_back(prov);
                        free(parsed[i]);
                    }
                    free(parsed);
                }
            } else if (line.compare(0, 15, "Build-Depends: ") == 0 ||
                       line.compare(0, 20, "Build-Depends-Arch: ") == 0 ||
                       line.compare(0, 21, "Build-Depends-Indep: ") == 0) {
                size_t colon = line.find(':');
                std::string raw_deps = line.substr(colon + 1);
                char **parsed = parse_depends(raw_deps.c_str());
                if (parsed) {
                    for (int i = 0; parsed[i]; i++) {
                        std::string dep = parsed[i];
                        std::string c_dep = clean_package_key(dep);
                        if (host_tools_set_.count(c_dep) || c_dep.rfind("dh-", 0) == 0) {
                            current.host_tools.push_back(c_dep);
                        } else {
                            bool bloat = false;
                            for (const auto& p : bloat_prefixes_) {
                                if (c_dep.rfind(p, 0) == 0) { bloat = true; break; }
                            }
                            if (!bloat && (c_dep.rfind("lib", 0) == 0 || (c_dep.length() > 4 && c_dep.substr(c_dep.length() - 4) == "-dev"))) {
                                current.target_build_depends.push_back(c_dep);
                            }
                        }
                        free(parsed[i]);
                    }
                    free(parsed);
                }
            }
        }

        if (in_entry && !current.package.empty()) {
            const char* target_arch = get_effective_arch();
            if (current.arch == "all" || current.arch == target_arch || current.arch.empty()) {
                graph[current.package] = current;
            }
        }
    }

    static std::string extract_constraint_string(const std::string& raw) {
        size_t paren_start = raw.find('(');
        size_t paren_end = raw.find(')', paren_start);
        if (paren_start != std::string::npos && paren_end != std::string::npos && paren_end > paren_start) {
            return raw.substr(paren_start + 1, paren_end - paren_start - 1);
        }
        return "";
    }

    static bool is_installed_satisfied(const std::string& dep_raw, const std::unordered_map<std::string, std::string>& host_installed_map) {
        std::string key = clean_package_key(dep_raw);
        std::string constraint = extract_constraint_string(dep_raw);

        /* Ground Truth: Check direct disk storage using runepkg_storage_package_exists() */
        if (constraint.empty()) {
            if (runepkg_storage_package_exists(key.c_str(), NULL) == 1) {
                return true;
            }
        } else {
            size_t eq_pos = constraint.find('=');
            if (eq_pos != std::string::npos) {
                std::string target_ver = constraint.substr(eq_pos + 1);
                target_ver.erase(0, target_ver.find_first_not_of(" \t\r\n="));
                target_ver.erase(target_ver.find_last_not_of(" \t\r\n") + 1);
                if (!target_ver.empty() && runepkg_storage_package_exists(key.c_str(), target_ver.c_str()) == 1) {
                    return true;
                }
            }
        }

        auto it = host_installed_map.find(key);
        if (it == host_installed_map.end()) {
            return false;
        }

        if (constraint.empty()) {
            return true;
        }

        int res = runepkg_util_check_version_constraint(it->second.c_str(), constraint.c_str());
        return (res == 1);
    }

    bool dfs_resolve_v2(const std::string& pkg,
                       ResolveMode mode,
                       const std::unordered_map<std::string, RuneGraphEntry>& graph,
                       const std::unordered_map<std::string, std::string>& virtual_to_real,
                       const std::unordered_map<std::string, std::string>& host_installed_map,
                       std::set<std::string>& visited,
                       std::set<std::string>& visiting,
                       std::vector<RuneGraphEntry>& order,
                       bool is_root_target = false) {
        std::string check_name = clean_package_key(pkg);

        if (!is_root_target && (mode == ResolveMode::MODE_INSTALL || mode == ResolveMode::MODE_HOST_DEPS) && is_installed_satisfied(pkg, host_installed_map)) {
            visited.insert(pkg);
            return true;
        }

        if (visiting.count(pkg)) return true;
        if (visited.count(pkg)) return true;

        visiting.insert(pkg);

        auto it = graph.find(pkg);
        if (it != graph.end()) {
            if (mode == ResolveMode::MODE_INSTALL || mode == ResolveMode::MODE_TOOLCHAIN || mode == ResolveMode::MODE_HOST_DEPS) {
                for (const auto& dep : it->second.pre_depends) {
                    resolve_dep_node(dep, mode, graph, virtual_to_real, host_installed_map, visited, visiting, order);
                }
                for (const auto& dep : it->second.depends) {
                    resolve_dep_node(dep, mode, graph, virtual_to_real, host_installed_map, visited, visiting, order);
                }
            } else if (mode == ResolveMode::MODE_BUILD) {
                for (const auto& dep : it->second.target_build_depends) {
                    resolve_dep_node(dep, mode, graph, virtual_to_real, host_installed_map, visited, visiting, order);
                }
            }

            if (is_root_target || !(mode == ResolveMode::MODE_HOST_DEPS && is_installed_satisfied(pkg, host_installed_map))) {
                order.push_back(it->second);
            }
        }

        visiting.erase(pkg);
        visited.insert(pkg);
        return true;
    }

private:
    void resolve_dep_node(const std::string& dep,
                          ResolveMode mode,
                          const std::unordered_map<std::string, RuneGraphEntry>& graph,
                          const std::unordered_map<std::string, std::string>& virtual_to_real,
                          const std::unordered_map<std::string, std::string>& host_installed_map,
                          std::set<std::string>& visited,
                          std::set<std::string>& visiting,
                          std::vector<RuneGraphEntry>& order) {
        std::string target_key = clean_package_key(dep);

        if ((mode == ResolveMode::MODE_INSTALL || mode == ResolveMode::MODE_HOST_DEPS) && is_installed_satisfied(dep, host_installed_map)) {
            return;
        }

        if (mode == ResolveMode::MODE_BUILD || mode == ResolveMode::MODE_TOOLCHAIN) {
            if (target_key.rfind("src:", 0) != 0) {
                std::string src_key = "src:" + target_key;
                if (graph.count(src_key)) {
                    target_key = src_key;
                } else {
                    auto it = graph.find(target_key);
                    if (it != graph.end() && !it->second.source_pkg.empty()) {
                        std::string cross_src = "src:" + it->second.source_pkg;
                        if (graph.count(cross_src)) target_key = cross_src;
                    } else if (it == graph.end() && virtual_to_real.count(target_key)) {
                        std::string real_pkg = virtual_to_real.at(target_key);
                        auto it2 = graph.find(real_pkg);
                        if (it2 != graph.end()) {
                            std::string cross_src = it2->second.source_pkg.empty() ? real_pkg : it2->second.source_pkg;
                            if (graph.count("src:" + cross_src)) target_key = "src:" + cross_src;
                            else target_key = real_pkg;
                        }
                    } else {
                        char* src_resolved = runepkg_repo_find_source_for_binary(target_key.c_str());
                        if (src_resolved) {
                            std::string ffi_src = "src:" + std::string(src_resolved);
                            if (graph.count(ffi_src)) target_key = ffi_src;
                            free(src_resolved);
                        }
                    }
                }
            }
        }

        if (graph.count(target_key)) {
            dfs_resolve_v2(target_key, mode, graph, virtual_to_real, host_installed_map, visited, visiting, order);
        } else if (virtual_to_real.count(target_key)) {
            dfs_resolve_v2(virtual_to_real.at(target_key), mode, graph, virtual_to_real, host_installed_map, visited, visiting, order);
        } else {
            /* Vigilant Version Sifting:
             * If target_key is name-version (e.g. perlapi-5.42.2), sift through available
             * providers of 'name-*' if the exact version isn't found.
             */
            size_t dash_pos = target_key.find_last_of('-');
            if (dash_pos != std::string::npos && dash_pos > 0) {
                std::string prefix = target_key.substr(0, dash_pos + 1); /* includes the dash */
                for (const auto& [v_name, r_name] : virtual_to_real) {
                    if (v_name.compare(0, prefix.length(), prefix) == 0) {
                         runepkg_util_log_verbose("[resolver] Vigilant substitution: requested %s, found %s (provided by %s)\n",
                                                  target_key.c_str(), v_name.c_str(), r_name.c_str());
                         dfs_resolve_v2(r_name, mode, graph, virtual_to_real, host_installed_map, visited, visiting, order);
                         return;
                    }
                }
            }
        }
    }

    int save_binary_graph(const std::string& out_path, const std::unordered_map<std::string, RuneGraphEntry>& graph) {
        std::ofstream out(out_path, std::ios::binary);
        if (!out.is_open()) return -1;

        const char magic[8] = {'R', 'U', 'N', 'E', 'G', 'R', 'P', 'H'};
        out.write(magic, 8);

        uint32_t count = graph.size();
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));

        for (const auto& [name, node] : graph) {
            write_string(out, node.package);
            write_string(out, node.version);
            write_string(out, node.arch);
            write_string(out, node.directory);
            write_string(out, node.filename);
            write_string(out, node.source_pkg);

            uint64_t size_val = node.size;
            out.write(reinterpret_cast<const char*>(&size_val), sizeof(size_val));

            uint32_t td_count = node.target_build_depends.size();
            out.write(reinterpret_cast<const char*>(&td_count), sizeof(td_count));
            for (const auto& d : node.target_build_depends) write_string(out, d);

            uint32_t ht_count = node.host_tools.size();
            out.write(reinterpret_cast<const char*>(&ht_count), sizeof(ht_count));
            for (const auto& h : node.host_tools) write_string(out, h);

            uint32_t dp_count = node.depends.size();
            out.write(reinterpret_cast<const char*>(&dp_count), sizeof(dp_count));
            for (const auto& d : node.depends) write_string(out, d);

            uint32_t pr_count = node.pre_depends.size();
            out.write(reinterpret_cast<const char*>(&pr_count), sizeof(pr_count));
            for (const auto& p : node.pre_depends) write_string(out, p);

            uint32_t pv_count = node.provides.size();
            out.write(reinterpret_cast<const char*>(&pv_count), sizeof(pv_count));
            for (const auto& p : node.provides) write_string(out, p);

            uint8_t dom = (uint8_t)node.domain;
            out.write(reinterpret_cast<const char*>(&dom), sizeof(dom));
        }

        std::cout << "  -> Serialized binary graph to " << out_path << " (" << (out.tellp() / 1024) << " KB)" << std::endl;
        return 0;
    }

    int load_binary_graph(const std::string& in_path, std::unordered_map<std::string, RuneGraphEntry>& graph) {
        std::ifstream in(in_path, std::ios::binary);
        if (!in.is_open()) return -1;

        char magic[8];
        in.read(magic, 8);
        if (memcmp(magic, "RUNEGRPH", 8) != 0) return -1;

        uint32_t count = 0;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));

        for (uint32_t i = 0; i < count; i++) {
            RuneGraphEntry node;
            node.package = read_string(in);
            node.version = read_string(in);
            node.arch = read_string(in);
            node.directory = read_string(in);
            node.filename = read_string(in);
            node.source_pkg = read_string(in);

            uint64_t size_val = 0;
            in.read(reinterpret_cast<char*>(&size_val), sizeof(size_val));
            node.size = (size_t)size_val;

            uint32_t td_count = 0;
            in.read(reinterpret_cast<char*>(&td_count), sizeof(td_count));
            for (uint32_t j = 0; j < td_count; j++) node.target_build_depends.push_back(read_string(in));

            uint32_t ht_count = 0;
            in.read(reinterpret_cast<char*>(&ht_count), sizeof(ht_count));
            for (uint32_t j = 0; j < ht_count; j++) node.host_tools.push_back(read_string(in));

            uint32_t dp_count = 0;
            in.read(reinterpret_cast<char*>(&dp_count), sizeof(dp_count));
            for (uint32_t j = 0; j < dp_count; j++) node.depends.push_back(read_string(in));

            uint32_t pr_count = 0;
            in.read(reinterpret_cast<char*>(&pr_count), sizeof(pr_count));
            for (uint32_t j = 0; j < pr_count; j++) node.pre_depends.push_back(read_string(in));

            uint32_t pv_count = 0;
            in.read(reinterpret_cast<char*>(&pv_count), sizeof(pv_count));
            for (uint32_t j = 0; j < pv_count; j++) node.provides.push_back(read_string(in));

            uint8_t dom = 0;
            in.read(reinterpret_cast<char*>(&dom), sizeof(dom));
            node.domain = (PkgDomain)dom;

            graph[node.package] = node;
        }
        return 0;
    }

    void write_string(std::ofstream& out, const std::string& str) {
        uint16_t len = str.length();
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        if (len > 0) out.write(str.data(), len);
    }

    std::string read_string(std::ifstream& in) {
        uint16_t len = 0;
        in.read(reinterpret_cast<char*>(&len), sizeof(len));
        if (len == 0) return "";
        std::string str(len, '\0');
        in.read(&str[0], len);
        return str;
    }
};

extern "C" int runepkg_resolver_harvest_graph(const char *sources_dir, const char *out_db_path) {
    try {
        std::string s_dir = sources_dir ? sources_dir : (g_runepkg_lists_dir ? g_runepkg_lists_dir : (g_runepkg_db_dir ? std::string(g_runepkg_db_dir) + "/lists" : "/var/lib/runepkg_dir/runepkg_db/lists"));
        std::string o_path = out_db_path ? out_db_path : (g_runepkg_db_dir ? std::string(g_runepkg_db_dir) + "/runes_graph.bin" : "/var/lib/runepkg_dir/runepkg_db/runes_graph.bin");

        RuneResolverEngine engine;
        return engine.harvest(s_dir, o_path);
    } catch (...) {
        return -1;
    }
}

extern "C" int runepkg_resolver_resolve_target(const char *pkg_name, RuneTargetPlan **out_plan) {
    try {
        if (!pkg_name || !out_plan) return -1;
        RuneResolverEngine engine;
        return engine.resolve_tree(pkg_name, ResolveMode::MODE_BUILD, out_plan);
    } catch (...) {
        if (out_plan) *out_plan = NULL;
        return -1;
    }
}

extern "C" int runepkg_resolver_get_install_plan_multiple(const char **pkg_names, int count, RuneTargetPlan **out_plan) {
    try {
        if (!pkg_names || count <= 0 || !out_plan) return -1;
        std::vector<std::string> pkgs;
        for (int i = 0; i < count; i++) {
            if (pkg_names[i]) pkgs.push_back(pkg_names[i]);
        }
        if (pkgs.empty()) return -1;
        RuneResolverEngine engine;
        return engine.resolve_tree_multiple(pkgs, ResolveMode::MODE_HOST_DEPS, out_plan);
    } catch (...) {
        if (out_plan) *out_plan = NULL;
        return -1;
    }
}

extern "C" int runepkg_resolver_get_install_plan(const char *pkg_name, RuneTargetPlan **out_plan) {
    if (!pkg_name) return -1;
    return runepkg_resolver_get_install_plan_multiple(&pkg_name, 1, out_plan);
}

extern "C" void runepkg_resolver_free_plan(RuneTargetPlan *plan) {
    try {
        if (!plan) return;
        for (int i = 0; i < plan->node_count; i++) {
            free(plan->nodes[i].package_name);
            free(plan->nodes[i].version);
            if (plan->nodes[i].arch) free(plan->nodes[i].arch);
            free(plan->nodes[i].source_name);
            if (plan->nodes[i].binary_filename) free(plan->nodes[i].binary_filename);

            if (plan->nodes[i].target_build_depends) {
                for (int j = 0; j < plan->nodes[i].target_build_depends_count; j++) {
                    free(plan->nodes[i].target_build_depends[j]);
                }
                free(plan->nodes[i].target_build_depends);
            }

            if (plan->nodes[i].host_tools_required) {
                for (int j = 0; j < plan->nodes[i].host_tools_count; j++) {
                    free(plan->nodes[i].host_tools_required[j]);
                }
                free(plan->nodes[i].host_tools_required);
            }
        }
        free(plan->nodes);
        free(plan);
    } catch (...) {
        /* No-op on exception */
    }
}

extern "C" int runepkg_resolver_dump_tree(const char *pkg_name) {
    try {
        if (!pkg_name) return -1;
        RuneResolverEngine engine;
        engine.dump_tree_view(pkg_name);
        return 0;
    } catch (...) {
        return -1;
    }
}
