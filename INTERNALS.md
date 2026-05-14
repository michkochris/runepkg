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

### Key Features of the Parser

1.  **Smart Tilde Expansion:** The parser automatically detects `~/` at the start of a value and expands it to the user's `$HOME` directory using `getenv("HOME")`. This allows configuration files to be portable across different user accounts without hardcoding paths.
2.  **Whitespace Robustness:** It aggressively trims whitespace around both keys and values. This makes the configuration files "human-friendly"—users can add spaces for readability without breaking the parser.
3.  **Comment & Empty Line Handling:** It intelligently ignores empty lines and lines starting with `#`, allowing for well-documented configuration files and metadata.
4.  **Memory Management:** The function dynamically allocates exactly enough memory for the retrieved value, returning a clean string that the caller can use and eventually free.

### Code Illustration: Dual Use

The elegance of this design is best seen in the source code where the same utility is used for vastly different contexts:

```c
/* Loading System Configuration (from runepkg_config.c) */
g_install_dir = runepkg_util_get_config_value(config_path, "install_dir", '=');

/* Parsing Package Metadata (from runepkg_pack.c) */
pkg_info->version = runepkg_util_get_config_value(control_path, "Version", ':');
```

By keeping the core configuration primitive and centralized, **runepkg** maintains its "old-school" lightweight nature while providing modern conveniences like dynamic path resolution and robust metadata handling.

---

## 2. Hybrid Persistent Storage & The "Rune" Hash

**runepkg** implements a unique storage architecture that combines the simplicity of Arch Linux's subdirectory structure with the high-speed lookup performance found in binary formats like RPM.

### A. The Directory Layout (Arch-Style Simplicity)
Each installed package resides in its own directory within the `runepkg_db` path. This makes the database human-browsable and easy to repair manually if needed.
- `runepkg_db/bash-5.2-1/`
- `runepkg_db/coreutils-9.1-2/`

Inside each directory, **runepkg** stores a specialized binary file: `pkginfo.bin`.

### B. The `pkginfo.bin` (RPM-Style Performance)
Instead of reparsing text files every time you query a package, **runepkg** serializes the `PkgInfo` structure into a custom binary format.
1.  **Serialization**: All strings (Name, Version, Description, etc.) and the full file list are packed into a single binary blob.
2.  **Fast Loading**: When **runepkg** starts, it doesn't need to parse metadata; it performs a direct binary read of the pre-calculated lengths and strings, drastically reducing I/O overhead.

### C. The "Rune" Hash Table: Memory-Resident Speed
To handle thousands of packages instantly, **runepkg** utilizes an advanced, custom-built hash table.

#### 1. FNV-1a Hashing Algorithm
We use the **Fowler-Noll-Vo (FNV-1a)** hash algorithm. It is chosen for its extreme simplicity and its ability to distribute string keys with very low collision rates, which is perfect for package names.

#### 2. Advanced Performance Features
-   **Prime-Sized Buckets**: The table size is always a **prime number**. This mathematically minimizes clustering and collisions compared to power-of-two sizes. The system includes a built-in prime finder to dynamically calculate the next optimal size.
-   **Dynamic Resizing**: The table automatically grows or shrinks based on the **Load Factor**.
    -   **Grow**: When the table is >75% full, it doubles in size to maintain $O(1)$ lookup time.
    -   **Shrink**: If many packages are removed and the load falls below 10%, the table shrinks to conserve memory.
-   **Deep Copy Architecture**: Adding a package to the hash table involves a "deep copy" of all metadata strings. This ensures the table owns its data and remains stable even if the temporary parsing buffers are freed.

### D. Verbose Statistics & Observability
When running with `-v`, **runepkg** provides internal "science" reports on the storage and hash health:
```text
[VERBOSE] Initializing runepkg hash table...
[VERBOSE] Hash Table Stats: Size=1031, Count=42, Load=0.04
[VERBOSE] Resizing hash table from 503 to 1031 buckets
```

This hybrid approach ensures that **runepkg** stays true to its "Runar Linux" roots—simple enough to understand, but fast enough to outperform standard text-based package managers.

---

## 3. Defensive Programming & Memory Safety

As a project written in C, **runepkg** prioritizes memory safety and security to prevent common vulnerabilities like buffer overflows, memory leaks, and "use-after-free" errors. We implement a "Defensive Programming" layer that hardens every memory operation.

### A. The "Free, Terminate, & Null" Strategy
One of the most common issues in C is accessing memory after it has been freed. **runepkg** mitigates this by using the `runepkg_secure_free` utility (and its variations like `runepkg_util_free_and_null`).

-   **Zero-Wiping**: For sensitive data, the memory is explicitly zeroed out (`memset`) before being released.
-   **Pointer Nulling**: Every free operation immediately sets the caller's pointer to `NULL`. This ensures that any subsequent accidental access results in a predictable segfault rather than undefined behavior or a security exploit.

