# RunePkg C++ Extended Engine & Security Perimeter Architecture

---

## 1. Overview & Architectural Vision

**runepkg** implements a high-performance **dual-engine architecture**:
- **Minimal Core (Pure ISO C89/C90)**: A zero-dependency, ultra-compact package engine designed for recovery media and embedded targets.
- **Extended C++ Engine (C++17 FFI Suite)**: A multi-threaded, security-hardened engine providing parallel repository synchronization, 70k+ package graph harvesting, OpenPGP cryptographic validation, and automated cross-toolchain package forging.

To guarantee project maintainability, memory safety, and complete isolation between the C89 core and C++ extensions, all C++ components are structured around a strict **Foundational Dependency Hierarchy** and **Header Isolation Model**.

---

## 2. Foundational C++ Module Hierarchy & Compilation Order

C++ modules in the `Makefile` compile strictly in dependency order—ensuring higher-level features rely on verified foundational primitives:

```text
[ C89 Core Engine ] ◄── (C FFI Boundary / runepkg_cpp_ffi.h) ──► [ C++ Extended Engine ]
                                                                       │
┌────────────────────────────────----------------──────────────────────┘
│
├─► 1. runepkg_security.cpp    (Security Perimeter & Trust Anchor)
│
├─► 2. runepkg_util_cpp.cpp    (Core C++ Utilities & FFI Bridge Helpers)
│
├─► 3. runepkg_resolver.cpp    (70k+ Repository Graph Harvester & Resolver)
│
└─► 4. runepkg_network.cpp     (Parallel Multi-threaded CURL Fetch Engine)
```

### Module Breakdown

1. **`runepkg_security` (`runepkg_security.hpp` / `runepkg_security.cpp`)**:
   - **Perimeter Security**: Acts as the system trust anchor.
   - **Path Traversal Defense**: `sanitize_extract_path` canonicalizes paths using `std::filesystem::weakly_canonical` and performs strict jail checks to reject relative traversal (`..`) or absolute path escapes during archive extraction.
   - **OpenPGP Keyring Discovery & Verification**: `verify_gpg_signature` scans system keyrings (`/etc/apt/trusted.gpg.d/`, `/usr/share/keyrings/`, `/etc/runepkg/keyrings/`) to verify repository `InRelease` / `Release` signatures via `gpgv`.
   - **Streaming Hash Validation**: `verify_sha256_checksum` and `verify_sha512_checksum` verify file integrity in streaming chunks using OpenSSL EVP digests, immediately unlinking bad downloads.
   - **POSIX Resource Limits**: `apply_extraction_resource_limits` enforces `rlimit` bounds (2GB max file size, 1024 open file descriptors) during package extraction to prevent zip bombs.
   - **Privilege Dropping & Process Isolation**: `SandboxWorker::execute` and `drop_privileges` execute untrusted archive extraction (`tar`/`ar`) in child `fork` processes under unprivileged credentials (`_apt` or `nobody`).

2. **`runepkg_util_cpp` (`runepkg_util_cpp.hpp` / `runepkg_util_cpp.cpp`)**:
   - **String Utilities**: Modern C++17 `trim`, `split`, `join`, `starts_with`, `ends_with`, `to_lower`, `to_upper`, and `format_bytes`.
   - **Atomic File I/O**: `write_string_to_file_atomic` writes payloads to temporary `.tmp` files before atomically renaming them.
   - **Subprocess Execution**: `exec_command` handles command execution with stdout/stderr capture and exit code extraction.
   - **Debian Control Parser**: `parse_deb_control_fields` parses key-value header blocks into structured maps.
   - **FFI Array Converters**: Safely converts between raw NULL-terminated C string arrays and `std::vector<std::string>`.

3. **`runepkg_resolver` (`runepkg_resolver.cpp`)**:
   - **Graph Harvester**: Parses 110,000+ Debian repository stanzas into compact binary graph representations (`runes_graph.bin`).
   - **Target Resolver**: Resolves runtime, build, and toolchain dependency trees natively using `runepkg::util` helpers.

4. **`runepkg_network` (`runepkg_network.cpp`)**:
   - **Parallel Downloader**: Multi-threaded CURL pool for parallel list fetching and package downloads.
   - **Integrated Validation**: Verifies OpenPGP signatures on repository indices and validates SHA256 checksums on downloaded `.deb` payloads.

---
