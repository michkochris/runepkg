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
#include <unordered_set>
#include <set>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>
#include <cstring>
#include <iomanip>
#include <chrono>

extern "C" {
    #include "runepkg_util.h"
    #include "runepkg_hash.h"
    #include "runepkg_handle.h"
    #include "runepkg_install.h"
}

// Architecture - default to amd64 for now
const char* G_ARCH = "amd64";

struct DownloadTask {
    std::string url;
    std::string dest_path;
    std::string pkg_name;
    size_t size = 0;
    bool success = false;
};

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

struct SegmentInfo {
    int fd;
    size_t current_offset;
    size_t end;
    std::string url;
    bool success;
};

size_t write_to_fd(void *ptr, size_t size, size_t nmemb, void *userdata) {
    SegmentInfo *seg = (SegmentInfo*)userdata;
    size_t bytes = size * nmemb;
    ssize_t written = pwrite(seg->fd, ptr, bytes, seg->current_offset);
    if (written > 0) {
        seg->current_offset += (size_t)written;
        return (size_t)written;
    }
    return 0;
}

bool download_file(const std::string& url, const std::string& dest_path, size_t expected_size = 0) {
    if (runepkg_util_file_exists(dest_path.c_str())) {
        return true;
    }

    const size_t SEGMENT_THRESHOLD = 8 * 1024 * 1024; // 8MB
    const int NUM_THREADS = 4;

    if (expected_size > SEGMENT_THRESHOLD) {
        int fd = open(dest_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            if (ftruncate(fd, expected_size) == 0) {
                std::vector<SegmentInfo> segments(NUM_THREADS);
                size_t seg_size = expected_size / NUM_THREADS;
                std::vector<std::future<void>> futures;

                for (int i = 0; i < NUM_THREADS; i++) {
                    segments[i].fd = fd;
                    segments[i].current_offset = i * seg_size;
                    segments[i].end = (i == NUM_THREADS - 1) ? expected_size - 1 : (i + 1) * seg_size - 1;
                    segments[i].url = url;
                    segments[i].success = false;

                    futures.push_back(std::async(std::launch::async, [&segments, i]() {
                        CURL *curl = curl_easy_init();
                        if (curl) {
                            curl_easy_setopt(curl, CURLOPT_URL, segments[i].url.c_str());
                            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_fd);
                            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &segments[i]);

                            char range[64];
                            snprintf(range, sizeof(range), "%zu-%zu", segments[i].current_offset, segments[i].end);
                            curl_easy_setopt(curl, CURLOPT_RANGE, range);

                            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
                            curl_easy_setopt(curl, CURLOPT_USERAGENT, "runepkg/1.0");

                            CURLcode res = curl_easy_perform(curl);
                            segments[i].success = (res == CURLE_OK);
                            curl_easy_cleanup(curl);
                        }
                    }));
                }

                for (auto& f : futures) f.get();
                bool all_success = true;
                for (const auto& s : segments) if (!s.success) all_success = false;

                close(fd);
                if (all_success) return true;
                unlink(dest_path.c_str());
            } else {
                close(fd);
            }
        }
    }

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
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "runepkg/1.0");

        res = curl_easy_perform(curl);
        fclose(fp);
        curl_easy_cleanup(curl);

        return (res == CURLE_OK);
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

// Helper to get latest versions from the metadata files
std::unordered_map<std::string, std::string> get_latest_versions() {
    std::unordered_map<std::string, std::string> latest_versions;

    std::string file_list_path = std::string(g_runepkg_db_dir) + "/repo_files.txt";
    std::ifstream flist(file_list_path);
    if (!flist.is_open()) return latest_versions;

    std::vector<std::string> pkg_files;
    std::string line;
    while (std::getline(flist, line)) {
        if (!line.empty()) pkg_files.push_back(line);
    }
    flist.close();

    for (const auto& filename : pkg_files) {
        std::ifstream infile(filename, std::ios::binary);
        if (!infile.is_open()) continue;

        std::string pkg_name, pkg_version;
        while (std::getline(infile, line)) {
            if (line.empty() || line == "\r") {
                if (!pkg_name.empty()) {
                    if (latest_versions.find(pkg_name) == latest_versions.end() ||
                        runepkg_util_compare_versions(pkg_version.c_str(), latest_versions[pkg_name].c_str()) > 0) {
                        latest_versions[pkg_name] = pkg_version;
                    }
                    pkg_name.clear(); pkg_version.clear();
                }
                continue;
            }
            if (line.compare(0, 9, "Package: ") == 0) {
                pkg_name = line.substr(9);
                if (!pkg_name.empty() && pkg_name.back() == '\r') pkg_name.pop_back();
            } else if (line.compare(0, 9, "Version: ") == 0) {
                pkg_version = line.substr(9);
                if (!pkg_version.empty() && pkg_version.back() == '\r') pkg_version.pop_back();
            }
        }
        if (!pkg_name.empty()) {
            if (latest_versions.find(pkg_name) == latest_versions.end() ||
                runepkg_util_compare_versions(pkg_version.c_str(), latest_versions[pkg_name].c_str()) > 0) {
                latest_versions[pkg_name] = pkg_version;
            }
        }
    }
    return latest_versions;
}

