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

**runepkg** is a lightning-fast, high-performance hybrid C89/C++ package manager and toolchain forge engineered specifically for the **Debian ecosystem**. Unlike traditional tools bound to a specific distribution, **runepkg** treats the entire Debian package universe as a universal supply chain—enabling developers to unearth, compile, and deploy `.deb` software across modern workstations and constrained embedded environments alike.

- **Low-Level Core (Pure C89/C90, ~400–530 KB):** A minimalist, memory-safe `.deb` package manager with capabilities comparable to `busybox dpkg`. Designed for embedded targets, recovery media, and memory-constrained environments where ISO C90 compliance is paramount.
    - **ANSI C Compliance**: Strictly follows C89/C90 standards for maximum portability across legacy and modern compilers.
    - **Minimalist Footprint**: Ultra-compact binary size (**417 KB** dynamic / **536 KB** static) optimized for memory-constrained hardware.
    - **Zero-Dependency Resilience**: Operates as a standalone ELF, immune to host-level library corruption or missing dynamic loaders.
    - **musl-libc Optimization**: Deeply integrated with musl for predictable, high-performance static linking in embedded systems.
    - **Security-First Core**: Built on a hardened memory model with `secure_malloc`, zero-wiping, and path traversal protection.

- **High-Level Version**: The extended C++ FFI suite transforms **runepkg** into a lightning fast **Debian** repository package manager but with a sophisticated toolchain and **Debian** compatible source package builder suited for rapid embedded systems deployment.
    - **Compact Extended Footprint**: Professional-grade binary size (**2.5 MB** 100% static) including full networking, compression, and C++ runtime.
    - **Parallel Networking**: High-speed multi-threaded repository synchronization and package downloading using `libcurl`.
    - **Enhanced C++ Security Foundation**: Hardened perimeter featuring automatic OpenPGP repository `InRelease` signature verification, streaming SHA256/SHA512 hash validation, path sanitization/jail traversal defense (`..`), POSIX `rlimit` extraction bounds, sandboxed privilege dropping (`_apt`), and 100% exception-safe C FFI bridge. Detailed technical background can be found in [CPP.md](./CPP.md).
    - **Debian Source Building**: High-speed workflow for unearthing and forging source packages. `runepkg source <pkg>` downloads and sets up package, `runepkg build <pkg>` triggers a build, and `runepkg buildpkg-split <pkg>` splits package into multiple debian compatible `.deb` fragments (e.g., bin, dev, doc, ext...).
    - **Cross-Toolchain Engine**: (`bootstrap <target> [pkgs...]`) builds a destructible/disposable cross compile toolchain that only adds build-dependencies of target <pkg's>. It cleanly cross compiles **Debian** compatible .deb's and runtime dependencies of the target <pkg's>. Perfect for a rapid deployment of .deb's into an embedded **Debian Ecosystem**. Keeping and creating a small footprint on an embedded device, not so easily done in the **Debian World.**

**Lightning-Fast Binary Autocompletion**: Both versions of **runepkg** leverage a high-performance binary completion engine for rapid shell integration. The **Low-Level Core** provides instant suggestions for command-line options and installed packages, while the **High-Level Version** delivers predictive discovery for over **70,000+ Debian Repository Packages** via sophisticated drop-down menus. This includes resolution for **Debian Source Packages** when using `runepkg source <pkg>`, maximizing productivity directly from the shell.

---

## Key Features

-   **Native Debian Ecosystem Compatibility:** Full fidelity with Debian binary `.deb` archives (control metadata, data payloads, md5sums, trigger scripts) and upstream repository metadata (`Packages.gz`, `Sources.gz`).
-   **Binary-Serialized Metadata (`pkginfo.bin`):** Replaces flat-file parsing bottlenecks with structured binary records organized in hierarchical `package-version` directories, enabling $O(1)$ lookup speeds.
-   **Deterministic Cross-Toolchain Forge (`bootstrap`):** Automatically provisions isolated, disposable Stage-1 cross-compilers to build Debian-compatible binaries and runtime dependencies for specific target profiles (e.g., `x86_64-musl-static`, `x86_64-musl-shared`, `aarch64-linux-gnu`) without polluting the host environment (`build-toolchain` option reserved for future development).
-   **Granular Sub-Package Splitting (`buildpkg-split`):** Compiles Debian source packages directly and segments the resulting tree into discrete Debian fragments (`bin`, `dev`, `doc`, `lib`) or compiles only a targeted sub-package.
-   **Hardened Memory Safety:** Built on a security-first memory model (`secure_malloc`) featuring automated zero-wiping, buffer protection, and strict path traversal validation.

