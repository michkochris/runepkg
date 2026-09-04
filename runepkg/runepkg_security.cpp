/******************************************************************************/
/* Filename:    runepkg_security.cpp                                           */
/* Author:      <michkochris@gmail.com>                                        */
/* Date:        2026-08-29                                                     */
/* Description: Security Perimeter, Trust Anchor & Archive Sanitizer           */
/* License:     GPL v3                                                         */
/******************************************************************************/

#include "runepkg_security.hpp"
#include "runepkg_util_cpp.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <memory>
#include <array>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>

#if defined(__has_include)
  #if __has_include(<openssl/evp.h>)
    #include <openssl/evp.h>
    #define HAVE_OPENSSL_EVP 1
  #endif
#endif

namespace runepkg::security {

namespace fs = std::filesystem;

/* --- Cryptographic Verification --- */

std::vector<std::string> find_system_keyring_paths() {
    std::vector<std::string> keyrings;
    static const char* search_dirs[] = {
        "/etc/apt/trusted.gpg.d",
        "/usr/share/keyrings",
        "/etc/runepkg/keyrings",
        "/etc/apt"
    };

    for (const char* dir : search_dirs) {
        std::error_code ec;
        if (fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (entry.is_regular_file(ec)) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".gpg" || ext == ".gpgv" || ext == ".kbx") {
                        keyrings.push_back(entry.path().string());
                    }
                }
            }
        }
    }

    if (fs::exists("/etc/apt/trusted.gpg")) {
        keyrings.push_back("/etc/apt/trusted.gpg");
    }
    return keyrings;
}

static std::string escape_shell_arg(const std::string& arg) {
    std::string escaped = "'";
    for (char c : arg) {
        if (c == '\'') {
            escaped += "'\\''";
        } else {
            escaped += c;
        }
    }
    escaped += "'";
    return escaped;
}

bool verify_gpg_signature(const std::string& filepath, const std::string& keyring_path) {
    if (!fs::exists(filepath)) {
        return false;
    }

    std::vector<std::string> keyrings;
    if (!keyring_path.empty() && fs::exists(keyring_path)) {
        keyrings.push_back(keyring_path);
    } else {
        keyrings = find_system_keyring_paths();
    }

    std::string keyrings_arg;
    for (const auto& kr : keyrings) {
        keyrings_arg += " --keyring " + escape_shell_arg(kr);
    }

    std::string cmd = "gpgv --quiet" + keyrings_arg + " " + escape_shell_arg(filepath);
    std::string output;
    int exit_code = -1;
    bool ok = util::exec_command(cmd, output, exit_code);
    return ok && (exit_code == 0);
}

static std::string compute_hash_evp(const std::string& filepath, const char* digest_name) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

#if defined(HAVE_OPENSSL_EVP)
    const EVP_MD* md = EVP_get_digestbyname(digest_name);
    if (!md) {
        return "";
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return "";
    }

    if (EVP_DigestInit_ex(ctx, md, nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    constexpr size_t BUF_SIZE = 65536;
    std::vector<char> buffer(BUF_SIZE);

    while (file.read(buffer.data(), BUF_SIZE) || file.gcount() > 0) {
        if (EVP_DigestUpdate(ctx, buffer.data(), static_cast<size_t>(file.gcount())) != 1) {
            EVP_MD_CTX_free(ctx);
            return "";
        }
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    EVP_MD_CTX_free(ctx);

    std::ostringstream hex_stream;
    for (unsigned int i = 0; i < digest_len; ++i) {
        hex_stream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    return hex_stream.str();
#else
    /* Fallback hash execution using sha256sum / sha512sum CLI */
    std::string tool = (std::strcmp(digest_name, "sha512") == 0) ? "sha512sum " : "sha256sum ";
    std::string output;
    int exit_code = -1;
    if (util::exec_command(tool + filepath, output, exit_code) && exit_code == 0) {
        std::istringstream iss(output);
        std::string hash;
        if (iss >> hash) {
            return util::to_lower(hash);
        }
    }
    return "";
#endif
}

static bool constant_time_compare(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    volatile unsigned char result = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        result |= (static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]));
    }
    return result == 0;
}