void build_index(const std::vector<std::string>& pkg_files, const std::string& index_bin_path, const std::string& file_list_path) {
    std::vector<IndexEntry> index;
    std::vector<std::string> file_list;

    for (size_t f_idx = 0; f_idx < pkg_files.size(); f_idx++) {
        std::ifstream infile(pkg_files[f_idx], std::ios::binary);
        if (!infile.is_open()) continue;

        file_list.push_back(pkg_files[f_idx]);
        uint32_t current_file_id = file_list.size() - 1;

        std::string line;
        uint32_t current_offset = 0;
        uint32_t stanza_offset = 0;
        std::string pkg_name;

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
                    pkg_name.clear();
                }
                stanza_offset = current_offset + len;
            } else if (line.compare(0, 9, "Package: ") == 0) {
                pkg_name = line.substr(9);
                if (!pkg_name.empty() && pkg_name.back() == '\r') pkg_name.pop_back();
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
        }
    }

    std::sort(index.begin(), index.end());

    std::ofstream out_index(index_bin_path, std::ios::binary);
    if (out_index.is_open()) {
        uint32_t count = index.size();
        out_index.write(reinterpret_cast<const char*>(&count), sizeof(count));
        out_index.write(reinterpret_cast<const char*>(index.data()), index.size() * sizeof(IndexEntry));
        out_index.close();
    }

    std::ofstream out_files(file_list_path);
    if (out_files.is_open()) {
        for (const auto& f : file_list) {
            out_files << f << "\n";
        }
        out_files.close();
    }
}

