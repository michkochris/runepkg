# runepkg Internals

This document details the technical architecture and logic behind **runepkg**. It is intended for developers and enthusiasts who want to understand how a lightweight, repository-aware package manager is constructed from the ground up in C and C++.

---

## 1. Bootstrapping: Unified Configuration Parsing

The foundation of **runepkg** is its approach to configuration and metadata. Rather than relying on heavy external libraries, **runepkg** uses a versatile, internal parser.

### The Multi-use Parser: `runepkg_util_get_config_value`
Located in `runepkg_util.c`, this function is the backbone of data retrieval. It is designed to be agnostic to the file type by accepting a dynamic `separator` argument, allowing it to handle different formats seamlessly:

-   **System Configuration (`=`):** Parses the `runepkgconfig` file for global settings and paths (e.g., `install_dir=...`).
-   **Package Metadata (`:`):** Parses Debian `.deb` control files and repository indices (e.g., `Package: ...`).

This unified logic ensures that system-wide improvements, such as tilde expansion or whitespace trimming, are applied consistently across both configuration and package management.
w
---

## 2. Interaction: The "Self-Completing" Binary

To provide a modern user experience without the lag of complex shell scripts, **runepkg** implements a high-performance completion engine directly in the C binary.

### Context-Aware Autocomplete
The completion engine scans the command line (`COMP_LINE`) to infer context. It supports:
-   **Command Aliases**: Seamlessly handles shorthand like `up`, `i`, and `s`.
-   **Interleaved Commands**: Correctly suggests completions even when flags and commands are mixed (e.g., `runepkg -v -i [TAB]`).

### Path Navigation & Context Isolation
To maintain speed when navigating large repositories (40,000+ entries), the engine employs two key techniques:
1.  **Context Isolation**: When it detects a path (`/` or `./`), it switches strictly to filesystem mode, ignoring the repository index to prevent delays.
2.  **The Anti-Jumping Ritual**: To prevent Bash from incorrectly adding a space after a directory name, the engine suggests both the directory name and a hidden "deep" version (e.g., `dir/` and `dir/.`). This forces the shell to wait for further user input, allowing for one-slash-at-a-time navigation.

---

## 3. Acquisition: The C++ FFI Networking Layer

**runepkg** uses a **Foreign Function Interface (FFI)** to bridge the performance of C with the modern networking capabilities of C++.

### Parallel Repository Synchronization
The `runepkg update` routine fetches multiple `Packages.gz` and `Sources.gz` files in parallel using `std::future`. Decompression and indexing occur on-the-fly via `zlib`, significantly reducing the time required to sync with remote repositories.

### Tiered Discovery
Acquisition is prioritized to keep the workspace tidy:
-   **Tier 1: Local Priority**: Commands like `build` and `fetch` automatically check local `./sources/` and `./debs/` folders before attempting a network download.
-   **Tier 2: Remote Fetching**: If not found locally, the C++ layer handles secure downloads via `libcurl`.

---

## 4. Processing: Parsing & "Clandestine" Resolution

Once metadata is acquired, the engine must resolve how to proceed with installation or building.

### "Clandestine" Dependency Resolution
When a `.deb` is targeted for installation, the engine performs a local search for sibling packages in the same directory. This "clandestine" resolution allows users to satisfy dependencies with local files before the tool attempts to reach out to the network.

### Intelligent Build Orchestration
The `buildpkg-split` command leverages the C++ layer to orchestrate complex tasks:
1.  **Metadata Discovery**: It probes `debian/control` and `debian/changelog` to identify package requirements.
2.  **Dependency Alchemy**: It parses `Build-Depends` to warn the user of missing headers or libraries before the build begins.
3.  **Native Build Fallback**: If standard tools like `debhelper` are absent, **runepkg** uses its internal C++ logic to bridge the gap. It automatically handles typical helper tasks such as splitting binary fragments, cleaning up metadata variables, and ensuring proper FHS directory structures—all without requiring `dpkg` or external scripts.

---

## 5. Persistence: Hybrid Storage & The "Rune" Hash

For long-term storage and fast retrieval, **runepkg** combines filesystem simplicity with binary performance.

### The Directory Layout
Each installed package occupies its own directory within the `runepkg_db`. This Arch-style approach makes manual inspection and repair straightforward.

### Binary Serialization (`pkginfo.bin`)
To avoid the overhead of reparsing text files, **runepkg** serializes package data into `pkginfo.bin`. This binary blob includes all strings and the full file list, allowing for near-instant loading into memory.

### The "Rune" Hash Table
For in-memory lookups, **runepkg** uses a custom hash table based on the **FNV-1a** algorithm. It features prime-sized buckets and dynamic resizing, ensuring $O(1)$ lookup performance regardless of how many thousands of packages are indexed.

---

## 6. Security: Defensive Architecture

As a C-based project, **runepkg** implements several layers of defense:

-   **Memory Safety**: Includes `runepkg_secure_malloc` with strict allocation limits (256MB) and integer overflow protection.
-   **Zero-Wiping**: Sensitive data is zeroed out in memory before being freed.
-   **Pointer Discipline**: Pointers are immediately nulled after freeing to prevent use-after-free bugs.
-   **Bounds Validation**: Every string and path operation is strictly validated to prevent buffer overflows and path traversal attacks.