---

## When to Use runepkg

**Use runepkg if you:**
- Need to compile Debian specific source packages for non-native architectures (ARM, RISC-V, x86_64-musl)
- Are building embedded Linux systems and want full Debian specific package ecosystem access
- Require minimal, isolated binaries with controlled dependencies for production deployment into Debian specific Linux system
- Are maintaining custom Linux distributions based on specific Debian packages

---

## Architecture & System Support

**runepkg** is built with a dual-tier architecture to suit different environments:

- **Minimal Core (Pure C89):** The heart of the tool is strictly compliant with the ANSI C standard. This version is designed for minimal, low-level installations on memory-constrained embedded systems.
- **Extended (C++ FFI):** An optional Extended C++ FFI layer provides high-speed parallel networking, repository synchronization, and a native Debian source package builder.
- **Compiler Agnostic:** Swap compilers during the build process (e.g., `CC=tcc`, `gcc`, `clang`, `pcc`, or `zig cc`) to suit your specific target environment or historical toolchain.
- **BusyBox Compatibility:** When using with **BusyBox**, it may take a custom build and more manual configuration... specifically, ensuring utility symlinks are enabled so the plumbing for `ar`, `tar`, and `gzip` resolves correctly.


```text
[ CLI / Shell Layer ]
│
├───> [ Minimal C89 Core ] ────────> Local .deb Installation, Unpacking, Removal, FNV-1a DB
│
└───> [ Extended C++ FFI ] ────────> Parallel Downloader (libcurl)
                                     ├─> Dependency Resolver (runes_graph.bin)
                                     ├─> Host Ingestion & Pruning (runes_host.bin)
                                     └─> Cross-Compile Forge & Source Builder (.dsc)
```

| Feature                | Minimal Core (C89)                     | Extended Suite (C++ FFI)                                     |
| :--------------------- | :------------------------------------- | :----------------------------------------------------------- |
| **Primary Role**       | Local `.deb` Package Management        | Repository Sync, Dependency Resolution & Forging             |
| **Language Standard**  | ANSI C (C89 / C90)                     | ISO C89 + C++17 (FFI Bridge)                                 |
| **Binary Footprint**   | ~417 KB dynamic / ~536 KB static       | ~2.5 MB (100% static)                                        |
| **Dependencies**       | libc only (`musl` or `glibc`)          | `libcurl`, `zlib` (or statically bundled)                    |
| **Packaging Commands** | `-i`, `-r`, `-l`, `-s`, `-L`, `-S`, `-u`, `-m`, `-b` | `update`, `upgrade`, `source`, `build`, `buildpkg-split`, `build-toolchain` |
| **Target Use Cases**   | Embedded targets, Linux From Scratch, initramfs       | Workstations, build farms, cross-compilation forges          |

---

## The runepkg Difference

**runepkg** was conceived with the idea of simplicity and raw performance. Long before the first line of code was written, the core concept was to fuse the clean, hierarchical **pkgname-version subdirectory structure** of Arch Linux with the high-speed **binary metadata (`pkginfo.bin`)** principles of RPM—creating a lightning-fast `.deb` pkg management solution engineered specifically to handle the scale and complexity of the **Debian ecosystem** with a lightweight engine.

This vision evolves package management by replacing the sequential, text-heavy bottlenecks of traditional tools with a performance-first hybrid architecture:

- **Unified High-Speed Metadata & Autocomplete:** Instead of parsing large flat files, **runepkg** utilizes **binary-serialized metadata** (`pkginfo.bin`) stored within hierarchical **package-version directories**. This same high-performance storage solution powers a **lightning-fast binary autocomplete engine** for the CLI—providing near-instant suggestions for command options, installed package names/versions, and repository packages via **memory-mapped indices** (`mmap`).
- **Efficient Memory-Safe Lookups:** A custom **FNV-1a hash table** utilizing a **unified `PkgInfo` structure** as the single source of truth across all operations. The engine features dynamic **load-based prime resizing** (growing and shrinking to optimize collisions) and a strict memory model that ensures all package data is safely cleared and nulled after use.
- **Parallel Performance Suite:** Features a dual-engine concurrency model: a C++ multithreaded pool for high-speed **parallel networking** (mapped to hardware concurrency) and a C-based `pthread` installer for **parallel file extraction**. Both engines avoid hardcoded limits, dynamically scaling to your CPU's core count to maximize I/O throughput.
- **Intelligent Local-First Resolution:** A smart logic layer that automatically detects and resolves dependencies using sibling `.deb` files found in the local directory or download cache before reaching for the network.
- **Security-Hardened Plumbing:** The core engine is built on a **security-first memory model** (`secure_malloc`) featuring automatic zero-wiping and strict **path traversal protection**. These defenses were refined through AI-driven security auditing to mitigate memory corruption risks and unauthorized filesystem access.
- **Comprehensive musl libc Support:** Provides full compatibility for both the **Minimal C Core** and the **Extended C++ FFI Suite** when targeting **musl libc**.
- **Self-Contained Static Binaries:** Enables the creation of 100% statically-linked binaries that carry their own runtime—ensuring high-performance networking and source-building capabilities function on any Linux distribution with zero shared library dependencies. Detailed technical background on this systems programming milestone can be found in [HOLY_GRAIL.md](./HOLY_GRAIL.md) 🏆
- **Enhanced Cryptographic Trust & Security Perimeter:** A comprehensive security layer supporting OpenPGP repository `InRelease` signature verification against system keyrings (`/etc/apt/trusted.gpg.d/`), streaming SHA256/SHA512 hash validation, POSIX `rlimit` extraction bounds (preventing zip bombs), sandboxed worker privilege dropping (`_apt`), and detached GPG signatures (`.sig`). Complete C++ architecture specifications can be found in [CPP.md](./CPP.md).
- **Native Debian Source Building**: An integrated C++ pipeline that streamlines the fetching and compilation of Debian source packages, the produced .deb's are fully compatible with the Debian Ecosystem. Runepkg directly produces "Raw Debian Runes" from single package builds to multi-package splitting and allowing for the optional building of a sub-package. This is all done independently without the overhead of traditional Debian distribution build tools.
- **Disposable Cross-Compilation Toolchain:** The `bootstrap <target> [pkgs...]` command bootstraps a destructible toolchain to cleanly cross-compile isolated, **Debian** compatible .deb's and runtime dependencies for the target packages. This enables rapid deployment of software into embedded **Debian Ecosystems** without contaminating the host environment (`build-toolchain` command line option is reserved for future development).
- **Proven Stability at Scale:** Rigorously stress-tested by successfully installing full desktop environments like **GNOME** and **XFCE**. The engine demonstrated extreme stability while resolving, downloading, and extracting thousands of recursive dependencies simultaneously.

### 1. Hierarchical Storage & Binary Caching
Traditional Debian tooling relies on scanning monolithic, text-heavy status files. **runepkg** implements isolated `package-version` subdirectories paired with binary-serialized metadata (`pkginfo.bin`). Lookups utilize dynamic, prime-resizing FNV-1a hash tables for deterministic high-performance query speeds.

### 2. Streamlined Debian Source Forging
Traditional Debian source compilation requires extensive distribution tooling (`dpkg-buildpackage`, `debhelper`, `fakeroot`, `quilt`). **runepkg** directly unpacks `.dsc` stanzas and patches, normalizes Autotools timestamp cascades, and compiles the source tree directly using target-profile toolchains.

### 3. Isolated Target Sysroot & Target <PKG'S>
When cross-compiling for embedded targets (such as `x86_64-musl-static`), **runepkg** evaluates dependencies independently:
-   **Host Tools:** Automatically satisfied via the host system to avoid rebuilding the compiler.
-   **Target Libraries:** Automatically cross-compiled and staged into the isolated profile sysroot (`/mnt/runepkg/<profile>/sysroot`).
-   **Target Runes:** Linked, stripped, assembled into `.deb` archives, and exported directly to `runepkg_debs/`.

🎩 Technical details on the genius architectural design choices can be found in [DESIGN.md](./DESIGN.md).

### 4: Workflows for the Enthusiast
For the hobbyist and system builder, **runepkg** excels at the "low-level" process of package creation. You don't need complex build harnesses or distribution-specific policies to forge a `.deb`.

