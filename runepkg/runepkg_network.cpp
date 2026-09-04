/*****************************************************************************
 * Filename:    runepkg_network.cpp
 * Author:      <michkochris@gmail.com>
 * Date:        2026-08-26
 * Description: Parallel network synchronization & fetch engine (C++ FFI)
 * LICENSE:     GPL v3
 ******************************************************************************/

#include "runepkg_cpp_ffi.h"
#include "runepkg_security.hpp"
#include "runepkg_util_cpp.hpp"
#include "runepkg_guard.hpp"
#include "runepkg_config.h"
#include <iostream>
#include <vector>
#include <string>
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
#include <unistd.h>
#include <fcntl.h>
#include <zlib.h>
#include <cstring>
#include <iomanip>
#include <chrono>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <functional>
#include <type_traits>

extern "C" {
    #include "runepkg_util.h"
    #include "runepkg_hash.h"
    #include "runepkg_handle.h"
    #include "runepkg_install.h"
    #include "runepkg_storage.h"
    #include "runepkg_crypto.h"
    #include "runepkg_completion.h"
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
#define G_ARCH get_effective_arch()

// Parallel Execution Engine
class ParallelExecutor {
public:
    ParallelExecutor(size_t max_threads = 0) : stop(false) {
        if (max_threads == 0) {
            max_threads = std::thread::hardware_concurrency();
            if (max_threads == 0) max_threads = 8;
        }
        for (size_t i = 0; i < max_threads; ++i)
            workers.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
    }

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using return_type = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop) throw std::runtime_error("enqueue on stopped ParallelExecutor");
            tasks.emplace([task] { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    ~ParallelExecutor() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers) worker.join();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// Global state for parallel progress tracking
std::atomic<int> g_finished_count{0};
std::mutex g_progress_mutex;
std::map<std::string, double> g_active_downloads;
std::unordered_set<std::string> g_completed_names;
int g_total_to_download = 0;

// Elder Futhark runes for thematic progress bar
const char* ELDER_FUTHARK[] = {
    "ᚠ", "ᚢ", "ᚦ", "ᚨ", "ᚱ", "ᚲ", "ᚷ", "ᚹ",
    "ᚺ", "ᚾ", "ᛁ", "ᛃ", "ᛇ", "ᛈ", "ᛉ", "ᛊ",
    "ᛏ", "ᛒ", "ᛖ", "ᛗ", "ᛚ", "ᛜ", "ᛞ", "ᛟ"
};

void render_runic_bar(int width, double fraction) {
    int pos = (int)(width * fraction);
    std::cout << "[";
    for (int i = 0; i < width; ++i) {
        if (i < pos) {
            std::cout << ELDER_FUTHARK[i % 24];
        } else {
            std::cout << "·";
        }
    }
    std::cout << "]";
}

void print_multi_progress() {
    if (g_active_downloads.empty()) {
        if (g_finished_count >= g_total_to_download && g_total_to_download > 0) {
        }
    }
}

void update_progress(const std::string& name, double fraction) {
    if (fraction >= 1.0) {
        std::string display_name = name;
        if (display_name.length() > 25) display_name = display_name.substr(0, 22) + "...";

        {
            std::lock_guard<std::mutex> lock(g_progress_mutex);
            if (g_completed_names.find(name) == g_completed_names.end()) {
                g_completed_names.insert(name);
                g_active_downloads.erase(name);

                g_finished_count++;
                std::cout << "\r\033[K  \033[1;34m->\033[0m " << std::left << std::setw(25) << display_name << " ";
                render_runic_bar(20, 1.0);
                std::cout << " 100.0%" << std::endl;
            }
        }
    } else {
        static std::map<std::string, int> last_percent;
        int current_percent = (int)(fraction * 100);

        std::lock_guard<std::mutex> lock(g_progress_mutex);
        if (last_percent[name] != current_percent) {
            std::string display_name = name;
            if (display_name.length() > 25) display_name = display_name.substr(0, 22) + "...";

            g_active_downloads[name] = fraction;
            last_percent[name] = current_percent;

            std::cout << "\r\033[K  \033[1;34m->\033[0m " << std::left << std::setw(25) << display_name << " ";
            render_runic_bar(20, fraction);
            std::cout << " " << std::fixed << std::setprecision(1) << std::right << std::setw(5) << (fraction * 100.0) << "%" << std::flush;
        }
    }
    print_multi_progress();
}

struct DownloadTask {
    std::string url;
    std::string dest_path;
    std::string pkg_name;
    size_t size = 0;
    bool success = false;
};

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}

int curl_progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    std::string *name = (std::string*)clientp;
    if (dltotal > 0) {
        update_progress(*name, (double)dlnow / dltotal);
    } else if (ultotal > 0) {
        update_progress(*name + " [UP]", (double)ulnow / ultotal);
    }
    return 0;
}

bool download_file(const std::string& url, const std::string& dest_path, size_t expected_size = 0, std::string pkg_name = "", bool force_refresh = false, std::string expected_sha256 = "") {
    if (!force_refresh && runepkg_util_file_exists(dest_path.c_str())) {
        if (!expected_sha256.empty()) {
            if (!runepkg::security::verify_sha256_checksum(dest_path, expected_sha256)) {
                unlink(dest_path.c_str());
            } else {
                {
                    std::lock_guard<std::mutex> lock(g_progress_mutex);
                    if (g_completed_names.find(pkg_name) == g_completed_names.end()) {
                        g_completed_names.insert(pkg_name);
                        g_finished_count++;
                        print_multi_progress();
                    }
                }
                return true;
            }
        } else {
            {
                std::lock_guard<std::mutex> lock(g_progress_mutex);
                if (g_completed_names.find(pkg_name) == g_completed_names.end()) {
                    g_completed_names.insert(pkg_name);
                    g_finished_count++;
                    print_multi_progress();
                }
            }
            return true;
        }
    }

    if (pkg_name.empty()) {
        pkg_name = url.substr(url.find_last_of('/') + 1);
    }

    update_progress(pkg_name, 0.0);

    const int MAX_RETRIES = 3;
    for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
        CURL *curl = curl_easy_init();
        if (!curl) return false;

        FILE *fp = fopen(dest_path.c_str(), "wb");
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
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pkg_name);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            fflush(fp);
            fsync(fileno(fp));
        }
        fclose(fp);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK) {
            if (expected_size > 0) {
                struct stat st;
                if (stat(dest_path.c_str(), &st) == 0 && (size_t)st.st_size != expected_size) {
                    if (attempt < MAX_RETRIES) {
                        unlink(dest_path.c_str());
                        usleep(500000 * attempt);
                        continue;
                    }
                    unlink(dest_path.c_str());
                    return false;
                }
            }
            if (!expected_sha256.empty()) {
                if (!runepkg::security::verify_sha256_checksum(dest_path, expected_sha256)) {
                    unlink(dest_path.c_str());
                    if (attempt < MAX_RETRIES) {
                        usleep(500000 * attempt);
                        continue;
                    }
                    return false;
                }
            }
            update_progress(pkg_name, 1.0);
            return true;
        }

        if (attempt < MAX_RETRIES) {
            unlink(dest_path.c_str());
            usleep(500000 * attempt);
            continue;
        }
    }

    unlink(dest_path.c_str());
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

    if (bytes_read == 0) {
        int fd = open(dest.c_str(), O_WRONLY);
        if (fd != -1) {
            fsync(fd);
            close(fd);
        }
        return true;
    }
    return false;
}

struct IndexEntry {
    char name[64];
    uint32_t file_id;
    uint32_t offset;

    bool operator<(const IndexEntry& other) const {
        return std::strcmp(name, other.name) < 0;
    }
};

struct PkgMetadata {
    std::string name;
    std::string version;
    std::string url;
    std::string depends;
    std::string pre_depends;
    std::string provides;
    std::string filename;
    std::string source_name;
    std::string description;
    std::string maintainer;
    std::string section;
    std::string priority;
    std::string homepage;
    std::string architecture;
    size_t size = 0;
};