### B. Secure Wrappers: `runepkg_secure_malloc`
We avoid using standard `malloc` and `calloc` directly. Instead, we use secure wrappers that include:
1.  **Strict Limits**: All allocations are checked against `RUNEPKG_MAX_ALLOC_SIZE` (256MB) to prevent "zip bomb" style memory exhaustion attacks.
2.  **Integer Overflow Protection**: During `calloc` operations, we perform pre-allocation checks to ensure `count * size` does not wrap around the `SIZE_MAX` limit.
3.  **Automatic Initialization**: `runepkg_secure_malloc` automatically zeroes out the allocated block, preventing the use of uninitialized "junk" data from previous processes.

### C. Bounds Checking & Validation
Every string operation is hardened with size validation:
-   **`runepkg_secure_strcpy`**: Unlike `strncpy`, which can leave strings unterminated, our secure copy always guarantees null-termination and validates the destination buffer size.
-   **Path Validation**: We include checks for path traversal (`..`) and enforce maximum path lengths (`RUNEPKG_MAX_PATH_LEN`) to protect the system filesystem during package extraction.

### D. Memory Debugging & Observability
By enabling `RUNEPKG_DEBUG_MEMORY`, the system tracks every allocation in real-time. This allows us to generate detailed statistics on total memory usage and detect leaks before they reach a production environment:
```text
[DEBUG] Allocated 4096 bytes at 0x55d1a2 (total: 125440 bytes)
[DEBUG] Freeing 0x55d1a2 (size: 4096)
```

By making defensive programming a "first-class citizen," **runepkg** provides a stable and secure foundation that is often missing in lightweight C applications.

---

## 4. The "Self-Completing" Binary & Context-Aware Autocomplete

One of the most technically challenging aspects of **runepkg** was moving beyond standard, sluggish Bash completion scripts. Instead, we implemented a high-performance **"Self-Completing Binary"** architecture.

### A. The Core Philosophy: Binary Intelligence
Traditional package managers rely on external shell scripts to parse lists for tab-completion, which can be slow and brittle. **runepkg** handles its own completion logic internally. When you hit `TAB`, Bash invokes `runepkg` itself in a special mode. The binary then detects the completion context and provides results instantly.

### B. Interleaved Command Awareness
The completion engine is designed to handle **interleaved commands**—a style favored for its flexibility. It doesn't just complete the next word; it scans the entire command line (via `COMP_LINE`) to infer the user's intent.
- If you type `runepkg -v -i [TAB]`, it knows you are in an "install" context and suggests `.deb` files and repository packages.
- If you type `runepkg -r [TAB]`, it switches to "remove" context and suggests already installed packages.

### C. Repository-Awareness & Speed
Once `runepkg update` is run, the system becomes aware of thousands of remote packages. To keep lookups instantaneous, we avoid text-parsing during completion:
1.  **Binary Mmap Indexing**: Local and repository package lists are stored in optimized binary files (`.bin`).
2.  **Memory Mapping (`mmap`)**: The completion engine uses `mmap` to map these binary indices directly into memory. This allows for lightning-fast **binary search** over tens of thousands of package names with near-zero I/O overhead.
3.  **Fast Prefix Matching**: The engine performs prefix-based binary searches to find all matching candidates in logarithmic time $O(\log n)$.

### D. Smart Context Switching
The engine intelligently switches its search target based on the command:
- **`install`**: Searches for local `.deb` files recursively and remote repository packages.
- **`remove/status/list`**: Searches the local installed package database.
- **`source`**: Searches for source packages in the repository index.

This specialized architecture makes **runepkg** one of the most responsive package managers to use in a terminal environment, proving that "old-school" tools can provide a premium modern experience.

---

## 5. Parallel Installation & Context-Aware Dependencies

The installation engine in **runepkg** is designed for speed and reliability, utilizing modern multi-core performance while maintaining "old-school" simplicity in its dependency handling.

### A. High-Performance Parallelism
Unlike traditional single-threaded installers, **runepkg** leverages a multi-threaded file operation engine.
- **Dynamic Threading**: The system uses `sysconf(_SC_NPROCESSORS_ONLN)` to detect available CPU cores and dynamically calculates the optimal thread count for the current machine.
- **Concurrent I/O**: File extraction and copying are handled in parallel using `pthread`. This drastically reduces the wall-clock time required for large packages with thousands of small files.

### B. Context-Aware Dependency Resolution ("Clandestine" Mode)
A standout feature of **runepkg** is its intelligent sibling detection. When you install a local `.deb` file that has dependencies, the engine performs a "clandestine" search in the same directory as the target file.
- **Local Priority**: If the required dependency `.deb` is present in the same folder, **runepkg** will automatically install it first.
- **Network Bypass**: This avoids unnecessary network traffic and repository lookups when you have a complete set of offline packages.
- **Version Intelligence**: The engine parses filenames to prefer dependency versions that match the origin package's requirements, ensuring compatibility without manual intervention.