- **Direct Build**: `runepkg build <dir> [output.deb]` builds a `.deb` instantly. It intelligently detects if the directory is an eligible "debian pkg based directory structure."
- **Manual Staging:** Use your own custom bash scripts to fetch and compile source code, then use `make install DESTDIR=/path/to/staging` to prepare your "debian pkg based directory structure" exactly how you want it.
- **Direct Forging:** Once your staging area is ready, a single command—`runepkg -b /path/to/staging`—wraps your work into a high-performance `.deb` package instantly.
- **FHS Initialization**: The engine can bootstrap a full Filesystem Hierarchy Standard (FHS) skeleton in seconds. This triggers automatically if **runepkg** detects you are installing to an alternate root (non-`/`)—creating necessary directories like `/usr/bin`, `/etc`, and `/lib` to prepare a fresh environment for software deployment.
- **Freedom from Bloat:** This workflow is ideal for **Linux From Scratch (LFS)** enthusiasts who want to maintain their own binary packages without the overhead of `debhelper`, `dpkg-dev`, or mandatory system-wide policies.

### 5: Surgical Precision
Unlike `apt-get source`, which may pull in massive build-dependency trees, `runepkg source` downloads only the "raw runes" (upstream source + Debian patches). This allows for direct inspection and modification of the `rules` build script or `control` metadata. **runepkg** remains independent of standard Debian tools; its **Native Build Fallback** can forge packages even when `debhelper` or `dpkg-dev` are missing.

### 6: The "Hacker" Build Loop
**runepkg** enables a streamlined "fetch-edit-build" workflow:
- **Fetch**: Use `runepkg source` to unearth a source package into your build directory.
- **Dependency Management**: Use `source-depends` to download a source package along with its runtime-dependencies, or `source-build-depends` to automate the retrieval of all binary packages required for a successful build.
- **Edit**: Modify `debian/rules`, `control`, or the source code itself.
- **Build**: Use `runepkg build <pkg|dir|.dsc>` to trigger a build. **runepkg** attempts the build without the strict dependency gatekeeping of mainstream tools. It automatically handles package names (auto-fetching), extracted directories, or `.dsc` files.
- **Multi-Package Builds**: `runepkg buildpkg-split <pkg|dir|.dsc> [target]` builds a source package and splits it into multiple `.deb` fragments (e.g., `bin`, `dev`, `doc`). You can optionally specify a `[target]` package name to forge only that specific fragment from the split list.

---

## Installation & Build Instructions

### Prerequisites

#### 1. A modular dependency bootstrap script is included for Debian, Ubuntu, and Kali Linux systems:

```bash
# Install all components (Core, musl cross-tools, C++ FFI)
./debian-depends.sh --all

# Or install modular profiles:
./debian-depends.sh --core      # Standard C compiler and basic build utilities
./debian-depends.sh --musl      # musl-libc cross-compilation toolchain
./debian-depends.sh --extended  # C++ FFI headers, libcurl, and zlib
```

#### 2. Runtime Dependencies
For package extraction, compression, and archival operations, **runepkg** relies on standard low-level utilities. It is engineered to be compatible with both full-featured GNU tools and lightweight alternatives:

- **Standard GNU/Linux:** `ar` (from `binutils`), `tar`, and compression suites (`gzip`, `xz`).
- **BusyBox Compatibility:** **runepkg** is fully operational in resource-constrained environments using **BusyBox**.

> [!IMPORTANT]
> **BusyBox Setup:** When using with BusyBox, ensure that your build includes the required applets (`ar`, `tar`, `gzip`, `xz`). You must either enable the **"install symlinks"** option during the BusyBox build process or manually configure symlinks in your `$PATH` so the plumbing for these utilities resolves correctly for the **runepkg** engine.

## Compiler Options

**Custom Toolchain:**
*Efficient clean, build, and install using your preferred compiler.*
```bash
make clean && sudo make uninstall
CC=clang CXX=clang++ make all && sudo make install
```

### 1. Standard glibc Build (Host Default)

```bash
# Clean local artifacts and uninstall existing binary
make clean && sudo make uninstall

# Build and install Minimal Core (C only)
make runepkg && sudo make install

# Build and install Full Extended Suite (C + C++ FFI)
make all && sudo make install
```

### 2. musl-libc Builds (Self-Contained / Embedded)

**Minimal Core (Static):**
```bash
make clean && sudo make uninstall
make MUSL=1 LDFLAGS="-static" runepkg && sudo make install
```

**Full Extended Suite (100% Self-Contained Static ELF):**
```bash
make clean-all && sudo make uninstall
make musl-all && sudo make install
```