bool verify_sha256_checksum(const std::string& filepath, const std::string& expected_hex_hash) {
    if (filepath.empty() || expected_hex_hash.empty()) {
        return false;
    }
    std::string calculated = compute_hash_evp(filepath, "sha256");
    if (calculated.empty()) {
        return false;
    }
    return constant_time_compare(util::to_lower(calculated), util::to_lower(expected_hex_hash));
}

bool verify_sha512_checksum(const std::string& filepath, const std::string& expected_hex_hash) {
    if (filepath.empty() || expected_hex_hash.empty()) {
        return false;
    }
    std::string calculated = compute_hash_evp(filepath, "sha512");
    if (calculated.empty()) {
        return false;
    }
    return constant_time_compare(util::to_lower(calculated), util::to_lower(expected_hex_hash));
}

/* --- Archive Sanitization & Path Traversal Defense --- */

bool sanitize_extract_path(const std::string& target_base_dir,
                          const std::string& entry_path,
                          std::string& resolved_path_out) {
    resolved_path_out.clear();
    if (target_base_dir.empty() || entry_path.empty()) {
        return false;
    }

    std::error_code ec;
    fs::path base_path = fs::weakly_canonical(target_base_dir, ec);
    if (ec) {
        return false;
    }

    /* Reject explicit absolute path entries or explicit relative traversal */
    if (entry_path.find("..") != std::string::npos) {
        /* Check if components attempt traversal */
        fs::path relative_entry(entry_path);
        for (const auto& part : relative_entry) {
            if (part == "..") {
                return false;
            }
        }
    }

    fs::path raw_combined = base_path / fs::path(entry_path).relative_path();
    fs::path normalized = fs::weakly_canonical(raw_combined, ec);
    if (ec) {
        return false;
    }

    std::string base_str = base_path.string();
    std::string norm_str = normalized.string();

    /* Ensure normalized path starts with base_path prefix (jail check) */
    if (!util::starts_with(norm_str, base_str)) {
        return false;
    }

    resolved_path_out = norm_str;
    return true;
}

/* --- Resource Limits & DoS Mitigation --- */

bool apply_extraction_resource_limits(size_t max_uncompressed_bytes, size_t max_files) {
    struct rlimit rl;

    /* Limit max single file size */
    if (max_uncompressed_bytes > 0) {
        rl.rlim_cur = static_cast<rlim_t>(max_uncompressed_bytes);
        rl.rlim_max = static_cast<rlim_t>(max_uncompressed_bytes);
        if (setrlimit(RLIMIT_FSIZE, &rl) != 0) {
            return false;
        }
    }

    /* Limit max open file descriptors */
    if (max_files > 0) {
        rl.rlim_cur = static_cast<rlim_t>(max_files + 32);
        rl.rlim_max = static_cast<rlim_t>(max_files + 64);
        if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
            return false;
        }
    }

    return true;
}

ExtractionQuotaTracker::ExtractionQuotaTracker(uint64_t max_bytes, uint32_t max_file_count)
    : max_bytes_(max_bytes), max_files_(max_file_count) {}

bool ExtractionQuotaTracker::add_bytes(uint64_t bytes) {
    if (max_bytes_ > 0 && (current_bytes_ + bytes > max_bytes_)) {
        return false;
    }
    current_bytes_ += bytes;
    return true;
}

bool ExtractionQuotaTracker::increment_file_count() {
    if (max_files_ > 0 && (current_files_ + 1 > max_files_)) {
        return false;
    }
    current_files_++;
    return true;
}

/* --- Privilege Management Subsystem --- */

bool is_root() {
    return geteuid() == 0;
}

