<h1>
  <img src="./runepkg/runepkg_icon.svg" width="30" style="vertical-align: middle;">
  runepkg
</h1>

---

[![libc: musl](https://img.shields.io/badge/Libc-musl-blue.svg)](https://musl.libc.org/)
[![Standard: C89 / C90](https://img.shields.io/badge/Standard-C89%20%2F%20C90-blue.svg)](https://en.wikipedia.org/wiki/ANSI_C)
[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://github.com/michkochris/runepkg)
[![FFI: C++](https://img.shields.io/badge/FFI-C%2B%2B-blue.svg)](https://isocpp.org/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

**runepkg** is a high-performance hybrid C89/C++ package manager engineered specifically for the **Debian ecosystem**. Treating the entire Debian universe as a universal supply chain, **runepkg** enables developers to unearth and deploy `.deb` software from any Debian repository or historical archive with extreme speed and minimal overhead.

Designed with a dual-tier architecture, **runepkg** functions as an ultra-compact C89 package manager for resource-constrained embedded targets, while offering an extended C++ suite for parallel multi-threaded repository synchronization and high-speed dependency resolution that outperforms `apt`.

---

> [!TIP]
> **Production Stability & Refinement (v1.0.4+):** **runepkg** is declared **Stable** and battle-tested on rolling Linux distributions. Core engine refinements fully resolve topological dependency ordering, maintainer script parameter passing (`$1`/`$2`), and multi-pass virtual/dummy package resolution. It seamlessly handles full system upgrades and massive environment deployments (like **XFCE** with 270+ packages) in a single pass.
>
> **dpkg "Quantum Entanglement" (Native):** **runepkg** maintains native, real-time compatibility with **dpkg**. Installing or removing packages directly updates `/var/lib/dpkg/status` stanzas and generates `/var/lib/dpkg/info/*.list` files with full POSIX and Debian policy compliance, guaranteeing that `dpkg` system state remains 100% harmonized.
>
> **apt Interoperability (Conditional):** **runepkg** offers high conditional interoperability alongside **apt**. Because `apt` maintains its own isolated higher-level state cache, coexisting workflows are supported, though occasional conflict resolution (e.g., `apt --fix-broken install`) may be required depending on repository package diversions and cache states.

---

## Is runepkg Right for Me?

| Scenario | runepkg | apt | Recommendation |
| :--- | :---: | :---: | :--- |
| **Standard Debian/Ubuntu Desktop** | ✅ | ✅ | Use **runepkg** for speed or **apt** |
| **Native `dpkg` State Integration** | ✅ | ✅ | Guaranteed via `runepkg/dpkg` layer |
| **Coexistence Alongside `apt`** | ✅ | ⚠️ | High interoperability (conditional) |
| **Embedded Targets / Initramfs / CLFS** | ✅ | ❌ | Use **runepkg** (C89 minimal footprint) |
| **Deterministic High-Speed Deployment** | ✅ | ⚠️ | Use **runepkg** (parallel multi-threaded) |

---

## Dual-Tier Architecture Overview

**runepkg** is built with a dual-tier architecture to serve both low-level embedded hardware and high-performance server/workstation targets:

```text
[ CLI / Shell Layer ]
│
├───> [ Minimal C89 Core ] ────────> Local .deb Installation, Unpacking, Removal, FNV-1a DB
│
└───> [ Extended C++ FFI ] ────────> Parallel Downloader (libcurl)
                                     ├─> Dependency Resolver (runes_graph.bin)
                                     └─> Host Ingestion & Pruning (runes_host.bin)
```

| Feature | Minimal Core (C89) | Extended Suite (C++ FFI) |
| :--- | :--- | :--- |
| **Primary Role** | Local `.deb` Package Management & Extraction | Repository Sync, Network Parallelism & Graph Resolution |
| **Language Standard** | ISO C89 / C90 (ANSI C) | ISO C89 + C++17 (FFI Bridge) |
| **Binary Size** | ~417 KB (dynamic) / ~536 KB (static) | ~2.5 MB (100% self-contained static ELF) |
| **Dependencies** | `libc` only (`musl` or `glibc`) | `libcurl`, `zlib` (or statically bundled) |
| **Core Commands** | `-i`, `-r`, `-l`, `-s`, `-L`, `-S`, `-u`, `-m`, `-b` | `update`, `upgrade`, `download-only`, `download-depends`, `search`, `info` |
| **Target Environments** | Embedded systems, initramfs, CLFS, recovery media | Workstations, build farms, cloud instances, chroots |

---

## Technical Innovations

- **Binary-Serialized Metadata (`pkginfo.bin`):** Replaces flat-file parsing bottlenecks with structured binary records organized in hierarchical `package-version` subdirectories, enabling $O(1)$ lookup speeds.
- **Unified FNV-1a Memory Engine:** Uses a custom FNV-1a hash table with dynamic prime resizing to store `PkgInfo` structures, ensuring minimal collision overhead and zero-wiped secure memory.
- **Dual-Engine Concurrency:** Combines a C++ thread pool for parallel HTTP repository downloads with a C `pthread` worker pool for parallel payload extraction.
- **Predictive Binary Autocompletion:** Mmap-backed binary autocompletion index (`runepkg_autocomplete.bin`) providing predictive shell suggestions across CLI flags, installed packages, and **70,000+ Debian repository packages**.
- **Security-Hardened Perimeter:** Features `secure_malloc` zero-wiping, path traversal jail protection (`..`), POSIX `rlimit` extraction bounds (preventing zip bombs), sandboxed worker privilege dropping (`_apt`), and OpenPGP `InRelease` signature verification.

---

## Installation & Build Instructions

### 1. Prerequisites

A modular dependency bootstrap script is included for Debian, Ubuntu, and Kali Linux systems:

```bash
# Install all components (Core, C++ FFI)
./debian-depends.sh --all

# Or install modular profiles:
./debian-depends.sh --core      # Standard C compiler and basic build utilities
./debian-depends.sh --extended  # C++ FFI headers, libcurl, and zlib
```

### 2. Standard Build Options

```bash
# Build and install Full Extended Suite (C + C++ FFI)
make clean && make all && sudo make install

# Build and install Minimal Core (C89 only)
make clean && make runepkg && sudo make install
```

### 3. musl-libc Builds (Self-Contained Static ELFs)

```bash
# Minimal Core (Static C89 binary)
make clean && make MUSL=1 LDFLAGS="-static" runepkg && sudo make install

# Full Extended Suite (100% Self-Contained Static ELF with bundled libcurl/zlib)
make clean-all && make musl-all && sudo make install
```

> [!TIP]
> `make musl-all` fetches an isolated musl toolchain and compiles `libcurl` and `zlib` from source, producing a single static binary that runs across any Linux distribution with zero shared library dependencies. See 🏆 [HOLY_GRAIL.md](./docs/HOLY_GRAIL.md) for details.

### 4. Custom Compiler Selection

```bash
CC=clang CXX=clang++ make all && sudo make install
```

---

## CLI Command Reference

### Package Operations
* `runepkg -i, --install <deb|pkg>`: Install local `.deb` files or repository packages.
* `runepkg -r, --remove <pkg>`: Remove an installed package.
* `runepkg -l, --list [pattern]`: List installed packages.
* `runepkg -s, --status <pkg>`: Display detailed status and metadata.
* `runepkg -L, --list-files <pkg>`: List all files owned by a package.
* `runepkg -S, --search <path>`: Find which installed package owns a file.
* `runepkg -u, --unpack <pkg.deb>`: Unpack `.deb` payload into `build_dir`.
* `runepkg -m, --md5check <pkg>`: Verify installed files against MD5 checksums.
* `runepkg -b, --build [dir] [out.deb]`: Build a `.deb` package from a compliant directory structure.

### Repository & Network Operations
* `runepkg update`: Synchronize repository indices and update binary graphs.
* `runepkg upgrade`: Download and install all available upgrades in a single pass.
* `runepkg search <pattern>`: Search repository packages by name or description.
* `runepkg info <pkg>`: View remote repository metadata.
* `runepkg download-only <pkg>`: Download a `.deb` package without installing.
* `runepkg download-depends <pkg>`: Download a `.deb` package and its binary dependencies.

### Maintenance & Shell Autocomplete
* `runepkg sync`: Synchronize local database with host `/var/lib/dpkg/status`.
* `runepkg transactions [list|inspect]`: Audit FSM execution logs and recover crashed runs.
* `runepkg --print-config`: Display active paths and settings.

To enable shell autocompletion across CLI flags and 70,000+ repository packages, add to `~/.bashrc`:
```bash
complete -o nospace -C runepkg runepkg
```

---

## Workspace Directory Hierarchy (`/var/lib/runepkg_dir/`)

```text
/var/lib/runepkg_dir/
├── runepkg_db/
│   ├── pkginfo/                  # Hierarchical installed package metadata
│   ├── runes_graph.bin           # Serialized repository dependency graph
│   ├── runes_host.bin            # Synced host dpkg status snapshot
│   ├── repo_index.bin            # Binary package metadata cache
│   └── runepkg_autocomplete.bin  # Memory-mapped autocompletion index
├── build_dir/                    # Source workspaces and unpacked packages
├── download_dir/                 # Cached .deb downloads
└── install_dir/                  # Package installation root (defaults to /)
```

---

## Documentation & References

- 🎩 [DESIGN.md](./docs/DESIGN.md): Detailed architectural design choices and performance engineering.
- 💻 [CPP.md](./docs/CPP.md): C++ FFI bridge specification and memory management.
- 🧪 [TESTING.md](./docs/TESTING.md): Integration test suites and fuzzing campaign methodologies.
- 🏆 [HOLY_GRAIL.md](./docs/HOLY_GRAIL.md): Statically-linked musl build guide.
- 🛡️ [COPILOT.md](./docs/COPILOT.md): Security review and architectural assessment.

---

## Philosophy & Contact

*Built with ❤️ for the GNU/Linux community. **runepkg** treats packages as building blocks rather than strict system policies, allowing power users to build and deploy `.deb` packages exactly as they wish.*

Developed from years of experience with Custom Cross Linux From Scratch (CLFS), **runepkg** views `.deb` packages as "runes"—valuable software building blocks. This tool empowers you to unearth and deploy software safely and efficiently.

<p align="left">
  <img src="./runepkg/runepkg_logo.svg" width="300" alt="runepkg Logo">
</p>

🍆 [michkochris@gmail.com](mailto:michkochris@gmail.com) | [runepkg@gmail.com](mailto:runepkg@gmail.com)