> [!TIP]
> `make musl-all` fetches isolated musl toolchain and builds all required dependencies (`libcurl`, `zlib`) from source, outputting a static binary that runs across any Linux distribution with zero shared library dependencies. Detailed technical background can be found in [HOLY_GRAIL.md](./HOLY_GRAIL.md).

### 3. uClibc-NG Build Options (Future)

*(Support for uClibc-NG is planned for future minimal environment deployments.)*

---

## Post-Installation

### Build as a .deb
To create a `.deb` package of **runepkg** itself:

```bash
chmod +x make_runepkg_deb.sh
./make_runepkg_deb.sh
```

### Lightning Fast Autocomplete
To enable the advanced binary-driven autocompletion engine, run command in terminal and add this to your `~/.bashrc` for persistence...

To enable predictive binary autocompletion across CLI flags, installed packages, and 70,000+ repository packages, add the following to your `~/.bashrc`:

```bash
complete -o nospace -C runepkg runepkg
```


---

## Configuration & Workspace Layout

Configuration is managed via `/etc/runepkg/runepkgconfig` or overridden using the `RUNEPKG_CONFIG_PATH` environment variable.

> [!CAUTION]
> **Power User Responsibility:** If your `install_dir` is set to `/` (system-wide), **runepkg** will remove files from your host system during a removal (`-r`) or upgrade. Always verify your active configuration using `--print-config-file` before executing such commands. Ensuring that the target root is correct is the user's responsibility.

### Diagnostic Commands
```bash
runepkg --print-config        # Display active paths, architectures, and repository settings
runepkg --print-config-file   # Display the active configuration file path
```
---

## Inside the runepkg_db

### Database & Artifact Hierarchy (`/var/lib/runepkg_dir/`)

For maximum performance, **runepkg** utilizes a high-speed binary database stored in the `runepkg_db` directory (typically `/var/lib/runepkg_dir/runepkg_db`).

- **`runepkg_autocomplete.bin`**: A memory-mapped binary index powering lightning-fast shell autocompletion for over 70,000+ packages.
- **`runes_graph.bin`**: A high-performance serialized dependency graph. This allows the resolver to calculate complex build trees in milliseconds.
- **`repo_index.bin` / `repo_src_index.bin`**: Optimized binary metadata indices for rapid repository searches and source package lookups.
- **`repo_url_mapping.txt`**: Maps packages and source components to their respective repository base URLs for efficient downloading.

```text
/var/lib/runepkg_dir/
├── runepkg_db/
│   ├── pkginfo/                  # Hierarchical installed package metadata
│   ├── runes_graph.bin           # Serialized repository dependency graph
│   ├── runes_host.bin            # Synced host dpkg status snapshot
│   ├── repo_index.bin            # Binary package metadata cache
│   ├── repo_src_index.bin        # Source package metadata cache
│   └── runepkg_autocomplete.bin  # Memory-mapped autocompletion index
├── build_dir/                    # Source workspaces and unpacked packages
├── download_dir/                 # Cached .deb downloads
├── runepkg_debs/                 # Staging area for forged .deb packages
└── install_dir/                  # Package installation root (defaults to /)
```

### System Registry & State
To ensure stability across reboots and support multiple environments, the core registry and toolchain state are stored independently of the package database in `/etc/runepkg/`:

- **`config_registry.txt`**: A persistent ledger that tracks the locations of active configuration files. This allows **runepkg** to reliably find its bearings even when custom config paths are used.
- **`active_target.conf`**: This file tracks your currently active cross-compilation profile. Keeping it in the system configuration path ensures it is properly reloaded and persists even when the ephemeral build directories are cleaned.

---

## Usage:

```text
runepkg (fast, efficient .deb package manager & toolchain forge)

Usage:
  runepkg <COMMAND> [OPTIONS] [ARGUMENTS]

Core Package Management (Local/Low-Level):
  sync                                    Synchronize host package database with system state.
  -i, --install <deb|pkg>...              Install .deb files or repository packages.
      --install -                         Read .deb paths from standard input.
      --install @file                     Read .deb paths from a list file.
  -r, --remove <package-name>             Remove an installed package.
      --remove -                          Read package names from standard input.
  -l, --list [pattern]                    List installed packages (supports wildcards).
  -s, --status <package-name>             Display detailed metadata for an installed package.
  -L, --list-files <package-name>         List all files owned by an installed package.
  -S, --search <file-path>                Search installed packages for a specific file.
  -u, --unpack <path-to-package.deb>      Unpack a .deb payload into build_dir.
  -m, --md5check <package-name>           Verify file integrity using MD5 checksums.
  -b, --build [dir] [output.deb]          Forge a .deb from a compliant directory tree.
  -v, --verbose                           Enable verbose diagnostic logging.
  -d, --debug                             Enable low-level developer trace logging.
  -f, --force                             Force operations despite dependency warnings.
      --version                           Display version and build information.
  -h, --help                              Display this help manual.

Advanced Repository Management (Network/FFI):
  update                                  Synchronize repository indices and update dependency graphs.
  upgrade                                 Download and install available upgrades.
  search <pkg|pattern>                    Search repositories for packages or descriptions.
                                          (Use "quotes" to search multi-word queries).
  source <pkg>                            Download and extract source package files into build_dir.
  source-depends <pkg>                    Download source package and its runtime dependencies.
  source-build-depends <pkg>              Download source package and its build dependencies.

  info <pkg>                              Display remote repository metadata for a package.
  build <pkg|path>                        Build a source package by name or path to .dsc.
  buildpkg-split <pkg|path> [target]      Build and split a source package (optional: target sub-package).
                                          (Automatically retrieves from repository if name is provided).

  download-only <pkg>                     Download a .deb without dependencies.
  download-depends <pkg>                  Download a .deb along with its binary dependencies.
  download-build-depends <pkg>            Download binary .debs required to compile a source package.

Maintenance & Diagnostics:
      --print-config                      Print all active path and repository settings.
      --print-config-file                 Show the path to the runepkgconfig file in use.
      --print-pkglist-file                Show paths to the autocomplete index files.
      --print-autopool                    Print the contents of the consolidated autocomplete pool.
      --rebuild-autocomplete              Rebuild the local package name index.
  transactions [list|inspect <ts|log>]   Audit FSM execution logs, inspect journals, or recover crashed runs.
                                          Accepts a timestamp or absolute path to a .log file.
                                          Guarantees atomic state and system integrity by tracking 
                                          transactional boundaries from start to finish.

Cross-Compilation:
  bootstrap <target> [pkgs...]            Construct isolated cross-compilation toolchain, 
                                          and forge target packages.
  build-toolchain                         Reserved command line option for future development.
  depends <pkg>                           Recursive ASCII tree visualization of target depends.

Experimental/Future:
  verify <pkg>                            Cryptographic package verification using GPG.

Note: Commands can be interleaved, e.g., 'runepkg -v -i pkg1.deb -s pkg2 -i pkg3.deb'
Note: C++ features are enabled based on your build target (`make all`).
```

<p align="left">
  <img src="./runepkg/runepkg_logo.svg" width="400" alt="runepkg Logo">
  <br>
</p>

## Philosophy & Background
---
*Built with ❤️ for the GNU/Linux community. **runepkg** treats packages as building blocks rather than strict system policies, allowing power users to build and compile .deb packages exactly as they wish.*

Developed from years of experience with Custom Cross Linux From Scratch (LFS), **runepkg** views ancient `.deb` packages as "runes"—valuable historical artifacts. This tool empowers you to unearth and run legacy software from Debian archives safely in modern environments.

## Testing & Validation
---
Comprehensive testing documentation, integration test suites, and fuzzing campaign methodologies can be found in [TESTING.md](./TESTING.md) 🧪

## Performance Benchmarks
---
Detailed benchmarks quantifying package lookup latency, repository update throughput, and memory footprint compared to `dpkg`/`apt` can be found in [BENCHMARKS.md](./BENCHMARKS.md) ⚡

## Technical Audit
---
An independent architectural assessment of the codebase's structural soundness and systems programming standards can be found in [COPILOT.md](./COPILOT.md) 🤖

## Security Audit
---
A comprehensive security assessment was conducted by **GitHub Copilot Security Review** on **runepkg v1.0.4** (Status: 🟢 **LOW Risk / Production-Hardened**). The audit evaluated ANSI C core memory safety, C++ FFI exception boundary isolation, OpenPGP signature verification, path traversal sanitization, POSIX `rlimit` extraction caps, and sandboxed privilege dropping (`_apt`). Full security threat modeling and audit details can be found in [SEC.md](./SEC.md) 🛡️

## Contact
---
🍆 [michkochris@gmail.com](mailto:michkochris@gmail.com) | [runepkg@gmail.com](mailto:runepkg@gmail.com)