// Global caches
struct RepoIndex {
    std::vector<IndexEntry> entries;
    std::vector<std::string> file_list;
    bool loaded = false;
};

static RepoIndex g_pkg_index;
static RepoIndex g_src_index;

struct RepoMapping {
    std::unordered_map<std::string, std::string> mapping;
    bool loaded = false;
};
static RepoMapping g_repo_mapping;

static std::unordered_map<std::string, PkgMetadata> g_metadata_cache;
static std::mutex g_metadata_cache_mutex;

static void ensure_index_loaded(bool is_source) {
    RepoIndex &idx_cache = is_source ? g_src_index : g_pkg_index;
    if (idx_cache.loaded) return;

    std::string index_name = is_source ? "repo_src_index.bin" : "repo_index.bin";
    std::string file_list_name = is_source ? "repo_src_files.txt" : "repo_files.txt";
    std::string index_path = std::string(g_runepkg_db_dir ? g_runepkg_db_dir : "/var/lib/runepkg_dir/runepkg_db") + "/" + index_name;
    std::string file_list_path = std::string(g_runepkg_db_dir ? g_runepkg_db_dir : "/var/lib/runepkg_dir/runepkg_db") + "/" + file_list_name;

    std::ifstream idx(index_path, std::ios::binary);
    if (idx.is_open()) {
        uint32_t count = 0;
        if (idx.read(reinterpret_cast<char*>(&count), sizeof(count))) {
            idx_cache.entries.resize(count);
            idx.read(reinterpret_cast<char*>(idx_cache.entries.data()), count * sizeof(IndexEntry));
        }
        idx.close();
    }

    std::ifstream flist(file_list_path);
    if (flist.is_open()) {
        std::string line;
        while (std::getline(flist, line)) {
            if (!line.empty()) {
                if (line.back() == '\r') line.pop_back();
                idx_cache.file_list.push_back(line);
            }
        }
        flist.close();
    }
    idx_cache.loaded = true;
}

static inline void ensure_repo_mapping_loaded() {
    if (g_repo_mapping.loaded) return;
    std::string mapping_path = std::string(g_runepkg_db_dir ? g_runepkg_db_dir : "/var/lib/runepkg_dir/runepkg_db") + "/repo_url_mapping.txt";
    std::ifstream mapping(mapping_path);
    if (mapping.is_open()) {
        std::string m_type, m_url, m_file;
        while (mapping >> m_type >> m_url >> m_file) {
            g_repo_mapping.mapping[m_file] = m_url;
        }
        mapping.close();
    }
    g_repo_mapping.loaded = true;
}

static std::string normalize_url(const std::string& url) {
    std::string safe_url = url;
    for (char &c : safe_url) {
        if (c == '/' || c == ':') c = '_';
    }
    return safe_url;
}

struct SourceFile {
    std::string filename;
    size_t size;
};

struct SourceMetadata {
    std::string name;
    std::string base_url;
    std::string build_depends;
    std::vector<SourceFile> files;
};

extern "C" int runepkg_cpp_ffi_available(void) {
    try {
        return 1;
    } catch (...) {
        return 0;
    }
}

std::unordered_map<std::string, std::string> get_latest_versions() {
    std::unordered_map<std::string, std::string> latest_versions;
    std::string file_list_path = std::string(g_runepkg_db_dir ? g_runepkg_db_dir : "/var/lib/runepkg_dir/runepkg_db") + "/repo_files.txt";
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
                    pkg_name.clear();
                    pkg_version.clear();
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
        uint32_t current_offset = 0, stanza_offset = 0;
        std::string pkg_name, provides_list;
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
                    if (!provides_list.empty()) {
                        std::stringstream ss(provides_list);
                        std::string virt_pkg;
                        while (std::getline(ss, virt_pkg, ',')) {
                            virt_pkg.erase(0, virt_pkg.find_first_not_of(" \t"));
                            virt_pkg.erase(virt_pkg.find_last_not_of(" \t") + 1);

                            size_t ver_start = virt_pkg.find_first_of(" (");
                            if (ver_start != std::string::npos) {
                                virt_pkg = virt_pkg.substr(0, ver_start);
                            }

                            if (!virt_pkg.empty()) {
                                IndexEntry v_entry;
                                std::strncpy(v_entry.name, virt_pkg.c_str(), 63);
                                v_entry.name[63] = '\0';
                                v_entry.file_id = current_file_id;
                                v_entry.offset = stanza_offset;
                                index.push_back(v_entry);
                            }
                        }
                    }
                    pkg_name.clear(); provides_list.clear();
                }
                stanza_offset = current_offset + len;
            } else if (line.compare(0, 9, "Package: ") == 0) {
                pkg_name = line.substr(9); if (!pkg_name.empty() && pkg_name.back() == '\r') pkg_name.pop_back();
            } else if (line.compare(0, 10, "Provides: ") == 0) {
                provides_list = line.substr(10); if (!provides_list.empty() && provides_list.back() == '\r') provides_list.pop_back();
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
        int fd = open(index_bin_path.c_str(), O_WRONLY);
        if (fd != -1) { fsync(fd); close(fd); }
    }
    std::ofstream out_files(file_list_path);
    if (out_files.is_open()) {
        for (const auto& f : file_list) out_files << f << "\n";
        out_files.close();
        int fd = open(file_list_path.c_str(), O_WRONLY);
        if (fd != -1) { fsync(fd); close(fd); }
    }
}