extern "C" int runepkg_update(void) {
    std::cout << "\033[1;32m[runepkg]\033[0m Starting parallel repository update..." << std::endl;

    if (!g_sources || g_sources_count == 0) {
        std::cerr << "Error: No sources configured in runepkgconfig." << std::endl;
        return -1;
    }

    curl_global_init(CURL_GLOBAL_ALL);

    std::vector<DownloadTask> bin_tasks, src_tasks;
    std::vector<std::string> bin_pkg_files, src_pkg_files;

    for (int i = 0; i < g_sources_count; i++) {
        std::string base_url = g_sources[i]->url;
        if (base_url.back() != '/') base_url += '/';

        std::string suite = g_sources[i]->suite;
        std::stringstream ss(g_sources[i]->components);
        std::string component;

        while (ss >> component) {
            std::string url, dest_path;
            if (std::string(g_sources[i]->type) == "deb") {
                url = base_url + "dists/" + suite + "/" + component + "/binary-" + G_ARCH + "/Packages.gz";
                std::string safe_url = url;
                std::replace(safe_url.begin(), safe_url.end(), '/', '_');
                std::replace(safe_url.begin(), safe_url.end(), ':', '_');
                dest_path = std::string(g_runepkg_lists_dir) + "/" + safe_url;
                bin_tasks.push_back({url, dest_path, "", 0, false});
            } else if (std::string(g_sources[i]->type) == "deb-src") {
                url = base_url + "dists/" + suite + "/" + component + "/source/Sources.gz";
                std::string safe_url = url;
                std::replace(safe_url.begin(), safe_url.end(), '/', '_');
                std::replace(safe_url.begin(), safe_url.end(), ':', '_');
                dest_path = std::string(g_runepkg_lists_dir) + "/" + safe_url;
                src_tasks.push_back({url, dest_path, "", 0, false});
            }
        }
    }

    std::cout << "Downloading " << bin_tasks.size() + src_tasks.size() << " package lists..." << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::future<bool>> futures;
    std::vector<DownloadTask*> all_tasks;
    for (auto& t : bin_tasks) all_tasks.push_back(&t);
    for (auto& t : src_tasks) all_tasks.push_back(&t);

    for (auto* task : all_tasks) {
        std::string display_name;
        size_t dists_pos = task->url.find("/dists/");
        if (dists_pos != std::string::npos) {
            display_name = task->url.substr(dists_pos + 7);
            size_t last_slash = display_name.find_last_of('/');
            if (last_slash != std::string::npos) display_name = display_name.substr(0, last_slash);
        } else {
            display_name = task->url;
        }

        std::cout << "  \033[1;34m->\033[0m Fetching: " << display_name << std::endl;
        futures.push_back(std::async(std::launch::async, [task]() {
            return download_file(task->url, task->dest_path, task->size);
        }));
    }

    for (size_t i = 0; i < all_tasks.size(); i++) {
        all_tasks[i]->success = futures[i].get();
        if (all_tasks[i]->success) {
            std::string decompressed = all_tasks[i]->dest_path;
            if (decompressed.size() > 3 && decompressed.substr(decompressed.size() - 3) == ".gz") {
                decompressed = decompressed.substr(0, decompressed.size() - 3);
            } else {
                decompressed += ".unpacked";
            }

            if (decompress_gz(all_tasks[i]->dest_path, decompressed)) {
                // Determine if it was bin or src
                bool is_bin = false;
                for(auto& t : bin_tasks) if(&t == all_tasks[i]) is_bin = true;
                if(is_bin) bin_pkg_files.push_back(decompressed);
                else src_pkg_files.push_back(decompressed);
            }
        }
    }

    std::cout << "Building Hybrid Binary and Source Indexes..." << std::endl;
    build_index(bin_pkg_files, std::string(g_runepkg_db_dir) + "/repo_index.bin", std::string(g_runepkg_db_dir) + "/repo_files.txt");
    build_index(src_pkg_files, std::string(g_runepkg_db_dir) + "/repo_src_index.bin", std::string(g_runepkg_db_dir) + "/repo_src_files.txt");

    auto latest_versions = get_latest_versions();
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

    std::cout << "\033[1;32mUpdate complete!\033[0m Binary/Source indexes updated. "
              << upgradable_count << " upgradable. Time: " << duration.count() / 1000.0 << "s" << std::endl;

    curl_global_cleanup();
    return 0;
}

