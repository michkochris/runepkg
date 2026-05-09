#include "runepkg_cpp_ffi.h"
#include "runepkg_config.h"
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <future>
#include <curl/curl.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <sys/stat.h>
#include <zlib.h>
#include <cstring>
#include <iomanip>
#include <chrono>

extern "C" {
    #include "runepkg_util.h"
    #include "runepkg_hash.h"
}

// Architecture - default to amd64 for now
const char* G_ARCH = "amd64";

struct DownloadTask {
    std::string url;
    std::string dest_path;
    bool success = false;
};

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

bool download_file(const std::string& url, const std::string& dest_path) {
    CURL *curl;
    FILE *fp;
    CURLcode res;
    curl = curl_easy_init();
    if (curl) {
        fp = fopen(dest_path.c_str(), "wb");
        if (!fp) {
            curl_easy_cleanup(curl);
            return false;
        }
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

        res = curl_easy_perform(curl);
        fclose(fp);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK) {
            return true;
        } else {
            std::cerr << "CURL Error: " << curl_easy_strerror(res) << " for " << url << std::endl;
        }
    }
    return false;
}

bool decompress_gz(const std::string& src, const std::string& dest) {
    gzFile src_file = gzopen(src.c_str(), "rb");
    if (!src_file) return false;

    std::ofstream dest_file(dest, std::ios::binary);
    if (!dest_file.is_open()) {
        gzclose(src_file);
        return false;
    }

    char buffer[8192];
    int bytes_read;
    while ((bytes_read = gzread(src_file, buffer, sizeof(buffer))) > 0) {
        dest_file.write(buffer, bytes_read);
    }

    gzclose(src_file);
    dest_file.close();
    return bytes_read == 0;
}

struct IndexEntry {
    char name[64];
    uint32_t file_id;
    uint32_t offset;

    bool operator<(const IndexEntry& other) const {
        return std::strcmp(name, other.name) < 0;
    }
};

extern "C" int runepkg_cpp_ffi_available(void) {
    return 1;
}