### C. The "Safety Net" & Atomic-Style Metadata
To prevent a corrupted database if a command is interrupted (e.g., a power failure or `Ctrl+C`), **runepkg** implements a strict execution order:
1. **Isolated Extraction**: Packages are first extracted into a temporary `control_dir` workspace.
2. **File Deployment**: Files are moved to the final `install_dir`.
3. **Database Finalization**: The package metadata is only serialized to the persistent `runepkg_db` **after** all files are successfully verified in their final locations.
4. **Automated Cleanup**: The `runepkg_pack_cleanup_extraction_workspace` routine ensures that no temporary artifacts or "half-unpacked" directories are left littering the filesystem, regardless of whether the installation succeeded or failed.

### D. Versatile Installation Sources
**runepkg** provides multiple "piping" styles for power users:
- **Batch Processing**: Use `runepkg --install @file.list` to process a manifest.
- **Standard Input**: Pipe lists directly into the binary with `ls *.deb | runepkg --install -`.
- **Interleaved Commands**: Mix installs and queries in a single line: `runepkg -i pkg1.deb -s pkg1 -i pkg2.deb`.

This combination of parallelism, intelligent context awareness, and safety guarantees makes **runepkg** both a powerful developer tool and a reliable system component.

---

## 6. Advanced Networking & The C++/C FFI Layer

To bridge the gap between low-level performance and high-level functionality, **runepkg** utilizes a sophisticated **Foreign Function Interface (FFI)**. While the core "plumbing" is written in pure C, the "porcelain" (networking and repository logic) is implemented in C++ to leverage modern features like parallelization and robust standard libraries.

### A. The "Hybrid Engine" Philosophy
The decision to use a C++/C FFI allows **runepkg** to remain lightweight and portable while providing features that usually require heavy dependencies.
- **C Layer**: Handles file operations, hashing, and database management for maximum speed and minimal footprint.
- **C++ Layer**: Orchestrates complex networking tasks via `libcurl`, manages parallel downloads with `std::async`, and handles complex string processing for metadata indexing.

### B. High-Speed Repository Updates
The `runepkg update` routine is engineered for concurrency. Unlike sequential managers, it treats every repository component as an independent task.
1.  **Parallel Fetching**: Using `std::future` and `std::async`, the engine fetches multiple `Packages.gz` and `Sources.gz` files simultaneously.
2.  **On-the-Fly Decompression**: Downloaded lists are decompressed using `zlib` and processed into the three-tier storage system instantly.
3.  **The Version Map**: A transient `std::unordered_map` is used to aggregate the latest versions across all repositories, allowing the system to identify upgradable packages in a single pass.

### C. Three-Tier Repository Metadata Storage
To maintain the project's "speed-first" philosophy, repository data is stored in a structured, searchable format that avoids the overhead of a SQL database:
- **Tier 1 (Binary Index)**: `repo_index.bin` stores a sorted list of `(PackageName, FileID, Offset)` entries. This enables $O(\log n)$ binary searches for any package in the repository.
- **Tier 2 (Flat-File Cache)**: The raw decompressed `Packages` files are kept as the "source of truth."
- **Tier 3 (Direct Offsets)**: The binary index points directly to the byte offset in the flat-file cache where a package's metadata begins. This allows for near-instant retrieval of full package stanzas (Dependencies, Descriptions, etc.).

### D. Parallel Package Prefetching
When performing an `upgrade` or a `source` download, **runepkg** doesn't wait for one file to finish before starting the next.
- **Batch Downloading**: In "upgrade" mode, all required `.deb` files are pre-fetched in parallel into the `download_dir`. This ensures that even if a network connection is lost midway, the installer already has all the necessary artifacts to proceed safely.
- **Source Package Parallelism**: For source packages (which often consist of multiple `.dsc`, `.orig.tar.gz`, and `.diff.gz` files), the engine triggers simultaneous downloads for every component, drastically reducing the total wait time.
- **Recursive Source Dependencies**: The `source-depends` command extends this by recursively parsing `Build-Depends` and pre-fetching the entire source dependency tree. This is essential for bootstrapping environments where you need to build a chain of related software from source.

### E. Modern Observability
The FFI layer includes a color-coded logging system that provides real-time feedback on networking states:
```text
[runepkg] Starting parallel repository update...
  -> Fetching: kali-rolling/main/binary-amd64
  -> Fetching: kali-rolling/contrib/binary-amd64
[upgradable] bash: 5.2.15 -> 5.2.21
[success] Downloaded 3 files to ~/runepkg_dir/download_dir
```

By leveraging C++ where it shines most, **runepkg** provides a high-level user experience without sacrificing its "old-school" C core performance.