bool drop_privileges(const std::string& username) {
    if (!is_root()) {
        return true; /* Already unprivileged */
    }

    const char* user_name = username.empty() ? "_apt" : username.c_str();
    struct passwd* pw = getpwnam(user_name);
    if (!pw) {
        pw = getpwnam("nobody");
        if (!pw) {
            return false;
        }
    }

    sigset_t oldset, newset;
    sigfillset(&newset);
    sigprocmask(SIG_BLOCK, &newset, &oldset);

    bool ok = false;
    if (initgroups(pw->pw_name, pw->pw_gid) == 0 &&
        setgid(pw->pw_gid) == 0 &&
        setuid(pw->pw_uid) == 0 &&
        setuid(0) == -1) { /* Verify privilege drop */
        ok = true;
    }

    sigprocmask(SIG_SETMASK, &oldset, nullptr);
    return ok;
}

int SandboxWorker::execute(const std::function<int()>& worker_func, const std::string& username) {
    if (!is_root()) {
        return worker_func();
    }

    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        if (!drop_privileges(username)) {
            _exit(127);
        }
        int code = worker_func();
        _exit(code);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        return -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return -1;
}

/* --- FFI Boundary Safety Wrappers --- */

bool validate_ffi_string(const char* str, size_t max_allowed_len) {
    if (!str) {
        return false;
    }
    size_t len = 0;
    while (len < max_allowed_len) {
        if (str[len] == '\0') {
            return true;
        }
        len++;
    }
    return false;
}

void* secure_ffi_buffer_alloc(size_t size) {
    if (size == 0) return nullptr;
    void* ptr = std::calloc(1, size);
    return ptr;
}

void secure_ffi_buffer_free(void* ptr, size_t size) {
    if (!ptr) return;
    if (size > 0) {
#if defined(HAVE_OPENSSL_EVP)
        OPENSSL_cleanse(ptr, size);
#else
        volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
        for (size_t i = 0; i < size; ++i) {
            p[i] = 0;
        }
#endif
    }
    std::free(ptr);
}

} // namespace runepkg::security

/* -------------------------------------------------------------------------- */
/* C FFI Bridge Implementation                                                */
/* -------------------------------------------------------------------------- */

extern "C" {

int runepkg_security_verify_sha256(const char* filepath, const char* expected_hash) {
    try {
        if (!filepath || !expected_hash) return 0;
        return runepkg::security::verify_sha256_checksum(filepath, expected_hash) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int runepkg_security_verify_sha512(const char* filepath, const char* expected_hash) {
    try {
        if (!filepath || !expected_hash) return 0;
        return runepkg::security::verify_sha512_checksum(filepath, expected_hash) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int runepkg_security_verify_gpg(const char* filepath, const char* keyring_path) {
    try {
        if (!filepath) return 0;
        std::string keyring = keyring_path ? keyring_path : "";
        return runepkg::security::verify_gpg_signature(filepath, keyring) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int runepkg_security_sanitize_path(const char* base_dir, const char* entry_path, char* out_buf, size_t max_len) {
    try {
        if (out_buf && max_len > 0) {
            std::memset(out_buf, 0, max_len);
        }
        if (!base_dir || !entry_path || !out_buf || max_len == 0) return 0;
        std::string resolved;
        if (!runepkg::security::sanitize_extract_path(base_dir, entry_path, resolved)) {
            return 0;
        }
        if (resolved.size() >= max_len) {
            return 0;
        }
        std::strncpy(out_buf, resolved.c_str(), max_len - 1);
        out_buf[max_len - 1] = '\0';
        return 1;
    } catch (...) {
        if (out_buf && max_len > 0) {
            std::memset(out_buf, 0, max_len);
        }
        return 0;
    }
}

int runepkg_security_apply_rlimits(size_t max_bytes, size_t max_files) {
    try {
        return runepkg::security::apply_extraction_resource_limits(max_bytes, max_files) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int runepkg_security_drop_privileges(const char* username) {
    try {
        std::string user = username ? username : "";
        return runepkg::security::drop_privileges(user) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int runepkg_security_drop_privileges_for_worker(const char* username) {
    try {
        std::string user = username ? username : "_apt";
        return runepkg::security::drop_privileges(user) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int runepkg_security_is_root(void) {
    try {
        return runepkg::security::is_root() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

} // extern "C"