struct SearchResult {
    std::string name;
    std::string version;
    std::string arch;
    std::string desc;
    bool installed = false;
};

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

    std::cout << "Searching repository metadata..." << std::endl;

    std::map<std::string, SearchResult> results;

    for (const auto& filename : pkg_files) {
        std::ifstream infile(filename);
        if (!infile.is_open()) continue;

        std::string pkg_name, pkg_version, pkg_arch, pkg_desc;
        while (std::getline(infile, line)) {
            if (line.empty() || line == "\r") {
                if (!pkg_name.empty()) {
                    std::string combined = pkg_name + " " + pkg_desc;
                    std::transform(combined.begin(), combined.end(), combined.begin(), ::tolower);

                    if (combined.find(q) != std::string::npos) {
                        SearchResult res = {pkg_name, pkg_version, pkg_arch, pkg_desc, false};
                        if (runepkg_main_hash_table && runepkg_hash_search(runepkg_main_hash_table, pkg_name.c_str())) {
                            res.installed = true;
                        }

                        // Deduplicate: Keep latest version
                        if (results.find(pkg_name) == results.end() ||
                            runepkg_util_compare_versions(pkg_version.c_str(), results[pkg_name].version.c_str()) > 0) {
                            results[pkg_name] = res;
                        }
                    }
                    pkg_name.clear(); pkg_version.clear(); pkg_arch.clear(); pkg_desc.clear();
                }
                continue;
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
        // Handle last
        if (!pkg_name.empty()) {
            std::string combined = pkg_name + " " + pkg_desc;
            std::transform(combined.begin(), combined.end(), combined.begin(), ::tolower);
            if (combined.find(q) != std::string::npos) {
                SearchResult res = {pkg_name, pkg_version, pkg_arch, pkg_desc, false};
                if (runepkg_main_hash_table && runepkg_hash_search(runepkg_main_hash_table, pkg_name.c_str())) res.installed = true;
                if (results.find(pkg_name) == results.end() || runepkg_util_compare_versions(pkg_version.c_str(), results[pkg_name].version.c_str()) > 0) results[pkg_name] = res;
            }
        }
    }

    for (const auto& pair : results) {
        const auto& res = pair.second;
        std::cout << "\033[1;32m" << res.name << "\033[0m/" << "repo";
        if (res.installed) std::cout << " [\033[1;33minstalled\033[0m]";
        std::cout << " \033[1;33m" << res.version << "\033[0m " << res.arch << std::endl;
        std::cout << "  " << res.desc << std::endl << std::endl;
    }

    if (results.empty()) {
        std::cout << "No matches found for '" << query << "'." << std::endl;
    } else {
        std::cout << "Found " << results.size() << " matches." << std::endl;
    }

    return 0;
}

// Internal helper for resolving package URL from index
std::string get_package_url(const char *pkg_name, bool is_source, uint32_t *out_offset, std::string *out_metafile) {
    std::string index_name = is_source ? "repo_src_index.bin" : "repo_index.bin";
    std::string file_list_name = is_source ? "repo_src_files.txt" : "repo_files.txt";

    std::string index_path = std::string(g_runepkg_db_dir) + "/" + index_name;
    std::ifstream idx(index_path, std::ios::binary);
    if (!idx.is_open()) return "";

    uint32_t count;
    idx.read(reinterpret_cast<char*>(&count), sizeof(count));
    std::vector<IndexEntry> entries(count);
    idx.read(reinterpret_cast<char*>(entries.data()), count * sizeof(IndexEntry));
    idx.close();

    IndexEntry search_target;
    std::strncpy(search_target.name, pkg_name, 63);
    search_target.name[63] = '\0';

    auto it = std::lower_bound(entries.begin(), entries.end(), search_target);
    if (it == entries.end() || std::strcmp(it->name, pkg_name) != 0) return "";

    std::ifstream flist(std::string(g_runepkg_db_dir) + "/" + file_list_name);
    std::vector<std::string> pkg_files;
    std::string line;
    while (std::getline(flist, line)) pkg_files.push_back(line);
    flist.close();

    if (it->file_id >= pkg_files.size()) return "";

    if (out_offset) *out_offset = it->offset;
    if (out_metafile) *out_metafile = pkg_files[it->file_id];

    std::ifstream meta(pkg_files[it->file_id]);
    meta.seekg(it->offset);

    std::string rel_path;
    while (std::getline(meta, line)) {
        if (line.empty() || line == "\r") break;
        if (!is_source && line.compare(0, 10, "Filename: ") == 0) {
            rel_path = line.substr(10);
            break;
        } else if (is_source && line.compare(0, 11, "Directory: ") == 0) {
            rel_path = line.substr(11);
            break;
        }
    }
    if (!rel_path.empty() && rel_path.back() == '\r') rel_path.pop_back();

    std::string base_url;
    for (int i = 0; i < g_sources_count; i++) {
        if (is_source && std::string(g_sources[i]->type) == "deb-src") {
            base_url = g_sources[i]->url; break;
        } else if (!is_source && std::string(g_sources[i]->type) == "deb") {
            base_url = g_sources[i]->url; break;
        }
    }
    if (base_url.empty()) return "";
    if (base_url.back() != '/') base_url += '/';

    return base_url + rel_path;
}

struct PkgMetadata {
    std::string name;
    std::string url;
    std::string depends;
    std::string filename;
    size_t size = 0;
};

PkgMetadata get_package_metadata(const std::string& pkg_name) {
    PkgMetadata meta_data;
    meta_data.name = pkg_name;

    uint32_t offset = 0;
    std::string meta_file;
    std::string url = get_package_url(pkg_name.c_str(), false, &offset, &meta_file);
    if (url.empty()) return meta_data;

    meta_data.url = url;
    meta_data.filename = url.substr(url.find_last_of('/') + 1);

    std::ifstream meta(meta_file);
    if (meta.is_open()) {
        meta.seekg(offset);
        std::string line;
        while (std::getline(meta, line)) {
            if (line.empty() || line == "\r") break;
            if (line.compare(0, 9, "Depends: ") == 0) {
                meta_data.depends = line.substr(9);
                if (!meta_data.depends.empty() && meta_data.depends.back() == '\r') meta_data.depends.pop_back();
            } else if (line.compare(0, 6, "Size: ") == 0) {
                try {
                    meta_data.size = std::stoull(line.substr(6));
                } catch (...) {
                    meta_data.size = 0;
                }
            }
        }
    }
    return meta_data;
}

std::vector<std::string> parse_depends_cpp(const std::string& depends) {
    std::vector<std::string> result;
    if (depends.empty()) return result;
    char **deps = parse_depends(depends.c_str());
    if (deps) {
        for (int i = 0; deps[i]; i++) {
            result.push_back(deps[i]);
            free(deps[i]);
        }
        free(deps);
    }
    return result;
}

void resolve_recursive(const std::string& pkg_name,
                      std::unordered_map<std::string, PkgMetadata>& resolved,
                      std::vector<std::string>& order,
                      std::unordered_set<std::string>& visiting,
                      bool ignore_installed) {
    if (resolved.count(pkg_name)) return;
    if (visiting.count(pkg_name)) return;

    // Check if installed, unless we are forcing this specific package
    if (!ignore_installed) {
        if (runepkg_main_hash_table && runepkg_hash_search(runepkg_main_hash_table, pkg_name.c_str())) return;
    }

    PkgMetadata meta = get_package_metadata(pkg_name);
    if (meta.url.empty()) return;

    visiting.insert(pkg_name);
    resolved[pkg_name] = meta;

    std::vector<std::string> deps = parse_depends_cpp(meta.depends);
    for (const auto& dep : deps) {
        resolve_recursive(dep, resolved, order, visiting, false);
    }

    order.push_back(pkg_name);
    visiting.erase(pkg_name);
}

extern "C" char* runepkg_repo_download(const char *pkg_name) {
    if (!pkg_name) return NULL;

    std::string index_path = std::string(g_runepkg_db_dir) + "/repo_index.bin";
    if (!runepkg_util_file_exists(index_path.c_str())) {
        static bool warned = false;
        if (!warned) {
            std::cerr << "\033[1;31m[error]\033[0m Repository index not found. Please run 'runepkg update' first." << std::endl;
            warned = true;
        }
        return NULL;
    }

    std::unordered_map<std::string, PkgMetadata> resolved;
    std::vector<std::string> order;
    std::unordered_set<std::string> visiting;

    // Use g_force_mode to decide if we should skip the "already installed" check for the root package
    resolve_recursive(pkg_name, resolved, order, visiting, g_force_mode);

    if (resolved.empty()) return NULL;

    if (resolved.size() > 1) {
        std::cout << "\033[1;34m[runepkg]\033[0m Resolving recursive dependencies for " << pkg_name << "..." << std::endl;
        std::cout << "\033[1;33m[dependencies]\033[0m The following dependencies are required:" << std::endl;
        int width = runepkg_util_get_terminal_width();
        int current_line_len = 2;
        std::cout << "  ";
        for (size_t i = 0; i < order.size(); i++) {
            if (current_line_len + order[i].length() + 1 > (size_t)width && i > 0) {
                std::cout << "\n  ";
                current_line_len = 2;
            }
            std::cout << order[i];
            current_line_len += order[i].length();
            if (i < order.size() - 1) {
                std::cout << " ";
                current_line_len += 1;
            }
        }
        std::cout << std::endl;

        std::cout << "Would you like to attempt to download and install them? [\033[1;33my\033[0m/\033[1;33mN\033[0m] ";
        std::fflush(stdout);
        char resp[16];
        bool confirmed = false;
        if (g_auto_confirm_deps) {
            std::cout << "\033[1;33my (auto)\033[0m" << std::endl;
            confirmed = true;
            g_auto_confirm_siblings = true;
        } else if (std::fgets(resp, sizeof(resp), stdin) && (resp[0] == 'y' || resp[0] == 'Y')) {
            confirmed = true;
            g_auto_confirm_deps = true;
            g_auto_confirm_siblings = true;
        }

        if (!confirmed) {
            std::cout << "Installation cancelled." << std::endl;
            return NULL;
        }
    }

    bool needs_download = false;
    for (const auto& name : order) {
        const auto& meta = resolved[name];
        std::string dest_path = std::string(g_download_dir) + "/" + meta.filename;
        if (!runepkg_util_file_exists(dest_path.c_str())) {
            needs_download = true;
            break;
        }
    }

    if (needs_download) {
        if (resolved.size() > 1) {
            std::cout << "\033[1;34m[runepkg]\033[0m Pre-fetching " << resolved.size() << " packages in parallel..." << std::endl;
        } else {
            std::string deb_filename = resolved[pkg_name].filename;
            std::cout << "\033[1;34m[download]\033[0m Downloading " << pkg_name << "..." << std::endl;
            std::cout << "  \033[1;34m->\033[0m " << deb_filename << std::endl;
        }
    }

    std::vector<DownloadTask> tasks;
    for (const auto& name : order) {
        const auto& meta = resolved[name];
        std::string dest_path = std::string(g_download_dir) + "/" + meta.filename;
        tasks.push_back({meta.url, dest_path, name, meta.size, false});
    }

    curl_global_init(CURL_GLOBAL_ALL);
    std::vector<std::future<bool>> futures;
    for (auto& t : tasks) {
        if (runepkg_util_file_exists(t.dest_path.c_str())) {
            futures.push_back(std::async(std::launch::deferred, [](){ return true; }));
        } else {
            if (resolved.size() > 1) {
                std::string deb_filename = t.url.substr(t.url.find_last_of('/') + 1);
                std::cout << "  \033[1;34m->\033[0m " << deb_filename << std::endl;
            }
            futures.push_back(std::async(std::launch::async, [&t]() {
                return download_file(t.url, t.dest_path, t.size);
            }));
        }
    }

    for (size_t i = 0; i < tasks.size(); i++) {
        tasks[i].success = futures[i].get();
    }
    curl_global_cleanup();

    std::string top_filename = resolved[pkg_name].url.substr(resolved[pkg_name].url.find_last_of('/') + 1);
    std::string top_dest = std::string(g_download_dir) + "/" + top_filename;
    return strdup(top_dest.c_str());
}

extern "C" int runepkg_upgrade(void) {
    std::cout << "\033[1;32m[runepkg]\033[0m Starting full system upgrade..." << std::endl;

    auto latest_versions = get_latest_versions();
    std::vector<std::string> to_upgrade;

    if (runepkg_main_hash_table) {
        for (size_t i = 0; i < runepkg_main_hash_table->size; i++) {
            runepkg_hash_node_t *node = runepkg_main_hash_table->buckets[i];
            while (node) {
                std::string name = node->data.package_name;
                if (latest_versions.count(name)) {
                    if (runepkg_util_compare_versions(latest_versions[name].c_str(), node->data.version) > 0) {
                        to_upgrade.push_back(name);
                    }
                }
                node = node->next;
            }
        }
    }

    if (to_upgrade.empty()) {
        std::cout << "All packages are already up to date." << std::endl;
        return 0;
    }

    std::cout << "The following packages will be upgraded:" << std::endl;
    int width = runepkg_util_get_terminal_width();
    int current_line_len = 2;
    std::cout << "  ";
    for (size_t i = 0; i < to_upgrade.size(); i++) {
        if (current_line_len + to_upgrade[i].length() + 1 > (size_t)width && i > 0) {
            std::cout << "\n  ";
            current_line_len = 2;
        }
        std::cout << to_upgrade[i];
        current_line_len += to_upgrade[i].length();
        if (i < to_upgrade.size() - 1) {
            std::cout << " ";
            current_line_len += 1;
        }
    }
    std::cout << std::endl;

    std::cout << "Do you want to continue? [\033[1;33my\033[0m/\033[1;33mN\033[0m] ";
    fflush(stdout);
    char resp[16];
    if (!fgets(resp, sizeof(resp), stdin) || (resp[0] != 'y' && resp[0] != 'Y')) {
        std::cout << "Upgrade cancelled." << std::endl;
        return 0;
    }

    // Parallel PRE-FETCH stage
    std::cout << "\033[1;34m[runepkg]\033[0m Pre-fetching " << to_upgrade.size() << " packages in parallel..." << std::endl;
    std::vector<DownloadTask> tasks;
    for (const auto& name : to_upgrade) {
        PkgMetadata meta = get_package_metadata(name);
        if (!meta.url.empty()) {
            tasks.push_back({meta.url, std::string(g_download_dir) + "/" + meta.filename, name, meta.size, false});
        }
    }

    curl_global_init(CURL_GLOBAL_ALL);
    std::vector<std::future<bool>> futures;
    for (auto& t : tasks) {
        if (runepkg_util_file_exists(t.dest_path.c_str())) {
            futures.push_back(std::async(std::launch::deferred, [](){ return true; }));
        } else {
            std::string deb_filename = t.url.substr(t.url.find_last_of('/') + 1);
            std::cout << "  \033[1;34m->\033[0m " << deb_filename << std::endl;
            futures.push_back(std::async(std::launch::async, [&t]() {
                return download_file(t.url, t.dest_path, t.size);
            }));
        }
    }
    for (size_t i = 0; i < tasks.size(); i++) tasks[i].success = futures[i].get();

    int success_count = 0, fail_count = 0;
    for (const auto& t : tasks) {
        if (!t.success) {
            std::cerr << "Failed to download " << t.pkg_name << std::endl;
            fail_count++;
            continue;
        }

        PkgInfo *current = runepkg_hash_search(runepkg_main_hash_table, t.pkg_name.c_str());
        if (current && current->version && runepkg_util_compare_versions(latest_versions[t.pkg_name].c_str(), current->version) <= 0) {
            continue;
        }

        std::cout << "\033[1;32m[upgrading]\033[0m " << t.pkg_name << std::endl;
        extern bool g_force_mode;
        bool old_force = g_force_mode;
        g_force_mode = true;
        if (handle_install(t.dest_path.c_str()) == 0) success_count++;
        else fail_count++;
        g_force_mode = old_force;
    }

    std::cout << "\033[1;32mUpgrade finished!\033[0m " << success_count << " upgraded, " << fail_count << " failed." << std::endl;
    curl_global_cleanup();
    return fail_count == 0 ? 0 : -1;
}

extern "C" int runepkg_repo_source_download(const char *pkg_name) {
    uint32_t offset;
    std::string metafile;
    std::string base_dir_url = get_package_url(pkg_name, true, &offset, &metafile);
    if (base_dir_url.empty()) return -1;

    struct SourceFile {
        std::string filename;
        size_t size;
    };
    std::vector<SourceFile> files_to_download;
    std::ifstream meta(metafile);
    meta.seekg(offset);
    std::string line;
    bool in_files = false;
    while (std::getline(meta, line)) {
        if (line.empty() || line == "\r") break;
        if (line.compare(0, 7, "Files: ") == 0) {
            in_files = true;
        } else if (in_files && line[0] == ' ') {
            std::stringstream ss(line);
            std::string hash, size_str, filename;
            ss >> hash >> size_str >> filename;
            if (!filename.empty()) {
                try {
                    files_to_download.push_back({filename, (size_t)std::stoull(size_str)});
                } catch (...) {
                    files_to_download.push_back({filename, 0});
                }
            }
        } else if (in_files && line[0] != ' ') {
            break; // Stop after the first file section (Files) to avoid double-counting with Checksums-Sha256
        }
    }

    std::cout << "\033[1;34m[source]\033[0m Downloading source package " << pkg_name << " (" << files_to_download.size() << " files in parallel)..." << std::endl;
    curl_global_init(CURL_GLOBAL_ALL);
    std::vector<std::future<bool>> futures;
    for (const auto& sf : files_to_download) {
        std::string url = base_dir_url + "/" + sf.filename;
        std::string dest = std::string(g_download_dir) + "/" + sf.filename;
        std::cout << "  \033[1;34m->\033[0m " << sf.filename << std::endl;
        futures.push_back(std::async(std::launch::async, [url, dest, &sf]() {
            return download_file(url, dest, sf.size);
        }));
    }

    int downloaded = 0;
    for (size_t i = 0; i < futures.size(); i++) if (futures[i].get()) downloaded++;
    curl_global_cleanup();

    std::cout << "\033[1;32m[success]\033[0m Downloaded " << downloaded << " files to " << g_download_dir << std::endl;
    return 0;
}