extern "C" int runepkg_update(void) {
    TransactionContext tx_ctx;
    runepkg::RunepkgTransactionGuard guard(&tx_ctx, "update", "1.0");
    try {
        std::cout << "\033[1;32m[runepkg]\033[0m Starting parallel repository update..." << std::endl;
        runepkg::util::log_info("Starting parallel repository update");
        auto start_time = std::chrono::high_resolution_clock::now();
        if (!g_sources || g_sources_count == 0) { std::cerr << "Error: No sources configured in runepkgconfig." << std::endl; return -1; }
        curl_global_init(CURL_GLOBAL_ALL);

        g_pkg_index.loaded = false;
        g_src_index.loaded = false;
        g_repo_mapping.loaded = false;
        g_pkg_index.entries.clear(); g_pkg_index.file_list.clear();
        g_src_index.entries.clear(); g_src_index.file_list.clear();
        g_repo_mapping.mapping.clear();
        {
            std::lock_guard<std::mutex> lock(g_metadata_cache_mutex);
            g_metadata_cache.clear();
        }
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
                    std::string safe_url = normalize_url(url);
                    dest_path = std::string(g_runepkg_lists_dir ? g_runepkg_lists_dir : "/var/lib/runepkg_dir/runepkg_db/lists") + "/" + safe_url;
                    bin_tasks.push_back({url, dest_path, "", 0, false});
                } else if (std::string(g_sources[i]->type) == "deb-src") {
                    url = base_url + "dists/" + suite + "/" + component + "/source/Sources.gz";
                    std::string safe_url = normalize_url(url);
                    dest_path = std::string(g_runepkg_lists_dir ? g_runepkg_lists_dir : "/var/lib/runepkg_dir/runepkg_db/lists") + "/" + safe_url;
                    src_tasks.push_back({url, dest_path, "", 0, false});
                }
            }
        }
        std::cout << "Downloading " << bin_tasks.size() + src_tasks.size() << " package lists..." << std::endl;

        /* Security Perimeter: Verify GPG signatures on InRelease index files */
        for (int i = 0; i < g_sources_count; i++) {
            std::string base_url = g_sources[i]->url;
            if (base_url.back() != '/') base_url += '/';
            std::string release_url = base_url + "dists/" + g_sources[i]->suite + "/InRelease";
            std::string safe_release = normalize_url(release_url);
            std::string release_path = std::string(g_runepkg_lists_dir ? g_runepkg_lists_dir : "/var/lib/runepkg_dir/runepkg_db/lists") + "/" + safe_release;
            if (download_file(release_url, release_path, 0, "InRelease (" + std::string(g_sources[i]->suite) + ")", true)) {
                if (runepkg::security::verify_gpg_signature(release_path)) {
                    std::cout << "  \033[1;32m[gpg]\033[0m Verified OpenPGP signature for " << g_sources[i]->suite << std::endl;
                }
            }
        }

        std::vector<std::future<bool>> futures;
        std::vector<DownloadTask*> all_tasks_ptrs;
        for (auto& t : bin_tasks) all_tasks_ptrs.push_back(&t);
        for (auto& t : src_tasks) all_tasks_ptrs.push_back(&t);
        { std::lock_guard<std::mutex> lock(g_progress_mutex); g_finished_count = 0; g_completed_names.clear(); g_active_downloads.clear(); g_total_to_download = all_tasks_ptrs.size(); }
        for (auto* task : all_tasks_ptrs) {
            if (runepkg_util_file_exists(task->dest_path.c_str())) {
                unlink(task->dest_path.c_str());
            }
        }

        ParallelExecutor pool(8);
        for (auto* task : all_tasks_ptrs) {
            std::string display_name; size_t dists_pos = task->url.find("/dists/");
            if (dists_pos != std::string::npos) { display_name = task->url.substr(dists_pos + 7); size_t last_slash = display_name.find_last_of('/'); if (last_slash != std::string::npos) display_name = display_name.substr(0, last_slash); }
            else display_name = task->url;

            futures.push_back(pool.enqueue([task, display_name]() {
                return download_file(task->url, task->dest_path, task->size, display_name, true);
            }));
        }
        for (size_t i = 0; i < all_tasks_ptrs.size(); i++) {
            all_tasks_ptrs[i]->success = futures[i].get();
            if (all_tasks_ptrs[i]->success) {
                std::string decompressed = all_tasks_ptrs[i]->dest_path;
                if (decompressed.size() > 3 && decompressed.substr(decompressed.size() - 3) == ".gz") {
                    decompressed = decompressed.substr(0, decompressed.size() - 3);
                } else {
                    decompressed += ".unpacked";
                }
                if (decompress_gz(all_tasks_ptrs[i]->dest_path, decompressed)) {
                    bool is_bin = false;
                    for(auto& t : bin_tasks) if(&t == all_tasks_ptrs[i]) is_bin = true;
                    if(is_bin) bin_pkg_files.push_back(decompressed);
                    else src_pkg_files.push_back(decompressed);
                }
            }
        }
        std::cout << std::endl << "Building Hybrid Binary and Source Indexes..." << std::endl;
        std::string db_dir_str = g_runepkg_db_dir ? g_runepkg_db_dir : "/var/lib/runepkg_dir/runepkg_db";
        build_index(bin_pkg_files, db_dir_str + "/repo_index.bin", db_dir_str + "/repo_files.txt");
        build_index(src_pkg_files, db_dir_str + "/repo_src_index.bin", db_dir_str + "/repo_src_files.txt");

        std::ofstream out_mapping(db_dir_str + "/repo_url_mapping.txt");
        if (out_mapping.is_open()) {
            for (int i = 0; i < g_sources_count; i++) {
                std::string base_url = g_sources[i]->url;
                if (base_url.back() != '/') base_url += '/';
                std::stringstream ss(g_sources[i]->components);
                std::string component;
                while (ss >> component) {
                    if (std::string(g_sources[i]->type) == "deb") {
                        std::string url = base_url + "dists/" + g_sources[i]->suite + "/" + component + "/binary-" + G_ARCH + "/Packages.gz";
                        std::string safe_url = normalize_url(url);
                        std::string decompressed = std::string(g_runepkg_lists_dir ? g_runepkg_lists_dir : "/var/lib/runepkg_dir/runepkg_db/lists") + "/" + safe_url;
                        if (decompressed.size() > 3 && decompressed.substr(decompressed.size() - 3) == ".gz") decompressed = decompressed.substr(0, decompressed.size() - 3);
                        out_mapping << "deb\t" << base_url << "\t" << decompressed << "\n";
                    } else if (std::string(g_sources[i]->type) == "deb-src") {
                        std::string url = base_url + "dists/" + g_sources[i]->suite + "/" + component + "/source/Sources.gz";
                        std::string safe_url = normalize_url(url);
                        std::string decompressed = std::string(g_runepkg_lists_dir ? g_runepkg_lists_dir : "/var/lib/runepkg_dir/runepkg_db/lists") + "/" + safe_url;
                        if (decompressed.size() > 3 && decompressed.substr(decompressed.size() - 3) == ".gz") decompressed = decompressed.substr(0, decompressed.size() - 3);
                        out_mapping << "deb-src\t" << base_url << "\t" << decompressed << "\n";
                    }
                }
            }
            out_mapping.close();
            std::string mapping_path = db_dir_str + "/repo_url_mapping.txt";
            int fd = open(mapping_path.c_str(), O_WRONLY);
            if (fd != -1) { fsync(fd); close(fd); }
        }

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
                            std::cout << "  \033[1;33m[upgradable]\033[0m " << name << ": " << node->data.version << " -> " << latest_versions[name] << std::endl;
                            upgradable_count++;
                        }
                    }
                    node = node->next;
                }
            }
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        std::cout << "\033[1;32mUpdate complete!\033[0m Binary/Source indexes updated. " << upgradable_count << " upgradable. Time: " << duration.count() / 1000.0 << "s" << std::endl;

        runepkg_storage_build_autocomplete_index();
        runepkg_resolver_harvest_graph(nullptr, nullptr);

        if (g_runepkg_db_dir) {
            std::string host_db_root = std::string(g_runepkg_db_dir) + "/host";
            std::string host_bin_graph = std::string(g_runepkg_db_dir) + "/runes_host.bin";
            if (runepkg_util_is_directory(host_db_root.c_str())) {
                runepkg_resolver_harvest_graph(host_db_root.c_str(), host_bin_graph.c_str());
            }
        }

        curl_global_cleanup();
        guard.commit();
        return 0;
    } catch (...) {
        std::cerr << "\033[1;31m[error]\033[0m Repository update encountered an internal exception." << std::endl;
        return -1;
    }
}

struct SearchResult {
    std::string name, version, arch, desc;
    bool installed = false;
};

