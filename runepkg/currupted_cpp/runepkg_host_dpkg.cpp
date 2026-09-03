/******************************************************************************
 * Filename:    runepkg_host_dpkg.cpp
 * Author:      <michkochris@gmail.com>
 * Date:        2026-08-28
 * Description: High-speed host dpkg status ingestion (C++ FFI)
 * LICENSE:     GPL v3
 ******************************************************************************/

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <ctime>

extern "C" {
    #include "runepkg_config.h"
    #include "runepkg_util.h"
    #include "runepkg_storage.h"
    #include "runepkg_pack.h"
}

namespace fs = std::filesystem;

class RuneHostDpkgSync {
public:
    int sync() {
        if (!g_dpkg_host || (strcmp(g_dpkg_host, "yes") != 0 && strcmp(g_dpkg_host, "auto") != 0)) {
            return 0;
        }

        const char* status_path = "/var/lib/dpkg/status";
        if (!fs::exists(status_path)) {
            if (strcmp(g_dpkg_host, "yes") == 0) {
                std::cerr << "\033[1;31mError:\033[0m dpkg-host=yes but " << status_path << " not found." << std::endl;
                return -1;
            }
            return 0;
        }

        std::string host_db_root = std::string(g_runepkg_db_dir ? g_runepkg_db_dir : "/var/lib/runepkg_dir/runepkg_db") + "/host";
        fs::create_directories(host_db_root);

        // Check if sync is needed using mtime against the cache stamp
        std::string stamp_path = std::string(g_runepkg_db_dir ? g_runepkg_db_dir : "/var/lib/runepkg_dir/runepkg_db") + "/host_sync.stamp";
        struct stat st;
        if (stat(status_path, &st) == 0) {
            struct stat sst;
            if (stat(stamp_path.c_str(), &sst) == 0) {
                if (st.st_mtime <= sst.st_mtime) {
                    runepkg_log_verbose("Host dpkg status is up to date.\n");
                    return 0;
                }
            }
        }

        std::cout << "\033[1;34m[dpkg-host]\033[0m Ingesting host packages into runepkg registry..." << std::endl;

        int fd = open(status_path, O_RDONLY);
        if (fd < 0) return -1;

        off_t raw_size = lseek(fd, 0, SEEK_END);
        if (raw_size <= 0) {
            close(fd);
            return 0;
        }
        size_t size = (size_t)raw_size;

        void* data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);

        if (data == MAP_FAILED) return -1;

        const char* start = (const char*)data;
        const char* end = start + size;
        const char* p = start;

        int count = 0;
        const char* pattern = "\n\n";
        while (p < end) {
            const char* entry_end = std::search(p, end, pattern, pattern + 2);
            if (entry_end != end) entry_end += 2;

            parse_and_store_entry(p, entry_end, host_db_root);
            count++;
            p = entry_end;
        }

        munmap(data, size);

        // Update stamp
        std::ofstream stamp(stamp_path);
        if (stamp.is_open()) {
            stamp << time(NULL);
            stamp.close();
        }

        std::cout << "\033[1;32m[success]\033[0m Synced " << count << " host packages to " << host_db_root << std::endl;

        /* Rebuild autocomplete index to include newly ingested host packages */
        runepkg_storage_build_autocomplete_index();

        return 0;
    }

private:
    void parse_and_store_entry(const char* start, const char* end, const std::string& host_db_root) {
        std::string pkg, ver, arch, desc, status, deps, pre_deps, provides;
        const char* p = start;

        while (p < end) {
            const char* line_end = std::find(p, end, '\n');
            std::string line(p, line_end);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.compare(0, 9, "Package: ") == 0) pkg = line.substr(9);
            else if (line.compare(0, 9, "Version: ") == 0) ver = line.substr(9);
            else if (line.compare(0, 14, "Architecture: ") == 0) arch = line.substr(14);
            else if (line.compare(0, 8, "Status: ") == 0) status = line.substr(8);
            else if (line.compare(0, 9, "Depends: ") == 0) deps = line.substr(9);
            else if (line.compare(0, 13, "Pre-Depends: ") == 0) pre_deps = line.substr(13);
            else if (line.compare(0, 10, "Provides: ") == 0) provides = line.substr(10);
            else if (line.compare(0, 13, "Description: ") == 0) desc = line.substr(13);

            p = (line_end == end) ? end : line_end + 1;
        }

        if (pkg.empty() || ver.empty() || status.find("installed") == std::string::npos) return;

        PkgInfo info;
        runepkg_pack_init_package_info(&info);
        info.package_name = strdup(pkg.c_str());
        info.version = strdup(ver.c_str());
        info.architecture = strdup(arch.c_str());
        info.description = strdup(desc.c_str());
        info.depends = strdup(deps.c_str());
        info.pre_depends = strdup(pre_deps.c_str());
        info.provides = strdup(provides.c_str());

        /* Scan .list file for installed file manifest */
        std::string list_path = "/var/lib/dpkg/info/" + pkg + ".list";
        if (!fs::exists(list_path)) {
            list_path = "/var/lib/dpkg/info/" + pkg + ":" + arch + ".list";
        }

        if (fs::exists(list_path)) {
            std::ifstream lf(list_path);
            std::string fline;
            std::vector<std::string> files;
            while (std::getline(lf, fline)) {
                if (!fline.empty() && fline[0] == '/') {
                    files.push_back(fline.substr(1)); // strip leading slash
                }
            }
            if (!files.empty()) {
                info.file_count = files.size();
                info.file_list = (char**)malloc(sizeof(char*) * info.file_count);
                for (size_t i = 0; i < files.size(); i++) {
                    info.file_list[i] = strdup(files[i].c_str());
                }
            }
        }

        /* Direct storage entry write into host database root */
        char* old_db = g_runepkg_db_dir;
        g_runepkg_db_dir = (char*)host_db_root.c_str();
        if (runepkg_storage_create_package_directory(pkg.c_str(), ver.c_str()) == 0) {
            runepkg_storage_write_package_info(pkg.c_str(), ver.c_str(), &info);
        }
        g_runepkg_db_dir = old_db;

        runepkg_pack_free_package_info(&info);
    }
};

extern "C" int runepkg_host_dpkg_sync(void) {
    RuneHostDpkgSync syncer;
    return syncer.sync();
}