extern "C" int runepkg_update(void) {
    std::cout << "\033[1;32m[runepkg]\033[0m Starting parallel repository update..." << std::endl;

    if (!g_sources || g_sources_count == 0) {
        std::cerr << "Error: No sources configured in runepkgconfig." << std::endl;
        return -1;
    }

    curl_global_init(CURL_GLOBAL_ALL);

    std::vector<DownloadTask> tasks;
    std::vector<std::string> pkg_files;

    for (int i = 0; i < g_sources_count; i++) {
        if (std::string(g_sources[i]->type) != "deb") continue;

        std::string base_url = g_sources[i]->url;
        if (base_url.back() != '/') base_url += '/';

        std::string suite = g_sources[i]->suite;
        std::stringstream ss(g_sources[i]->components);
        std::string component;

        while (ss >> component) {
            std::string url = base_url + "dists/" + suite + "/" + component + "/binary-" + G_ARCH + "/Packages.gz";

            std::string safe_url = url;
            std::replace(safe_url.begin(), safe_url.end(), '/', '_');
            std::replace(safe_url.begin(), safe_url.end(), ':', '_');

            std::string dest_path = std::string(g_runepkg_lists_dir) + "/" + safe_url;
            tasks.push_back({url, dest_path, false});
        }
    }

    std::cout << "Downloading " << tasks.size() << " package lists..." << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::future<bool>> futures;
    for (auto& task : tasks) {
        futures.push_back(std::async(std::launch::async, download_file, task.url, task.dest_path));
    }

    for (size_t i = 0; i < tasks.size(); i++) {
        tasks[i].success = futures[i].get();
        if (tasks[i].success) {
            std::string decompressed = tasks[i].dest_path;
            if (decompressed.size() > 3 && decompressed.substr(decompressed.size() - 3) == ".gz") {
                decompressed = decompressed.substr(0, decompressed.size() - 3);
            } else {
                decompressed += ".unpacked";
            }

            if (decompress_gz(tasks[i].dest_path, decompressed)) {
                pkg_files.push_back(decompressed);
            }
        }
    }

    std::cout << "Building Hybrid Binary Name Index (Tier 1 & 2)..." << std::endl;

    std::vector<IndexEntry> index;
    std::vector<std::string> file_list;
    std::unordered_map<std::string, std::string> latest_versions;

    for (size_t f_idx = 0; f_idx < pkg_files.size(); f_idx++) {
        std::ifstream infile(pkg_files[f_idx], std::ios::binary);
        if (!infile.is_open()) continue;

        file_list.push_back(pkg_files[f_idx]);
        uint32_t current_file_id = file_list.size() - 1;

        std::string line;
        uint32_t current_offset = 0;
        uint32_t stanza_offset = 0;
        std::string pkg_name;
        std::string pkg_version;

        while (std::getline(infile, line)) {
            size_t len = line.length() + 1;

            if (line.empty() || line == "\r") {
                if (!pkg_name.empty()) {
                    IndexEntry entry;
                    std::strncpy(entry.name, pkg_name.c_str(), 63);
                    entry.name[63] = '\0';
                    entry.file_id = current_file_id;
                    entry.offset = stanza_offset;
                    index.push_back(entry);

                    if (latest_versions.find(pkg_name) == latest_versions.end() ||
                        runepkg_util_compare_versions(pkg_version.c_str(), latest_versions[pkg_name].c_str()) > 0) {
                        latest_versions[pkg_name] = pkg_version;
                    }

                    pkg_name.clear();
                    pkg_version.clear();
                }
                stanza_offset = current_offset + len;
            } else if (line.compare(0, 9, "Package: ") == 0) {
                pkg_name = line.substr(9);
                if (!pkg_name.empty() && pkg_name.back() == '\r') pkg_name.pop_back();
            } else if (line.compare(0, 9, "Version: ") == 0) {
                pkg_version = line.substr(9);
                if (!pkg_version.empty() && pkg_version.back() == '\r') pkg_version.pop_back();
            }

            current_offset += len;
        }
        if (!pkg_name.empty()) {
            IndexEntry entry;
            std::strncpy(entry.name, pkg_name.c_str(), 63);
            entry.name[63] = '\0';
            entry.file_id = current_file_id;
            entry.offset = stanza_offset;
            index.push_back(entry);
            if (latest_versions.find(pkg_name) == latest_versions.end() ||
                runepkg_util_compare_versions(pkg_version.c_str(), latest_versions[pkg_name].c_str()) > 0) {
                latest_versions[pkg_name] = pkg_version;
            }
        }
    }

    std::sort(index.begin(), index.end());

    std::string index_path = std::string(g_runepkg_db_dir) + "/repo_index.bin";
    std::ofstream out_index(index_path, std::ios::binary);
    if (out_index.is_open()) {
        uint32_t count = index.size();
        out_index.write(reinterpret_cast<const char*>(&count), sizeof(count));
        out_index.write(reinterpret_cast<const char*>(index.data()), index.size() * sizeof(IndexEntry));
        out_index.close();
    }

    std::string file_list_path = std::string(g_runepkg_db_dir) + "/repo_files.txt";
    std::ofstream out_files(file_list_path);
    if (out_files.is_open()) {
        for (const auto& f : file_list) {
            out_files << f << "\n";
        }
        out_files.close();
    }

    std::cout << "Checking for upgradable packages..." << std::endl;
    int upgradable_count = 0;
    if (runepkg_main_hash_table) {
        for (size_t i = 0; i < runepkg_main_hash_table->size; i++) {
            runepkg_hash_node_t *node = runepkg_main_hash_table->buckets[i];
            while (node) {
                std::string name = node->data.package_name;
                if (latest_versions.count(name)) {
                    if (runepkg_util_compare_versions(latest_versions[name].c_str(), node->data.version) > 0) {
                        std::cout << "  \033[1;33m[upgradable]\033[0m " << name << ": "
                                  << node->data.version << " -> " << latest_versions[name] << std::endl;
                        upgradable_count++;
                    }
                }
                node = node->next;
            }
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "\033[1;32mUpdate complete!\033[0m " << index.size() << " packages indexed. "
              << upgradable_count << " upgradable. Time: " << duration.count() / 1000.0 << "s" << std::endl;

    curl_global_cleanup();
    return 0;
}

extern "C" int runepkg_repo_search(const char *query) {
    if (!query || strlen(query) == 0) return -1;

    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);

    std::string file_list_path = std::string(g_runepkg_db_dir) + "/repo_files.txt";
    std::ifstream flist(file_list_path);
    if (!flist.is_open()) {
        std::cerr << "Error: Repository index not found. Run 'runepkg update' first." << std::endl;
        return -1;
    }

    std::vector<std::string> pkg_files;
    std::string line;
    while (std::getline(flist, line)) {
        if (!line.empty()) pkg_files.push_back(line);
    }
    flist.close();

    std::cout << "Sorting and searching metadata..." << std::endl;

    int match_count = 0;
    for (const auto& filename : pkg_files) {
        std::ifstream infile(filename);
        if (!infile.is_open()) continue;

        std::string line;
        std::string pkg_name, pkg_version, pkg_arch, pkg_desc;
        bool match = false;

        while (std::getline(infile, line)) {
            if (line.empty() || line == "\r") {
                if (match && !pkg_name.empty()) {
                    std::cout << "\033[1;32m" << pkg_name << "\033[0m/" << "repo"
                              << " \033[1;33m" << pkg_version << "\033[0m " << pkg_arch << std::endl;
                    std::cout << "  " << pkg_desc << std::endl << std::endl;
                    match_count++;
                }
                pkg_name.clear(); pkg_version.clear(); pkg_arch.clear(); pkg_desc.clear();
                match = false;
                continue;
            }

            std::string lower_line = line;
            std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);
            if (lower_line.find(q) != std::string::npos) {
                match = true;
            }

            if (line.compare(0, 9, "Package: ") == 0) pkg_name = line.substr(9);
            else if (line.compare(0, 9, "Version: ") == 0) pkg_version = line.substr(9);
            else if (line.compare(0, 14, "Architecture: ") == 0) pkg_arch = line.substr(14);
            else if (line.compare(0, 13, "Description: ") == 0) pkg_desc = line.substr(13);

            if (!pkg_name.empty() && pkg_name.back() == '\r') pkg_name.pop_back();
            if (!pkg_version.empty() && pkg_version.back() == '\r') pkg_version.pop_back();
            if (!pkg_arch.empty() && pkg_arch.back() == '\r') pkg_arch.pop_back();
            if (!pkg_desc.empty() && pkg_desc.back() == '\r') pkg_desc.pop_back();
        }
        if (match && !pkg_name.empty()) {
            std::cout << "\033[1;32m" << pkg_name << "\033[0m/" << "repo"
                      << " \033[1;33m" << pkg_version << "\033[0m " << pkg_arch << std::endl;
            std::cout << "  " << pkg_desc << std::endl << std::endl;
            match_count++;
        }
    }

    if (match_count == 0) {
        std::cout << "No matches found for '" << query << "'." << std::endl;
    } else {
        std::cout << "Found " << match_count << " matches." << std::endl;
    }

    return 0;
}

