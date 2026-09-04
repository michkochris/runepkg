# runepkg Architectural Design

## 1. Executive Vision: Debian as a Universal Supply Chain

`runepkg` is founded on the philosophy that the Debian source archive is not just a distribution, but a **universal supply chain** of raw building blocks (Runes). Traditionally, cross-compiling Debian packages requires heavy tooling (`pbuilder`, `sbuild`) and is strictly coupled to the `glibc` runtime. 

`runepkg` decouples the source archive from the distribution harness. It allows developers to "unearth" unmodified `.dsc` source packages and "forge" them into isolated, highly-optimized `.deb` packages for any target (e.g., `x86_64-musl-static`) with zero host contamination.

---

## 2. Dual-Engine Architecture (C89 Core vs. C++17 Forge)

The system is split into two distinct layers to balance portability and modern features:

### Core Mode (ISO C89/C90)
*   **Purpose:** Bootstrapping, recovery media, and initramfs operations.
*   **Design:** Zero-dependency, small footprint (~500 KB). Compiles on virtually any toolchain.
*   **Scope:** Local package management (`install`, `remove`, `unpack`, `status`), MD5 verification, and binary index lookups.
*   **Files:** `runepkg_cli.c`, `runepkg_handle.c`, `runepkg_config.c`, `runepkg_util.c`, etc.

### Forge/Extended Mode (C++17 FFI)
*   **Purpose:** High-level orchestration, network operations, and cross-compilation.
*   **Design:** Leverages modern C++ for parallel processing and complex graph algorithms.
*   **Scope:** Parallel repository updates, dependency resolution, 4-stage toolchain bootstrapping, and the Matrix Engine.
*   **Files:** `runepkg_network.cpp`, `runepkg_resolver.cpp`, `runepkg_bootstrap.cpp`, `runepkg_matrix.cpp`.

---

## 3. Binary Storage Model & Zero-Copy mmap Indexing

To avoid the I/O bottlenecks of scanning large text files (like `/var/lib/dpkg/status` or repository `Packages` files), `runepkg` uses a **binary-first** approach.

### FNV-1a Hash Bucketing
The system serializes package metadata into fixed-layout binary structures (`pkginfo.bin`, `matrix_rules.bin`). Package lookups are performed using 32-bit FNV-1a hashes mapped to prime-sized buckets, achieving $O(1)$ complexity.

### Memory-Mapped Autocomplete
The completion engine uses `runepkg_autocomplete.bin`, a memory-mapped sorted index. This allows the shell to perform instant, zero-allocation prefix searches across 100k+ packages without ever reading the whole file into RAM.

---

## 4. The Matrix Engine (Declarative Runes)

A key architectural breakthrough is the **Matrix Engine**, which eliminates hardcoded package workarounds.

*   **Declarative Rules:** Package behaviors (flags, build systems, shell hooks) are defined in human-readable `.txt` files in `target-rules/`.
*   **JIT Rule Compilation:** During `runepkg update`, these text files are compiled into a binary `matrix_rules.bin`.
*   **Shell Hooks:** The engine supports `pre_configure`, `build_override`, and `post_install` hooks. These hooks use dynamic variable substitution (e.g., `${TARGET_TRIPLE}`, `${DESTDIR}`) to handle profile-specific patching without C++ changes.

---

## 5. The Forge: Hermetic Toolchain Construction

`runepkg` implements a structurally correct **4-stage destructible bootstrap** process to ensure toolchain purity:

1.  **Stage 1A (Binutils):** Build the cross-linker and assembler.
2.  **Stage 1B (GCC Core):** Build a freestanding C-only bootstrap compiler.
3.  **Stage 1C (Headers & Libc):** Install sanitized Linux headers and the target C library (`musl` or `glibc`) into the isolated sysroot.
4.  **Stage 1D (GCC Final):** Build the full C/C++ compiler with target-native runtime libraries.

**Isolation:** The entire forge lives in `/mnt/runepkg/{profile}/`, keeping the host system completely clean.

---

## 6. Dependency Graph Pruning & Resolution

Standard Debian dependency trees are often "bloated" for embedded targets (pulling in documentation, icons, etc.).

*   **Pruned Harvester:** The resolver identifies strictly Tier-3 C/C++ runtime libraries and Tier-0/2 host build tools.
*   **Topological Forge Path:** It generates a minimal, bottom-up build order.
*   **ASCII Tree Visualization:** The `depends` command provides a recursive view of this minimal tree, helping developers visualize the "Forge footprint" before building.

---

