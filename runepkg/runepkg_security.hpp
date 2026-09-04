/******************************************************************************/
/* Filename:    runepkg_security.hpp                                           */
/* Author:      <michkochris@gmail.com>                                        */
/* Date:        2026-08-29                                                     */
/* Description: Security Perimeter, Trust Anchor & Archive Sanitizer           */
/* License:     GPL v3                                                         */
/******************************************************************************/

#ifndef RUNEPKG_SECURITY_HPP
#define RUNEPKG_SECURITY_HPP

#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <cstdint>
#include <cstddef>
#include <sys/types.h>

namespace runepkg::security {

/* --- Cryptographic Verification Subsystem --- */

/** Discovers default system GPG keyrings under /etc/apt/trusted.gpg.d/, /usr/share/keyrings/, /etc/runepkg/keyrings/. */
std::vector<std::string> find_system_keyring_paths();

/** Verifies GPG/OpenPGP signature of a file against a trusted keyring file or system keyrings. */
bool verify_gpg_signature(const std::string& filepath, const std::string& keyring_path = "");

/** Computes SHA256 checksum of file in streaming chunks and compares with expected hex hash. */
bool verify_sha256_checksum(const std::string& filepath, const std::string& expected_hex_hash);

/** Computes SHA512 checksum of file in streaming chunks and compares with expected hex hash. */
bool verify_sha512_checksum(const std::string& filepath, const std::string& expected_hex_hash);

/* --- Archive Sanitization & Path Traversal Defense --- */

/**
 * Sanitizes archive entry path, ensuring it stays strictly inside target_base_dir.
 * Canonicalizes path and rejects relative traversal (..) or absolute escapes.
 * Returns true if valid, writing resolved path to resolved_path_out.
 */
bool sanitize_extract_path(const std::string& target_base_dir,
                          const std::string& entry_path,
                          std::string& resolved_path_out);

/* --- Resource Limits & DoS Mitigation --- */

/** Applies POSIX rlimits (max file size, max open file descriptors, max memory). */
bool apply_extraction_resource_limits(size_t max_uncompressed_bytes, size_t max_files);

/** Streaming quota tracker for extraction routines to prevent zip bombs. */
class ExtractionQuotaTracker {
public:
    ExtractionQuotaTracker(uint64_t max_bytes, uint32_t max_file_count);

    /** Adds extracted bytes; returns false if quota exceeded. */
    bool add_bytes(uint64_t bytes);

    /** Increments extracted file count; returns false if limit exceeded. */
    bool increment_file_count();

    uint64_t get_total_bytes() const { return current_bytes_; }
    uint32_t get_total_files() const { return current_files_; }

private:
    uint64_t max_bytes_;
    uint32_t max_files_;
    uint64_t current_bytes_ = 0;
    uint32_t current_files_ = 0;
};

/* --- Privilege Management Subsystem --- */

/** Transitions process from root to specified unprivileged user/group. */
bool drop_privileges(const std::string& username);

/** Checks if current effective user ID is root (0). */
bool is_root();

/** Process isolation worker that executes a function in a forked child process with dropped privileges. */
class SandboxWorker {
public:
    static int execute(const std::function<int()>& worker_func, const std::string& username = "_apt");
};

/* --- FFI Boundary Safety Wrappers --- */

/** Validates raw C string pointer, null-termination, and length constraints. */
bool validate_ffi_string(const char* str, size_t max_allowed_len);

/** Allocates a zero-initialized buffer for FFI payloads. */
void* secure_ffi_buffer_alloc(size_t size);

/** Zeroes memory securely before freeing buffer. */
void secure_ffi_buffer_free(void* ptr, size_t size);

} // namespace runepkg::security

/* -------------------------------------------------------------------------- */
/* C FFI Bridge Declarations                                                  */
/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

int runepkg_security_verify_sha256(const char* filepath, const char* expected_hash);
int runepkg_security_verify_sha512(const char* filepath, const char* expected_hash);
int runepkg_security_verify_gpg(const char* filepath, const char* keyring_path);
int runepkg_security_sanitize_path(const char* base_dir, const char* entry_path, char* out_buf, size_t max_len);
int runepkg_security_apply_rlimits(size_t max_bytes, size_t max_files);
int runepkg_security_drop_privileges(const char* username);
int runepkg_security_drop_privileges_for_worker(const char* username);
int runepkg_security_is_root(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNEPKG_SECURITY_HPP */