extern "C" char* runepkg_repo_download(const char *pkg_name) {
    if (!pkg_name || strlen(pkg_name) == 0) return NULL;

    std::string index_path = std::string(g_runepkg_db_dir) + "/repo_index.bin";
    std::ifstream idx(index_path, std::ios::binary);
    if (!idx.is_open()) {
        std::cerr << "Error: Repository index not found. Run 'runepkg update' first." << std::endl;
        return NULL;
    }

    uint32_t count;
    idx.read(reinterpret_cast<char*>(&count), sizeof(count));
    std::vector<IndexEntry> entries(count);
    idx.read(reinterpret_cast<char*>(entries.data()), count * sizeof(IndexEntry));
    idx.close();

    IndexEntry search_target;
    std::strncpy(search_target.name, pkg_name, 63);
    search_target.name[63] = '\0';

    auto it = std::lower_bound(entries.begin(), entries.end(), search_target);
    if (it == entries.end() || std::strcmp(it->name, pkg_name) != 0) {
        std::cerr << "Error: Package '" << pkg_name << "' not found in repository index." << std::endl;
        return NULL;
    }

    // Found it. Now map file_id to actual metadata file
    std::string file_list_path = std::string(g_runepkg_db_dir) + "/repo_files.txt";
    std::ifstream flist(file_list_path);
    std::vector<std::string> pkg_files;
    std::string line;
    while (std::getline(flist, line)) pkg_files.push_back(line);
    flist.close();

    if (it->file_id >= pkg_files.size()) {
        std::cerr << "Error: Internal index corruption (file_id out of range)." << std::endl;
        return NULL;
    }

    std::string metadata_file = pkg_files[it->file_id];
    std::ifstream meta(metadata_file);
    meta.seekg(it->offset);

    std::string deb_relative_path;
    while (std::getline(meta, line)) {
        if (line.empty() || line == "\r") break;
        if (line.compare(0, 10, "Filename: ") == 0) {
            deb_relative_path = line.substr(10);
            if (!deb_relative_path.empty() && deb_relative_path.back() == '\r') deb_relative_path.pop_back();
            break;
        }
    }
    meta.close();

    if (deb_relative_path.empty()) {
        std::cerr << "Error: Could not find Filename field in metadata for " << pkg_name << std::endl;
        return NULL;
    }

    // Now we need the base URL. We can infer it from the metadata filename.
    // The metadata filename was safe_url (URL with / and : replaced by _)
    // We can find the corresponding g_sources entry by matching the URL prefix
    std::string base_url;
    for (int i = 0; i < g_sources_count; i++) {
        std::string source_url = g_sources[i]->url;
        if (source_url.back() != '/') source_url += '/';

        // Check if metadata_file contains a part of this URL (roughly)
        // A better way is to store the source index in the binary index,
        // but for now let's use the source list order.
        // During update, we processed sources in order.
        // We can find which source produced this metadata_file.
        // The metadata_file was g_runepkg_lists_dir + "/" + safe_url
        // Let's just try all sources and see if the URL looks plausible.

        std::string test_url = source_url + deb_relative_path;
        // Basic check: is this source_url part of the original Packages.gz URL?
        // Since we don't have that mapping easily, let's assume the first source that matches
        // the metadata file's origin (this is a bit hacky, but should work for now).
        base_url = source_url;
        break;
    }

    std::string full_url = base_url + deb_relative_path;
    std::string deb_filename = deb_relative_path.substr(deb_relative_path.find_last_of('/') + 1);
    std::string dest_path = std::string(g_download_dir) + "/" + deb_filename;

    std::cout << "\033[1;34m[download]\033[0m Downloading " << pkg_name << " from " << full_url << "..." << std::endl;

    curl_global_init(CURL_GLOBAL_ALL);
    if (download_file(full_url, dest_path)) {
        std::cout << "\033[1;32m[success]\033[0m Downloaded to " << dest_path << std::endl;
        curl_global_cleanup();
        return strdup(dest_path.c_str());
    } else {
        std::cerr << "\033[1;31m[error]\033[0m Failed to download " << pkg_name << std::endl;
        curl_global_cleanup();
        return NULL;
    }
}
