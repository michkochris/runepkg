# runepkg Internals

This document describes the technical architecture and "science" behind **runepkg**. It is designed for developers and enthusiasts who want to understand how a lightweight, repository-aware package manager is built from the ground up in C.

---

## 1. Unified Configuration & PKGINFO Metadata Parsing

The most primitive yet essential part of **runepkg** is how it handles configuration and package metadata. Instead of using complex libraries or separate parsers for different tasks, **runepkg** utilizes a **"smart" multi-use function** that handles key-value pairs across the entire system.

### The Smart Multi-use Parser: `runepkg_util_get_config_value`

Located in `runepkg_util.c`, this function is the backbone of all data retrieval in **runepkg**. It is designed to be highly versatile by accepting a dynamic `separator` argument.

#### Versatility through Separators
The parser is agnostic to the file type; it only cares about the key it's looking for and how the value is delimited. This allows a single function to handle two primary use cases:

- **System Configuration (`=`):** It parses the main `runepkgconfig` file where paths and global settings are defined.
  - *Example:* `install_dir=~/runepkg_dir/install_dir`
- **Package Metadata (`:`):** It parses standard Debian `.deb` control files and repository metadata.
  - *Example:* `Package: runepkg` or `Version: 1.0.4`

This unified approach ensures that any improvements made to the core parsing logic (like tilde expansion or whitespace trimming) immediately benefit both system configuration and package management.

---

## 2. Hybrid Persistent Storage & The "Rune" Hash

**runepkg** implements a unique storage architecture that combines the simplicity of Arch Linux's subdirectory structure with the high-speed lookup performance found in binary formats like RPM.

### A. The Directory Layout (Arch-Style Simplicity)
Each installed package resides in its own directory within the `runepkg_db` path. 
Inside each directory, **runepkg** stores a specialized binary file: `pkginfo.bin`.

### B. The `pkginfo.bin` (RPM-Style Performance)
Instead of reparsing text files every time you query a package, **runepkg** serializes the `PkgInfo` structure into a custom binary format. All strings and the full file list are packed into a single binary blob for near-instant loading.

### C. The "Rune" Hash Table: Memory-Resident Speed
To handle thousands of packages instantly, **runepkg** utilizes an advanced, custom-built hash table using the **FNV-1a** algorithm. It features **prime-sized buckets** and **dynamic resizing** to maintain $O(1)$ lookup performance regardless of database size.

---

## 3. Defensive Programming & Memory Safety

As a project written in C, **runepkg** prioritizes memory safety.
-   **Zero-Wiping**: sensitive data is zeroed out before being released.
-   **Pointer Nulling**: pointers are set to `NULL` immediately after freeing.
-   **Secure Wrappers**: `runepkg_secure_malloc` includes strict allocation limits (256MB) and integer overflow protection.
-   **Bounds Checking**: Every string and path operation is validated to prevent overflows and traversal attacks.

---

## 4. The "Self-Completing" Binary & Disciplined Autocomplete

**runepkg** implements a high-performance **"Self-Completing Binary"** architecture, moving logic from sluggish shell scripts into the optimized C engine.

### A. Interleaved Command & Alias Awareness
The completion engine scans the entire `COMP_LINE` to infer context. It supports shorthand aliases (`up`, `ug`, `i`, `r`, `s`, `l`) and interleaved commands (e.g., `runepkg -v -i [TAB]`).

### B. Disciplined Path Navigation
To provide a smooth segment-by-segment navigation experience in the terminal, the engine employs two key techniques:
1.  **Strict Context Isolation**: When typing an absolute path (`/`) or relative path (`./`), the engine strictly switches to filesystem mode, completely ignoring the 40,000+ repository entries to prevent lags.
2.  **The Anti-Jumping Ritual**: When suggesting a directory, the engine provides both the directory name and a hidden "deep" version (e.g., `dir/` and `dir/.`). This forces Bash to see multiple possibilities, preventing it from incorrectly "jumping" to the end and adding a space, allowing the user to navigate one slash at a time.

---

## 5. Autonomous Installation & The Build Rituals

**runepkg** has evolved into a fully autonomous engine that handles the entire package lifecycle.

### A. Intelligent "Clandestine" Dependencies
When installing a `.deb`, the engine performs a "clandestine" search in local directories (`./debs/`, etc.) to satisfy dependencies before reaching for the network.

### B. Multi-Package Split Build Ritual
The new `buildpkg-split` command leverages a C++ orchestration layer to:
1.  **Auto-Unearth**: If the source isn't local, it automatically fetches it from the repository.
2.  **Metadata Discovery**: If pointed at a directory, it digs into `debian/control` and `debian/changelog` to identify the package.
3.  **Dependency Alchemy**: It parses `Build-Depends` and performs a pre-build ritual to warn the user of missing headers/libraries before starting the forge.
4.  **Surgical Splitting**: It parses `.install` files to split a single source build into multiple binary fragments (`bin`, `dev`, `doc`) based on official Debian standards.

### C. Native Build Fallback
If standard tools like `debhelper` are missing, **runepkg** attempts a **Native Rune Build** using its internal C++ logic, allowing for compilation in minimal environments.

---

## 6. Advanced Networking & The C++/C FFI Layer

The **Foreign Function Interface (FFI)** allows **runepkg** to bridge pure C performance with C++ networking power.

### A. High-Speed Repository Updates
The `runepkg update` routine fetches multiple `Packages.gz` and `Sources.gz` files in parallel using `std::future`. Decompression and indexing happen on-the-fly using `zlib`.

### B. Search & Discovery Tiering
- **Tier 1 (Binary search)**: $O(\log n)$ search over sorted binary indices.
- **Tier 2 (Mmap retrieval)**: Memory-mapped access to flat-file caches for instant metadata retrieval.
- **Tier 3 (Local Priority)**: Commands like `build` and `fetch` automatically prioritize local `./sources/` and `./debs/` folders, keeping the user's workspace tidy without external scripts.

By combining hardened C plumbing with an intelligent C++ FFI layer, **runepkg** provides a high-level user experience with "old-school" surgical control.