extern "C" int runepkg_repo_search(const char *query) {
    try {
        if (!query || strlen(query) == 0) return -1;
        ensure_index_loaded(false);

        std::string q = query; std::transform(q.begin(), q.end(), q.begin(), ::tolower);
        std::cout << "Searching repository metadata..." << std::endl;
        std::map<std::string, SearchResult> results;

        for (const auto& filename : g_pkg_index.file_list) {
            std::ifstream infile(filename);
            if (!infile.is_open()) continue;
            std::string pkg_name, pkg_version, pkg_arch, pkg_desc, pkg_provides, line;
            while (std::getline(infile, line)) {
                if (line.empty() || line == "\r") {
                    if (!pkg_name.empty()) {
                        std::string combined = pkg_name + " " + pkg_desc + " " + pkg_provides;
                        std::transform(combined.begin(), combined.end(), combined.begin(), ::tolower);
                        if (combined.find(q) != std::string::npos) {
                            SearchResult res = {pkg_name, pkg_version, pkg_arch, pkg_desc, false};
                            if (runepkg_main_hash_table && runepkg_hash_search(runepkg_main_hash_table, pkg_name.c_str())) res.installed = true;
                            if (results.find(pkg_name) == results.end() || runepkg_util_compare_versions(pkg_version.c_str(), results[pkg_name].version.c_str()) > 0) results[pkg_name] = res;
                        }
                        pkg_name.clear(); pkg_version.clear(); pkg_arch.clear(); pkg_desc.clear(); pkg_provides.clear();
                    }
                    continue;
                }
                if (line.compare(0, 9, "Package: ") == 0) {
                    pkg_name = line.substr(9);
                    if (!pkg_name.empty() && pkg_name.back() == '\r') pkg_name.pop_back();
                } else if (line.compare(0, 9, "Version: ") == 0) {
                    pkg_version = line.substr(9);
                    if (!pkg_version.empty() && pkg_version.back() == '\r') pkg_version.pop_back();
                } else if (line.compare(0, 14, "Architecture: ") == 0) {
                    pkg_arch = line.substr(14);
                    if (!pkg_arch.empty() && pkg_arch.back() == '\r') pkg_arch.pop_back();
                } else if (line.compare(0, 13, "Description: ") == 0) {
                    pkg_desc = line.substr(13);
                    if (!pkg_desc.empty() && pkg_desc.back() == '\r') pkg_desc.pop_back();
                } else if (line.compare(0, 10, "Provides: ") == 0) {
                    pkg_provides = line.substr(10);
                    if (!pkg_provides.empty() && pkg_provides.back() == '\r') pkg_provides.pop_back();
                }
            }
            if (!pkg_name.empty()) {
                std::string combined = pkg_name + " " + pkg_desc + " " + pkg_provides; std::transform(combined.begin(), combined.end(), combined.begin(), ::tolower);
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
            std::cout << " \033[1;33m" << res.version << "\033[0m " << res.arch << std::endl << "  " << res.desc << std::endl << std::endl;
        }
        if (results.empty()) {
            std::cout << "No matches found for '" << query << "'." << std::endl;
        } else {
            std::cout << "Found " << results.size() << " matches." << std::endl;
        }
        return 0;
    } catch (...) {
        return -1;
    }
}

std::string get_package_url(const char *pkg_name, bool is_source, uint32_t *out_offset, std::string *out_metafile) {
    ensure_index_loaded(is_source);
    RepoIndex &idx_cache = is_source ? g_src_index : g_pkg_index;

    if (idx_cache.entries.empty()) return "";

    IndexEntry search_target;
    std::strncpy(search_target.name, pkg_name, 63);
    search_target.name[63] = '\0';
    auto range = std::equal_range(idx_cache.entries.begin(), idx_cache.entries.end(), search_target);
    if (range.first == range.second) return "";

    auto best_it = range.first;
    if (std::next(range.first) != range.second) {
        std::string best_version;
        for (auto it = range.first; it != range.second; ++it) {
            if (it->file_id >= idx_cache.file_list.size()) continue;
            std::ifstream meta(idx_cache.file_list[it->file_id]);
            if (!meta.is_open()) continue;
            meta.seekg(it->offset);
            std::string line, ver;
            while (std::getline(meta, line)) {
                if (line.empty() || line == "\r") break;
                if (line.compare(0, 9, "Version: ") == 0) {
                    ver = line.substr(9);
                    if (!ver.empty() && ver.back() == '\r') ver.pop_back();
                    break;
                }
            }
            if (best_version.empty() || runepkg_util_compare_versions(ver.c_str(), best_version.c_str()) > 0) {
                best_version = ver;
                best_it = it;
            }
        }
    }

    auto it = best_it;
    if (it->file_id >= idx_cache.file_list.size()) return "";
    if (out_offset) *out_offset = it->offset;
    if (out_metafile) *out_metafile = idx_cache.file_list[it->file_id];

    ensure_repo_mapping_loaded();
    std::string base_url = g_repo_mapping.mapping[idx_cache.file_list[it->file_id]];

    if (base_url.empty()) {
        for (int i = 0; i < g_sources_count; i++) {
            if (is_source && std::string(g_sources[i]->type) == "deb-src") { base_url = g_sources[i]->url; break; }
            else if (!is_source && std::string(g_sources[i]->type) == "deb") { base_url = g_sources[i]->url; break; }
        }
    }

    std::ifstream meta(idx_cache.file_list[it->file_id]);
    if (!meta.is_open()) return "";
    meta.seekg(it->offset);
    std::string rel_path, line;
    while (std::getline(meta, line)) {
        if (line.empty() || line == "\r") break;
        if (!is_source && line.compare(0, 10, "Filename: ") == 0) { rel_path = line.substr(10); break; }
        else if (is_source && line.compare(0, 11, "Directory: ") == 0) { rel_path = line.substr(11); break; }
    }
    if (!rel_path.empty() && rel_path.back() == '\r') rel_path.pop_back();

    if (base_url.empty()) return "";
    if (base_url.back() != '/') base_url += '/';
    return base_url + rel_path;
}

PkgMetadata get_package_metadata(const std::string& pkg_name) {
    {
        std::lock_guard<std::mutex> lock(g_metadata_cache_mutex);
        if (g_metadata_cache.count(pkg_name)) return g_metadata_cache[pkg_name];
    }

    PkgMetadata meta_data; meta_data.name = pkg_name;
    uint32_t offset = 0; std::string meta_file;
    std::string url = get_package_url(pkg_name.c_str(), false, &offset, &meta_file);
    if (url.empty()) return meta_data;
    meta_data.url = url; meta_data.filename = url.substr(url.find_last_of('/') + 1);
    std::ifstream meta(meta_file);
    if (meta.is_open()) {
        meta.seekg(offset); std::string line;
        while (std::getline(meta, line)) {
            if (line.empty() || line == "\r") break;
            if (line.compare(0, 9, "Package: ") == 0) {
                meta_data.name = line.substr(9);
                if (!meta_data.name.empty() && meta_data.name.back() == '\r') meta_data.name.pop_back();
            } else if (line.compare(0, 9, "Version: ") == 0) {
                meta_data.version = line.substr(9);
                if (!meta_data.version.empty() && meta_data.version.back() == '\r') meta_data.version.pop_back();
            } else if (line.compare(0, 9, "Depends: ") == 0) {
                meta_data.depends = line.substr(9);
                if (!meta_data.depends.empty() && meta_data.depends.back() == '\r') meta_data.depends.pop_back();
            } else if (line.compare(0, 13, "Pre-Depends: ") == 0) {
                meta_data.pre_depends = line.substr(13);
                if (!meta_data.pre_depends.empty() && meta_data.pre_depends.back() == '\r') meta_data.pre_depends.pop_back();
            } else if (line.compare(0, 10, "Provides: ") == 0) {
                std::string prov = line.substr(10);
                if (!prov.empty() && prov.back() == '\r') prov.pop_back();
                if (meta_data.provides.empty()) meta_data.provides = prov;
                else meta_data.provides += ", " + prov;
            } else if (line.compare(0, 8, "Source: ") == 0) {
                meta_data.source_name = line.substr(8);
                size_t space = meta_data.source_name.find(' ');
                if (space != std::string::npos) meta_data.source_name = meta_data.source_name.substr(0, space);
                if (!meta_data.source_name.empty() && meta_data.source_name.back() == '\r') meta_data.source_name.pop_back();
            } else if (line.compare(0, 6, "Size: ") == 0) {
                try { meta_data.size = std::stoull(line.substr(6)); } catch (...) { meta_data.size = 0; }
            } else if (line.compare(0, 13, "Description: ") == 0) {
                meta_data.description = line.substr(13);
                if (!meta_data.description.empty() && meta_data.description.back() == '\r') meta_data.description.pop_back();
            } else if (line.compare(0, 12, "Maintainer: ") == 0) {
                meta_data.maintainer = line.substr(12);
                if (!meta_data.maintainer.empty() && meta_data.maintainer.back() == '\r') meta_data.maintainer.pop_back();
            } else if (line.compare(0, 9, "Section: ") == 0) {
                meta_data.section = line.substr(9);
                if (!meta_data.section.empty() && meta_data.section.back() == '\r') meta_data.section.pop_back();
            } else if (line.compare(0, 10, "Priority: ") == 0) {
                meta_data.priority = line.substr(10);
                if (!meta_data.priority.empty() && meta_data.priority.back() == '\r') meta_data.priority.pop_back();
            } else if (line.compare(0, 10, "Homepage: ") == 0) {
                meta_data.homepage = line.substr(10);
                if (!meta_data.homepage.empty() && meta_data.homepage.back() == '\r') meta_data.homepage.pop_back();
            } else if (line.compare(0, 14, "Architecture: ") == 0) {
                meta_data.architecture = line.substr(14);
                if (!meta_data.architecture.empty() && meta_data.architecture.back() == '\r') meta_data.architecture.pop_back();
            }
        }
    }
    if (meta_data.source_name.empty()) meta_data.source_name = meta_data.name;

    {
        std::lock_guard<std::mutex> lock(g_metadata_cache_mutex);
        g_metadata_cache[pkg_name] = meta_data;
    }
    return meta_data;
}

SourceMetadata get_source_package_metadata(const std::string& pkg_name) {
    SourceMetadata meta_data; meta_data.name = pkg_name;
    uint32_t offset = 0; std::string meta_file;
    std::string base_url = get_package_url(pkg_name.c_str(), true, &offset, &meta_file);

    if (base_url.empty()) {
        PkgMetadata bin_meta = get_package_metadata(pkg_name);
        if (!bin_meta.url.empty() && !bin_meta.source_name.empty() && bin_meta.source_name != pkg_name) {
            runepkg_log_verbose("Source package '%s' not found, falling back to source of binary '%s' -> '%s'\n",
                                pkg_name.c_str(), pkg_name.c_str(), bin_meta.source_name.c_str());
            base_url = get_package_url(bin_meta.source_name.c_str(), true, &offset, &meta_file);
            if (!base_url.empty()) {
                meta_data.name = bin_meta.source_name;
            }
        }
    }

    if (base_url.empty()) return meta_data;
    meta_data.base_url = base_url;
    std::ifstream meta(meta_file);
    if (meta.is_open()) {
        meta.seekg(offset); std::string line; bool in_files = false;
        while (std::getline(meta, line)) {
            if (line.empty() || line == "\r") break;
            if (line.compare(0, 9, "Package: ") == 0) {
                meta_data.name = line.substr(9);
                if (!meta_data.name.empty() && meta_data.name.back() == '\r') meta_data.name.pop_back();
            } else if (line.compare(0, 15, "Build-Depends: ") == 0 ||
                       line.compare(0, 20, "Build-Depends-Arch: ") == 0 ||
                       line.compare(0, 21, "Build-Depends-Indep: ") == 0) {
                size_t colon = line.find(':');
                std::string val = line.substr(colon + 1);
                if (!val.empty() && val.back() == '\r') val.pop_back();
                if (!meta_data.build_depends.empty()) meta_data.build_depends += ", ";
                meta_data.build_depends += val;
            } else if (line.compare(0, 7, "Files: ") == 0) in_files = true;
            else if (in_files && line[0] == ' ') {
                std::stringstream ss(line); std::string hash, size_str, filename; ss >> hash >> size_str >> filename;
                if (!filename.empty()) { try { meta_data.files.push_back({filename, (size_t)std::stoull(size_str)}); } catch (...) { meta_data.files.push_back({filename, 0}); } }
            } else if (in_files && line[0] != ' ') in_files = false;
        }
    }
    return meta_data;
}

extern "C" int runepkg_repo_install_multiple(const char **pkg_names, int count) {
    if (!pkg_names || count <= 0) return -1;
    std::string index_path = std::string(g_runepkg_db_dir ? g_runepkg_db_dir : "/var/lib/runepkg_dir/runepkg_db") + "/runes_graph.bin";
    if (!runepkg_util_file_exists(index_path.c_str())) {
        std::cerr << "\033[1;31m[error]\033[0m Dependency graph not found. Please run 'runepkg update' first." << std::endl;
        return -1;
    }

    std::vector<RuneTargetPlan*> plans;
    size_t total_size = 0;
    std::unordered_set<std::string> unique_dest_files;
    std::vector<DownloadTask> tasks;

    std::cout << "\033[1;34m[runepkg]\033[0m Planning installation for " << count << " package(s)..." << std::endl;

    for (int i = 0; i < count; i++) {
        if (!pkg_names[i]) continue;
        RuneTargetPlan *plan = nullptr;
        if (runepkg_resolver_get_install_plan(pkg_names[i], &plan) == 0 && plan) {
            plans.push_back(plan);
            for (int j = 0; j < plan->node_count; j++) {
                std::string pkg_name = plan->nodes[j].package_name;
                std::string filename = plan->nodes[j].binary_filename ? plan->nodes[j].binary_filename : "";
                if (filename.empty()) {
                    PkgMetadata meta = get_package_metadata(pkg_name);
                    filename = meta.filename;
                }

                if (!filename.empty()) {
                    std::string base_file = filename.substr(filename.find_last_of('/') + 1);
                    std::string dest_path = std::string(g_download_dir ? g_download_dir : "/var/lib/runepkg_dir/download_dir") + "/" + base_file;

                    if (unique_dest_files.find(dest_path) == unique_dest_files.end()) {
                        unique_dest_files.insert(dest_path);

                        ensure_repo_mapping_loaded();
                        std::string url = get_package_url(pkg_name.c_str(), false, nullptr, nullptr);

                        if (!url.empty()) {
                            tasks.push_back({url, dest_path, pkg_name, plan->nodes[j].download_size, false});
                            total_size += plan->nodes[j].download_size;
                        }
                    }
                }
            }
        } else {
            std::cerr << "  -> \033[1;31mnot found:\033[0m " << pkg_names[i] << std::endl;
        }
    }

    if (tasks.empty()) {
        for (auto p : plans) runepkg_resolver_free_plan(p);
        return 0;
    }

    bool needs_confirm = true;
    if (needs_confirm) {
        int width = runepkg_util_get_terminal_width(); int current_line_len = 2; std::cout << "  ";
        int i = 0;
        for (const auto& t : tasks) {
            if (current_line_len + t.pkg_name.length() + 1 > (size_t)width && i > 0) { std::cout << "\n  "; current_line_len = 2; }
            std::cout << t.pkg_name; current_line_len += t.pkg_name.length(); if (i < (int)tasks.size() - 1) { std::cout << " "; current_line_len += 1; }
            i++;
        }
        char size_buf[64];
        std::cout << std::endl << std::endl << tasks.size() << " packages (" << runepkg_util_format_size(total_size, size_buf, sizeof(size_buf)) << ") will be installed. Continue? [\033[1;33my\033[0m/\033[1;33mN\033[0m] ";
        std::fflush(stdout); char resp[16]; bool confirmed = false;
        if (g_auto_confirm_deps) { std::cout << "\033[1;33my (auto)\033[0m" << std::endl; confirmed = true; }
        else if (std::fgets(resp, sizeof(resp), stdin) && (resp[0] == 'y' || resp[0] == 'Y')) { confirmed = true; }
        if (!confirmed) {
            for (auto p : plans) runepkg_resolver_free_plan(p);
            std::cout << "Installation cancelled." << std::endl;
            return -1;
        }
    }

    curl_global_init(CURL_GLOBAL_ALL);
    std::vector<std::future<bool>> futures;
    { std::lock_guard<std::mutex> lock(g_progress_mutex); g_finished_count = 0; g_completed_names.clear(); g_active_downloads.clear(); g_total_to_download = tasks.size(); }

    ParallelExecutor pool(8);
    for (size_t idx = 0; idx < tasks.size(); idx++) {
        futures.push_back(pool.enqueue([&tasks, idx]() {
            bool ok = download_file(tasks[idx].url, tasks[idx].dest_path, tasks[idx].size, tasks[idx].pkg_name);
            tasks[idx].success = ok;
            if (ok && runepkg_crypto_is_enabled()) {
                download_file(tasks[idx].url + ".sig", tasks[idx].dest_path + ".sig", 0, tasks[idx].pkg_name + " (sig)", false);
            }
            return ok;
        }));
    }

    int fail_count = 0;
    for (size_t idx = 0; idx < tasks.size(); idx++) {
        if (!futures[idx].get()) fail_count++;
    }

    if (fail_count == 0) {
        std::cout << std::endl << "\033[1;32m[success]\033[0m Downloaded " << tasks.size() << " packages to " << (g_download_dir ? g_download_dir : "/var/lib/runepkg_dir/download_dir") << std::endl;
    } else {
        std::cout << std::endl << "\033[1;31m[error]\033[0m " << fail_count << " downloads failed." << std::endl;
    }
    curl_global_cleanup();

    if (fail_count > 0) {
        for (auto p : plans) runepkg_resolver_free_plan(p);
        return -1;
    }

    extern bool g_batch_mode;
    g_batch_mode = true;

    if (g_batch_planned_packages) {
        runepkg_hash_clear_table(g_batch_planned_packages);
        for (auto plan : plans) {
            for (int i = 0; i < plan->node_count; i++) {
                PkgInfo dummy;
                runepkg_pack_init_package_info(&dummy);
                dummy.package_name = strdup(plan->nodes[i].package_name);
                if (plan->nodes[i].version) dummy.version = strdup(plan->nodes[i].version);
                runepkg_hash_add_package(g_batch_planned_packages, &dummy);
                runepkg_pack_free_package_info(&dummy);
            }
        }
    }

    std::unordered_set<std::string> installed_in_this_pass;
    for (auto plan : plans) {
        for (int i = 0; i < plan->node_count; i++) {
            std::string p_name = plan->nodes[i].package_name;
            if (installed_in_this_pass.count(p_name)) continue;

            std::string dest_path;
            for (const auto& t : tasks) {
                if (t.pkg_name == p_name && t.success) {
                    dest_path = t.dest_path;
                    break;
                }
            }
            if (!dest_path.empty()) {
                runepkg_install_batch_item(dest_path.c_str());
                installed_in_this_pass.insert(p_name);
            }
        }
    }

    for (auto p : plans) runepkg_resolver_free_plan(p);
    g_batch_mode = false;
    return 0;
}

extern "C" int runepkg_repo_install(const char *pkg_name) {
    return runepkg_repo_install_multiple(&pkg_name, 1);
}

extern "C" int runepkg_repo_package_exists(const char *pkg_name) {
    if (!pkg_name) return 0;
    std::string clean_pkg = pkg_name; size_t extra_pos = clean_pkg.find_first_of(":[<");
    if (extra_pos != std::string::npos) clean_pkg = clean_pkg.substr(0, extra_pos);
    clean_pkg.erase(0, clean_pkg.find_first_not_of(" \t")); clean_pkg.erase(clean_pkg.find_last_not_of(" \t") + 1);
    std::string url = get_package_url(clean_pkg.c_str(), false, nullptr, nullptr);
    return !url.empty();
}

extern "C" int runepkg_repo_download_multiple(const char **pkg_names, int count, bool recursive) {
    if (!pkg_names || count <= 0) return -1;
    TransactionContext tx_ctx;
    runepkg::RunepkgTransactionGuard guard(&tx_ctx, pkg_names[0] ? pkg_names[0] : "download", "1.0");
    runepkg::util::log_info("Starting repository download for " + std::to_string(count) + " packages");

    std::string index_path = std::string(g_runepkg_db_dir ? g_runepkg_db_dir : "/var/lib/runepkg_dir/runepkg_db") + "/runes_graph.bin";
    if (!runepkg_util_file_exists(index_path.c_str())) {
        std::cerr << "\033[1;31m[error]\033[0m Dependency graph not found. Please run 'runepkg update' first." << std::endl;
        return -1;
    }

    std::vector<RuneTargetPlan*> plans;
    size_t total_size = 0;
    std::unordered_set<std::string> unique_dest_files;
    std::vector<DownloadTask> tasks;

    std::cout << "\033[1;34m[runepkg]\033[0m Planning download for " << count << " package(s)..." << std::endl;

    for (int i = 0; i < count; i++) {
        if (!pkg_names[i]) continue;
        RuneTargetPlan *plan = nullptr;

        if (runepkg_resolver_get_install_plan(pkg_names[i], &plan) == 0 && plan) {
            plans.push_back(plan);

            for (int j = (recursive ? 0 : plan->node_count - 1); j < plan->node_count; j++) {
                std::string pkg_name = plan->nodes[j].package_name;
                std::string filename = plan->nodes[j].binary_filename ? plan->nodes[j].binary_filename : "";

                if (filename.empty()) {
                    PkgMetadata meta = get_package_metadata(pkg_name);
                    filename = meta.filename;
                }

                if (!filename.empty()) {
                    std::string base_file = filename.substr(filename.find_last_of('/') + 1);
                    std::string dest_path = std::string(g_download_dir ? g_download_dir : "/var/lib/runepkg_dir/download_dir") + "/" + base_file;

                    if (unique_dest_files.find(dest_path) == unique_dest_files.end()) {
                        unique_dest_files.insert(dest_path);

                        ensure_repo_mapping_loaded();
                        std::string url = get_package_url(pkg_name.c_str(), false, nullptr, nullptr);

                        if (!url.empty()) {
                            tasks.push_back({url, dest_path, pkg_name, plan->nodes[j].download_size, false});
                            total_size += plan->nodes[j].download_size;
                        }
                    }
                }
            }
        } else {
            std::cerr << "  -> \033[1;31mnot found:\033[0m " << pkg_names[i] << std::endl;
        }
    }

    if (tasks.empty()) {
        for (auto p : plans) runepkg_resolver_free_plan(p);
        return 0;
    }

    if (recursive) {
        int width = runepkg_util_get_terminal_width(); int current_line_len = 2; std::cout << "  ";
        int i = 0;
        for (const auto& t : tasks) {
            if (current_line_len + t.pkg_name.length() + 1 > (size_t)width && i > 0) { std::cout << "\n  "; current_line_len = 2; }
            std::cout << t.pkg_name; current_line_len += t.pkg_name.length(); if (i < (int)tasks.size() - 1) { std::cout << " "; current_line_len += 1; }
            i++;
        }
        char size_buf[64];
        std::cout << std::endl << std::endl << tasks.size() << " packages (" << runepkg_util_format_size(total_size, size_buf, sizeof(size_buf)) << ") will be downloaded. Continue? [\033[1;33my\033[0m/\033[1;33mN\033[0m] ";
        std::fflush(stdout); char resp[16]; bool confirmed = false;
        if (g_auto_confirm_deps) { std::cout << "\033[1;33my (auto)\033[0m" << std::endl; confirmed = true; }
        else if (std::fgets(resp, sizeof(resp), stdin) && (resp[0] == 'y' || resp[0] == 'Y')) confirmed = true;
        if (!confirmed) { for (auto p : plans) runepkg_resolver_free_plan(p); std::cout << "Download cancelled." << std::endl; return -1; }
    }

    curl_global_init(CURL_GLOBAL_ALL);
    std::vector<std::future<bool>> futures;
    { std::lock_guard<std::mutex> lock(g_progress_mutex); g_finished_count = 0; g_completed_names.clear(); g_active_downloads.clear(); g_total_to_download = tasks.size(); }
    ParallelExecutor pool(8);
    for (size_t idx = 0; idx < tasks.size(); idx++) {
        futures.push_back(pool.enqueue([&tasks, idx]() {
            bool ok = download_file(tasks[idx].url, tasks[idx].dest_path, tasks[idx].size, tasks[idx].pkg_name);
            tasks[idx].success = ok;
            if (ok && runepkg_crypto_is_enabled()) download_file(tasks[idx].url + ".sig", tasks[idx].dest_path + ".sig", 0, tasks[idx].pkg_name + " (sig)", false);
            return ok;
        }));
    }

    int fail_count = 0;
    for (size_t idx = 0; idx < tasks.size(); idx++) if (!futures[idx].get()) fail_count++;

    if (fail_count == 0) {
        std::cout << std::endl << "\033[1;32m[success]\033[0m Downloaded " << tasks.size() << " packages to " << (g_download_dir ? g_download_dir : "/var/lib/runepkg_dir/download_dir") << std::endl;
        guard.commit();
    } else {
        std::cout << std::endl << "\033[1;31m[error]\033[0m " << fail_count << " downloads failed." << std::endl;
        runepkg::util::log_error(std::to_string(fail_count) + " package downloads failed", tx_ctx.log_dir);
    }

    for (auto p : plans) runepkg_resolver_free_plan(p);
    curl_global_cleanup();
    return (fail_count == 0) ? 0 : -1;
}

extern "C" char* runepkg_repo_download(const char *pkg_name, bool recursive) {
    if (!pkg_name) return NULL;
    if (runepkg_repo_download_multiple(&pkg_name, 1, recursive) == 0) {
        PkgMetadata meta = get_package_metadata(pkg_name);
        std::string top_filename = meta.url.substr(meta.url.find_last_of('/') + 1);
        std::string top_dest = std::string(g_download_dir ? g_download_dir : "/var/lib/runepkg_dir/download_dir") + "/" + top_filename;
        return strdup(top_dest.c_str());
    }
    return NULL;
}

extern "C" int runepkg_repo_build_depends_download_multiple(const char **pkg_names, int count) {
    if (!pkg_names || count <= 0) return -1;
    std::string index_path = std::string(g_runepkg_db_dir ? g_runepkg_db_dir : "/var/lib/runepkg_dir/runepkg_db") + "/runes_graph.bin";
    if (!runepkg_util_file_exists(index_path.c_str())) {
        std::cerr << "\033[1;31m[error]\033[0m Dependency graph not found. Please run 'runepkg update' first." << std::endl;
        return -1;
    }

    std::vector<RuneTargetPlan*> plans;
    size_t total_size = 0;
    std::unordered_set<std::string> unique_dest_files;
    std::vector<DownloadTask> tasks;

    std::cout << "\033[1;34m[runepkg]\033[0m Resolving binary build-dependencies..." << std::endl;

    for (int i = 0; i < count; i++) {
        if (!pkg_names[i]) continue;
        RuneTargetPlan *plan = nullptr;

        if (runepkg_resolver_resolve_target(pkg_names[i], &plan) == 0 && plan) {
            plans.push_back(plan);
            for (int j = 0; j < plan->node_count; j++) {
                std::string pkg_name = plan->nodes[j].package_name;
                std::string filename = plan->nodes[j].binary_filename ? plan->nodes[j].binary_filename : "";

                if (filename.empty()) {
                    PkgMetadata meta = get_package_metadata(pkg_name);
                    filename = meta.filename;
                }

                if (!filename.empty()) {
                    std::string base_file = filename.substr(filename.find_last_of('/') + 1);
                    std::string dest_path = std::string(g_download_dir ? g_download_dir : "/var/lib/runepkg_dir/download_dir") + "/" + base_file;

                    if (unique_dest_files.find(dest_path) == unique_dest_files.end()) {
                        unique_dest_files.insert(dest_path);

                        ensure_repo_mapping_loaded();
                        std::string url = get_package_url(pkg_name.c_str(), false, nullptr, nullptr);

                        if (!url.empty()) {
                            tasks.push_back({url, dest_path, pkg_name, plan->nodes[j].download_size, false});
                            total_size += plan->nodes[j].download_size;
                        }
                    }
                }
            }
        } else {
            std::cerr << "  -> \033[1;31mnot found:\033[0m " << pkg_names[i] << std::endl;
        }
    }

    if (tasks.empty()) {
        for (auto p : plans) runepkg_resolver_free_plan(p);
        std::cout << "All build dependencies are already satisfied or not found." << std::endl;
        return 0;
    }

    int width = runepkg_util_get_terminal_width(); int current_line_len = 2; std::cout << "  ";
    int display_idx = 0;
    for (const auto& t : tasks) {
        if (current_line_len + t.pkg_name.length() + 1 > (size_t)width && display_idx > 0) { std::cout << "\n  "; current_line_len = 2; }
        std::cout << t.pkg_name; current_line_len += t.pkg_name.length(); if (display_idx < (int)tasks.size() - 1) { std::cout << " "; current_line_len += 1; }
        display_idx++;
    }
    char size_buf[64];
    std::cout << std::endl << std::endl << tasks.size() << " binary packages (" << runepkg_util_format_size(total_size, size_buf, sizeof(size_buf)) << ") will be downloaded. Continue? [\033[1;33my\033[0m/\033[1;33mN\033[0m] ";
    std::fflush(stdout); char resp[16]; bool confirmed = false;
    if (g_auto_confirm_deps) { std::cout << "\033[1;33my (auto)\033[0m" << std::endl; confirmed = true; }
    else if (std::fgets(resp, sizeof(resp), stdin) && (resp[0] == 'y' || resp[0] == 'Y')) confirmed = true;
    if (!confirmed) { for (auto p : plans) runepkg_resolver_free_plan(p); std::cout << "Download cancelled." << std::endl; return 0; }

    curl_global_init(CURL_GLOBAL_ALL);
    std::vector<std::future<bool>> futures;
    { std::lock_guard<std::mutex> lock(g_progress_mutex); g_finished_count = 0; g_completed_names.clear(); g_active_downloads.clear(); g_total_to_download = tasks.size(); }
    ParallelExecutor pool(8);
    for (size_t idx = 0; idx < tasks.size(); idx++) {
        futures.push_back(pool.enqueue([&tasks, idx]() {
            bool ok = download_file(tasks[idx].url, tasks[idx].dest_path, tasks[idx].size, tasks[idx].pkg_name);
            tasks[idx].success = ok;
            if (ok && runepkg_crypto_is_enabled()) download_file(tasks[idx].url + ".sig", tasks[idx].dest_path + ".sig", 0, tasks[idx].pkg_name + " (sig)", false);
            return ok;
        }));
    }
    int fail_count = 0;
    for (size_t idx = 0; idx < tasks.size(); idx++) { if (!futures[idx].get()) fail_count++; }
    if (fail_count == 0) std::cout << std::endl << "\033[1;32m[success]\033[0m Downloaded " << tasks.size() << " packages to " << (g_download_dir ? g_download_dir : "/var/lib/runepkg_dir/download_dir") << std::endl;
    else std::cout << std::endl << "\033[1;31m[error]\033[0m " << fail_count << " downloads failed." << std::endl;

    extern bool g_batch_mode; g_batch_mode = true;
    if (installing_packages) {
        for (const auto& t : tasks) {
            PkgInfo dummy; runepkg_pack_init_package_info(&dummy);
            dummy.package_name = strdup(t.pkg_name.c_str());
            runepkg_hash_add_package(installing_packages, &dummy); runepkg_pack_free_package_info(&dummy);
        }
    }
    for (const auto& t : tasks) { if (!t.success) continue; runepkg_install_batch_item(t.dest_path.c_str()); }

    for (auto p : plans) runepkg_resolver_free_plan(p);
    g_batch_mode = false; curl_global_cleanup();
    return (fail_count == 0) ? 0 : -1;
}

extern "C" int runepkg_repo_build_depends_download(const char *pkg_name) {
    return runepkg_repo_build_depends_download_multiple(&pkg_name, 1);
}

extern "C" int runepkg_upgrade(void) {
    std::cout << "\033[1;32m[runepkg]\033[0m Starting full system upgrade..." << std::endl;
    auto latest_versions = get_latest_versions(); std::vector<std::string> to_upgrade;
    if (runepkg_main_hash_table) {
        for (size_t i = 0; i < runepkg_main_hash_table->size; i++) {
            runepkg_hash_node_t *node = runepkg_main_hash_table->buckets[i];
            while (node) {
                std::string name = node->data.package_name;
                if (latest_versions.count(name) && runepkg_util_compare_versions(latest_versions[name].c_str(), node->data.version) > 0) to_upgrade.push_back(name);
                node = node->next;
            }
        }
    }
    if (to_upgrade.empty()) { std::cout << "All packages are already up to date." << std::endl; return 0; }

    std::vector<const char*> pkgs_c;
    for (const auto& s : to_upgrade) pkgs_c.push_back(s.c_str());

    return runepkg_repo_install_multiple(pkgs_c.data(), pkgs_c.size());
}

extern "C" int runepkg_repo_source_download_multiple(const char **pkg_names, int count) {
    if (!pkg_names || count <= 0) return -1;

    std::vector<SourceMetadata> all_meta;
    std::vector<std::string> missing_pkgs;

    for (int i = 0; i < count; i++) {
        SourceMetadata meta = get_source_package_metadata(pkg_names[i]);
        if (meta.base_url.empty()) {
            printf("  -> %-30s \033[1;31mnot found!\033[0m\n", pkg_names[i]);
            missing_pkgs.push_back(pkg_names[i]);
        } else {
            printf("  -> %-30s \033[1;32mfound\033[0m\n", pkg_names[i]);
            all_meta.push_back(meta);
        }
    }

    if (!missing_pkgs.empty()) {
        for (const auto& pkg : missing_pkgs) {
            std::cerr << "\033[1;31m[error]\033[0m Could not find source package metadata for '" << pkg << "'" << std::endl;
        }
        return -1;
    }
    if (all_meta.empty()) return -1;

    size_t total_size = 0;
    size_t total_files = 0;
    for (const auto& meta : all_meta) {
        for (const auto& sf : meta.files) total_size += sf.size;
        total_files += meta.files.size();
    }

    char size_buf[64];
    std::cout << "\033[1;34m[runepkg]\033[0m Downloading source package(s) ("
              << runepkg_util_format_size(total_size, size_buf, sizeof(size_buf)) << " total, "
              << total_files << " files)..." << std::endl;

    curl_global_init(CURL_GLOBAL_ALL);
    std::vector<std::future<bool>> futures;
    {
        std::lock_guard<std::mutex> lock(g_progress_mutex);
        g_finished_count = 0;
        g_completed_names.clear();
        g_active_downloads.clear();
        g_total_to_download = (int)total_files;
    }

    if (!g_build_dir) {
        std::cerr << "\033[1;31m[error]\033[0m Build directory not configured." << std::endl;
        return -1;
    }

    ParallelExecutor pool(8);
    for (const auto& meta : all_meta) {
        for (size_t idx = 0; idx < meta.files.size(); idx++) {
            std::string url = meta.base_url + "/" + meta.files[idx].filename;
            std::string dest = std::string(g_build_dir) + "/" + meta.files[idx].filename;
            size_t size = meta.files[idx].size;
            std::string filename = meta.files[idx].filename;
            futures.push_back(pool.enqueue([url, dest, size, filename]() {
                return download_file(url, dest, size, filename);
            }));
        }
    }

    int downloaded = 0;
    for (size_t idx = 0; idx < futures.size(); idx++) if (futures[idx].get()) downloaded++;
    curl_global_cleanup();

    bool unpack_success = true;
    if (downloaded == (int)total_files) {
        std::cout << std::endl << "\033[1;32m[success]\033[0m Downloaded " << downloaded << " files to " << g_build_dir << std::endl;
        for (const auto& meta : all_meta) {
            std::string dsc_path;
            for (const auto& sf : meta.files) {
                if (sf.filename.size() > 4 && sf.filename.substr(sf.filename.size() - 4) == ".dsc") {
                    dsc_path = std::string(g_build_dir) + "/" + sf.filename;
                    break;
                }
            }
            if (!dsc_path.empty()) {
                printf("\033[1;34m[notice]\033[0m Source unpacking is currently disabled for stability.\n");
            }
        }
        runepkg_storage_build_autocomplete_index();
    } else {
        std::cout << std::endl << "\033[1;31m[error]\033[0m Failed to download source package files (" << downloaded << "/" << total_files << " ok)." << std::endl;
        return -1;
    }
    return unpack_success ? 0 : -1;
}

extern "C" int runepkg_repo_source_download(const char *pkg_name) {
    return runepkg_repo_source_download_multiple(&pkg_name, 1);
}

extern "C" int runepkg_repo_source_build_depends_download_multiple(const char **pkg_names, int count) {
    if (!pkg_names || count <= 0) return -1;

    for (int i = 0; i < count; i++) {
        RuneTargetPlan *plan = nullptr;
        if (runepkg_resolver_resolve_target(pkg_names[i], &plan) == 0 && plan) {
            std::vector<const char*> build_srcs;
            for (int j = 0; j < plan->node_count; j++) {
                build_srcs.push_back(plan->nodes[j].package_name);
            }
            if (!build_srcs.empty()) {
                runepkg_repo_source_download_multiple(build_srcs.data(), build_srcs.size());
            }
            runepkg_resolver_free_plan(plan);
        }
    }
    return 0;
}

extern "C" int runepkg_repo_source_build_depends_download(const char *pkg_name) {
    return runepkg_repo_source_build_depends_download_multiple(&pkg_name, 1);
}

extern "C" int runepkg_repo_source_depends_download_multiple(const char **pkg_names, int count) {
    if (!pkg_names || count <= 0) return -1;

    for (int i = 0; i < count; i++) {
        RuneTargetPlan *plan = nullptr;
        if (runepkg_resolver_get_install_plan(pkg_names[i], &plan) == 0 && plan) {
            std::vector<const char*> dep_srcs;
            for (int j = 0; j < plan->node_count; j++) {
                dep_srcs.push_back(plan->nodes[j].package_name);
            }
            if (!dep_srcs.empty()) {
                runepkg_repo_source_download_multiple(dep_srcs.data(), dep_srcs.size());
            }
            runepkg_resolver_free_plan(plan);
        }
    }
    return 0;
}

extern "C" int runepkg_repo_source_depends_download(const char *pkg_name) {
    return runepkg_repo_source_depends_download_multiple(&pkg_name, 1);
}

extern "C" int runepkg_repo_info(const char *pkg_name) {
    if (!pkg_name) return -1;

    std::string index_path = std::string(g_runepkg_db_dir ? g_runepkg_db_dir : "/var/lib/runepkg_dir/runepkg_db") + "/repo_index.bin";
    if (!runepkg_util_file_exists(index_path.c_str())) {
        std::cerr << "\033[1;31m[error]\033[0m Repository index not found. Please run 'runepkg update' first." << std::endl;
        return -1;
    }

    PkgMetadata meta = get_package_metadata(pkg_name);
    if (meta.url.empty()) {
        printf("'%s' not found... did you mean?\n\n", pkg_name);
        char suggestions[12][PATH_MAX];
        int count = runepkg_completion_get_repo_suggestions(pkg_name, suggestions, 12);
        if (count > 0) {
            const char *items[12];
            for (int i = 0; i < count; i++) items[i] = suggestions[i];
            runepkg_util_print_columns(items, count, "    ");
        }
        return -1;
    }

    printf("Package: %s\n", meta.name.c_str());
    printf("Version: %s\n", meta.version.empty() ? "(unknown)" : meta.version.c_str());
    printf("Architecture: %s\n", meta.architecture.empty() ? "(unknown)" : meta.architecture.c_str());
    printf("Maintainer: %s\n", meta.maintainer.empty() ? "(unknown)" : meta.maintainer.c_str());
    printf("Description: %s\n", meta.description.empty() ? "(unknown)" : meta.description.c_str());
    printf("Depends: %s\n", meta.depends.empty() ? "(none)" : meta.depends.c_str());
    char size_buf[32];
    printf("Download-Size: %s\n", runepkg_util_format_size(meta.size, size_buf, sizeof(size_buf)));
    printf("Section: %s\n", meta.section.empty() ? "(unknown)" : meta.section.c_str());
    printf("Priority: %s\n", meta.priority.empty() ? "(unknown)" : meta.priority.c_str());
    printf("Homepage: %s\n", meta.homepage.empty() ? "(unknown)" : meta.homepage.c_str());

    if (runepkg_main_hash_table) {
        PkgInfo *info = runepkg_hash_search(runepkg_main_hash_table, meta.name.c_str());
        if (info) {
            printf("Status: installed (version %s)\n", info->version);
        } else {
            printf("Status: not installed\n");
        }
    }

    return 0;
}

extern "C" char* runepkg_repo_find_source_for_binary(const char* bin_pkg_name) {
    if (!bin_pkg_name) return nullptr;
    SourceMetadata meta = get_source_package_metadata(bin_pkg_name);
    if (!meta.name.empty()) {
        return strdup(meta.name.c_str());
    }
    return strdup(bin_pkg_name);
}

extern "C" char* runepkg_repo_get_source_build_depends(const char* src_pkg_name) {
    if (!src_pkg_name) return nullptr;
    SourceMetadata meta = get_source_package_metadata(src_pkg_name);
    if (!meta.build_depends.empty()) {
        return strdup(meta.build_depends.c_str());
    }
    return nullptr;
}