## 7. Configuration & Multi-Purpose Registry

The configuration system (`runepkg_config.c`) is designed for extreme portability and flexibility:

*   **Dynamic Registry:** `runepkg` can be "installed" into multiple directories. It tracks its own root location via a configuration registry, allowing multiple independent instances of the manager to coexist on the same system.
*   **Active Target State:** The manager maintains an `active_target.conf` which persists the current profile context (triplet, sysroot paths, etc.) across shell sessions.
*   **Path Portability:** All paths (base, build, download, debs) are fully configurable, making the tool as comfortable in a standard Linux root as it is in a constrained Termux environment.

---

## 8. Comparison with the Ecosystem

| Dimension | **runepkg** | Buildroot | Yocto / BitBake |
| :--- | :--- | :--- | :--- |
| **Philosophy** | Source Forge from Upstream | Rootfs Generator | Meta-Distro Framework |
| **Maintenance** | Zero (Uses Debian Upstream) | High (Manual `.mk` files) | High (Layer management) |
| **Toolchain** | 4-Stage Destructible | Internal or Wrapper | Layered Recipes |
| **Overhead** | Low-Level ($O(1)$ Binary) | Moderate | Extreme (RAM/Disk heavy) |
| **Isolation** | Absolute (Sysroot/Prefix) | High | High |

---

## 9. Finite State Machine (FSM) & Transactional Engine Architecture

`runepkg` incorporates a 6-tier transactional lifecycle to guarantee atomic state transitions, signal-safe rollbacks, and complete execution auditing across both C89 core and C++ extended build modes.

### Dual-Tier State Engine
*   **Low-Level C89 Core (`runepkg_state.c/.h`):** Provides a portable, zero-dependency state dispatch engine. States move strictly through `IDLE` $\rightarrow$ `PREPARING` $\rightarrow$ `FETCHING` $\rightarrow$ `VALIDATING` $\rightarrow$ `STAGING` $\rightarrow$ `COMMITTING` $\rightarrow$ `CLEANUP`.
*   **High-Level C++ RAII Guard (`runepkg_guard.hpp/.cpp`):** Wraps C transaction contexts in an RAII `RunepkgTransactionGuard`. If an exception or early return unwinds the stack prior to `.commit()`, the destructor triggers `step_rollback()` and writes post-mortem logs automatically.

### Async-Signal-Safe Self-Pipe (`sig_pipe`)
To avoid executing non-async-safe functions (`malloc`, file I/O, `unlink`) inside signal handlers, `runepkg` installs a non-blocking `sig_pipe`. On `SIGINT`, `SIGTERM`, `SIGSEGV`, or `SIGQUIT`, the signal handler performs a 1-byte write over `sig_pipe[1]`. State checkpoints poll `sig_pipe[0]` non-blocking and trigger `step_rollback()` safely in user thread context.

### Process-Global Concurrency Locking
`step_prepare()` acquires an exclusive `fcntl` write-lock on `/srv/lib/runepkg_dir/log_dir/transaction.lock` (`ctx->lock_fd`), preventing parallel `runepkg` processes from racing on database stanzas or file mutations.

### Pre-Mutation Backups & Journaling
Before any file is overwritten or unlinked, `perform_file_install()` creates a pre-mutation backup in `ctx->staging_dir/backups/`. Every file operation registers a journal entry (`runepkg_journal_record_create`, `runepkg_journal_record_overwrite`, `runepkg_journal_record_delete`) and writes human-readable `[JOURNAL]` lines directly into the active `transaction-YYYYMMDD-HHMMSS.log`.

### Automated Startup Crash Recovery
On startup (`runepkg_init()`), `runepkg_fsm_recover_orphaned_transactions()` scans `g_log_dir` for abandoned `staging_<pid>` workspaces left by crashed processes. It verifies PID liveness via `kill(pid, 0)` and automatically purges orphaned staging files. Operators can audit logs using `runepkg transactions`, `runepkg transactions list`, and `runepkg transactions inspect <timestamp>`.

---

## 10. Future Roadmap & Next Steps

*   **Standard Rules Library:** Expand `target-rules/` to cover standard utilities (`coreutils`, `openssl`, `sed`, `grep`).
*   **Automated Test Suite:** Implement end-to-end integration tests (Host $\rightarrow$ Stage 1 $\rightarrow$ Forge).
*   **GPG Verification:** Flesh out the `verify` command for cryptographic validation of downloaded Runes.
*   **Extended Metadata:** Add support for architecture-specific patch injection within the Matrix rules.
