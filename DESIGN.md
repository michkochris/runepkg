# runepkg Design & Architecture

This document details the technical architecture and logic behind **runepkg**. It is intended for developers and enthusiasts who want to understand how a lightweight, repository-aware package manager is constructed from the ground up using strict **C89 (ANSI C)** and C++.

---

## 1. Portability: The C89 Standard Foundation

A core design goal of **runepkg** is absolute portability. To achieve this, the entire core C engine was refactored to adhere to the strict **ISO C90 (C89)** standard.

### The Portability Layer: `runepkg_portable.h`
Since C89 lacks modern features like `bool` or fixed-width integers, **runepkg** uses a dedicated portability header to bridge the gap:
- **Feature Detection**: Automatically detects compiler versions and provides standard `stdbool.h` or `stdint.h` if available; otherwise, it provides custom fallback definitions.
- **Structural Enforcement**: Adhering to C89 forces a "declaration-at-top" pattern, which improves code readability and prevents variable shadowing in deep logic.
- **musl libc Compatibility**: By avoiding GNU-specific extensions and modern C shorthands, **runepkg** is natively compatible with minimal libraries like **musl libc**, making it a perfect candidate for static linking in recovery environments.

---

## 2. Interaction: The "Self-Completing" Binary

To provide a modern user experience without the lag of complex shell scripts, **runepkg** implements a high-performance completion engine directly in the C core.

### Consolidated Binary Index
The engine uses a consolidated binary autocomplete index (`runepkg_autocomplete.bin`) located in the database directory. This index:
- **Unified Pool**: Aggregates installed packages, local `.deb` files, build directories, and repository names into a single searchable binary blob.
- **$O(\log n)$ Performance**: Uses memory-mapped (`mmap`) binary search to provide near-instant results even with 50,000+ entries.
- **Context-Aware Inference**: Scans the command line (`COMP_LINE`) to infer whether the user expects a package name, a file path, or a command switch.

### Path Navigation & Context Isolation
To maintain speed and prevent "jumping" (where Bash adds a space too early), the engine employs the **Anti-Jumping Ritual**: it suggests both a directory name and a hidden "deep" version (e.g., `dir/` and `dir/.`). This forces the shell to complete only up to the next slash, enabling segment-by-segment navigation.

---

## 3. Acquisition: The C++ FFI Networking Layer

**runepkg** uses a **Foreign Function Interface (FFI)** to bridge the pure C core with modern C++ networking.

### Parallel Repository Synchronization
The `runepkg update` routine fetches multiple `Packages.gz` and `Sources.gz` files in parallel using `std::future` and a thread-safe task pool. This architecture ensures rock-solid stability during massive transactions, such as full desktop environment upgrades. Decompression and binary indexing occur on-the-fly via `zlib`.

### Tiered Discovery
Acquisition is prioritized to keep the workspace tidy:
- **Tier 1: Local Priority**: Automatically checks local `./sources/` and `./debs/` folders before attempting a network download.
- **Tier 2: Remote Fetching**: Handles secure downloads via `libcurl` only when local "runes" are not found.

---

## 4. Processing: "Clandestine" Resolution & Building

### "Clandestine" Dependency Resolution
When a `.deb` is targeted, the engine performs a recursive "spider" search for sibling packages in the same directory. This prevents unnecessary network hits by identifying that a required dependency is already sitting on the user's disk alongside the main package.

### Intelligent Build Orchestration
The `buildpkg-split` command leverages the FFI layer to forge packages:
1. **Dependency Alchemy**: Parses `Build-Depends` to warn users of missing headers before a build begins.
2. **Native Build Fallback**: If standard tools like `debhelper` are absent, **runepkg** uses its internal logic to handle binary fragment splitting and FHS directory creation, keeping the tool independent of the standard Debian toolchain.

---

## 5. Security: Defense-in-Depth Architecture

**runepkg** implements several hardened layers of defense:

### Cryptographic Trust (GPG)
Integrated **GPG signature verification** provides a cryptographic audit path for all installations:
- **Detached Signatures**: Automatically looks for `.sig` files alongside `.deb` packages.
- **Silent Verification**: Executes `gpg --verify` in a silent subprocess, reporting only thematic success (`[gpg_sig] Passed`) or failure messages.
- **Tamper Block**: If verification fails, the installation is immediately aborted to prevent the execution of corrupted or malicious "runes."

### Hardened Plumbing (`runepkg_defensive.c`)
- **Secure Memory**: Features `runepkg_secure_malloc` with zero-wiping and strict 256MB allocation limits to prevent DoS attacks.
- **Path Validation**: Strictly validates all file paths to block traversal attacks (`/../`) and unauthorized access outside the system install root.
- **Integrity Checks**: Standard MD5 checksum verification is integrated into the installation sequence and available as a manual audit command (`runepkg -m`).

---

## 6. Persistence: The "Rune" Hash Table

For in-memory lookups, **runepkg** uses a custom hash table based on the **FNV-1a** algorithm. It features prime-sized buckets and dynamic resizing, ensuring $O(1)$ lookup performance regardless of the database size. Each installed package occupies its own directory, keeping metadata organized and human-readable.
