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

**runepkg** is a lightning-fast, high-performance, hybrid C89/C++ package manager for the **Debian ecosystem**. It is designed to be versatile: serving as both a low-level **ISO (C90)** compliant tool for managing .deb's with similar functionality to `busybox dpkg` (perfect for managing .deb's in embedded systems. The **minimal version** comes in at just 400-500KB) The high-level **Extended C++ FFI Version** has similar functionality to `apt-get`. Featuring advanced parallel networking and **Debian** source pkg building.

**Lightning-Fast Binary Autocompletion**: Both versions of **runepkg** leverage a high-performance binary completion engine for rapid shell integration. The **Low-Level Core** provides instant suggestions for command-line options and installed packages, while the **High-Level Version** delivers predictive discovery for over **70,000+ Debian Repository Packages** via sophisticated drop-down menus. This includes resolution for **Debian Source Packages** when using `runepkg source <pkg>`, maximizing productivity directly from the shell.

- **Low-Level Core**: A minimalist, C89/C90 compliant engine engineered for size, compliance, and resilience in constrained environments.
    - **ANSI C Compliance**: Strictly follows C89/C90 standards for maximum portability across legacy and modern compilers.
    - **Minimalist Footprint**: Ultra-compact binary size (**417 KB** dynamic / **536 KB** static) optimized for memory-constrained hardware.
    - **Zero-Dependency Resilience**: Operates as a standalone ELF, immune to host-level library corruption or missing dynamic loaders.
    - **musl-libc Optimization**: Deeply integrated with musl for predictable, high-performance static linking in embedded systems.
    - **Security-First Core**: Built on a hardened memory model with `secure_malloc`, zero-wiping, and path traversal protection.

- **High-Level Version**: The extended C++ FFI suite transforms **runepkg** into a lightning fast **Debian** repository package manager but with a sophisticated toolchain and **Debian** compatible source package builder suited for rapid embedded systems deployment.
    - **Compact Extended Footprint**: Professional-grade binary size (**2.5 MB** 100% static) including full networking, compression, and C++ runtime.
    - **Parallel Networking**: High-speed multi-threaded repository synchronization and package downloading using `libcurl`.
    - **Debian Source Building**: High-speed workflow for unearthing and forging source packages. `runepkg source` downloads and sets up package, `runepkg build` triggers a build, and `runepkg buildpkg-split` splits package into multiple debian compatible `.deb` fragments (e.g., bin, dev, doc, ext...).
    - **Cross-Toolchain Engine**: (`build-toolchain --target={target-profile} <pkg's>`) builds a destructible/disposable cross compile toolchain that only adds build-dependencies of target <pkg's>. It cleanly cross compiles **Debian** compatible .deb's and runtime dependencies of the target <pkg's>. Perfect for a rapid deployment of .deb's into an embedded **Debian Ecosystem**. Keeping and creating a small footprint on an embedded device, not so easily done in the **Debian World.**


## Architecture & System Support
---

**runepkg** is built with a dual-tier architecture to suit different environments:

- **Minimal Core (Pure C89):** The heart of the tool is strictly compliant with the ANSI C standard. This version is designed for minimal, low-level installations on memory-constrained embedded systems.
- **Extended (C++ FFI):** An optional Extended C++ FFI layer provides high-speed parallel networking, repository synchronization, and a native Debian source package builder.
- **Compiler Agnostic:** Swap compilers during the build process (e.g., `CC=tcc`, `gcc`, `clang`, `pcc`, or `zig cc`) to suit your specific target environment or historical toolchain.
- **BusyBox Compatibility:** When using with **BusyBox**, it may take a custom build and more manual configuration... specifically, ensuring utility symlinks are enabled so the plumbing for `ar`, `tar`, and `gzip` resolves correctly.

## The runepkg Difference
---

**runepkg** was conceived with the idea of simplicity and raw performance. Long before the first line of code was written, the core concept was to fuse the clean, hierarchical **pkgname-version subdirectory structure** of Arch Linux with the high-speed **binary metadata (`pkginfo.bin`)** principles of RPM—creating a lightning-fast `.deb` pkg management solution engineered specifically to handle the scale and complexity of the **Debian ecosystem** with a lightweight engine. 

This vision evolves package management by replacing the sequential, text-heavy bottlenecks of traditional tools with a performance-first hybrid architecture:

- **Unified High-Speed Metadata & Autocomplete:** Instead of parsing large flat files, **runepkg** utilizes **binary-serialized metadata** (`pkginfo.bin`) stored within hierarchical **package-version directories**. This same high-performance storage solution powers a **lightning-fast binary autocomplete engine** for the CLI—providing near-instant suggestions for command options, installed package names/versions, and repository packages via **memory-mapped indices** (`mmap`).
- **Efficient Memory-Safe Lookups:** A custom **FNV-1a hash table** utilizing a **unified `PkgInfo` structure** as the single source of truth across all operations. The engine features dynamic **load-based prime resizing** (growing and shrinking to optimize collisions) and a strict memory model that ensures all package data is safely cleared and nulled after use.
- **Parallel Performance Suite:** Features a dual-engine concurrency model: a C++ multithreaded pool for high-speed **parallel networking** (mapped to hardware concurrency) and a C-based `pthread` installer for **parallel file extraction**. Both engines avoid hardcoded limits, dynamically scaling to your CPU's core count to maximize I/O throughput.
- **Intelligent Local-First Resolution:** A smart logic layer that automatically detects and resolves dependencies using sibling `.deb` files found in the local directory or download cache before reaching for the network.
- **Security-Hardened Plumbing:** The core engine is built on a **security-first memory model** (`secure_malloc`) featuring automatic zero-wiping and strict **path traversal protection**. These defenses were refined through AI-driven security auditing to mitigate memory corruption risks and unauthorized filesystem access.
- **Comprehensive musl libc Support:** Provides full compatibility for both the **Minimal C Core** and the **Extended C++ FFI Suite** when targeting **musl libc**.
- **Self-Contained Static Binaries:** Enables the creation of 100% statically-linked binaries that carry their own runtime—ensuring high-performance networking and source-building capabilities function on any Linux distribution with zero shared library dependencies.
- **Optional Cryptographic Trust:** A forward-thinking security layer that supports **detached GPG signatures** (`.sig` files). While standard Debian repositories sign at the index level, **runepkg** empowers users to verify individual "runes." This allows for the verification of custom-signed packages and provides a foundation for future secure-repository standards where every `.deb` is cryptographically immutable.
- **Native Debian Source Building**: An integrated C++ pipeline that streamlines the fetching and compilation of Debian source packages, the produced .deb's are fully compatible with the Debian Ecosystem. Runepkg directly produces "Raw Debian Runes" from single package builds to multi-package splitting and allowing for the optional building of a sub-package. This is all done independently without the overhead of traditional Debian distribution build tools.
- **Disposable Cross-Compilation Toolchain:** The `build-toolchain --target={profile} <pkg>` command bootstraps a destructible toolchain to cleanly cross-compile isolated, **Debian** compatible .deb's and runtime dependencies for the target packages. This enables rapid deployment of software into embedded **Debian Ecosystems** without contaminating the host environment.
- **Proven Stability at Scale:** Rigorously stress-tested by successfully installing full desktop environments like **GNOME** and **XFCE**. The engine demonstrated extreme stability while resolving, downloading, and extracting thousands of recursive dependencies simultaneously.

🎩 Technical details on the genius architectural design choices can be found in [DESIGN.md](./DESIGN.md).

## Key Features
---

### 1: Workflows for the Enthusiast
For the hobbyist and system builder, **runepkg** excels at the "low-level" process of package creation. You don't need complex build harnesses or distribution-specific policies to forge a `.deb`.

- **Direct Build**: `runepkg build <dir> [output.deb]` builds a `.deb` instantly. It intelligently detects if the directory is an eligible "debian pkg based directory structure."
- **Manual Staging:** Use your own custom bash scripts to fetch and compile source code, then use `make install DESTDIR=/path/to/staging` to prepare your "debian pkg based directory structure" exactly how you want it.
- **Direct Forging:** Once your staging area is ready, a single command—`runepkg -b /path/to/staging`—wraps your work into a high-performance `.deb` package instantly.
- **FHS Initialization**: The engine can bootstrap a full Filesystem Hierarchy Standard (FHS) skeleton in seconds. This triggers automatically if **runepkg** detects you are installing to an alternate root (non-`/`)—creating necessary directories like `/usr/bin`, `/etc`, and `/lib` to prepare a fresh environment for software deployment.
- **Freedom from Bloat:** This workflow is ideal for **Linux From Scratch (LFS)** enthusiasts who want to maintain their own binary packages without the overhead of `debhelper`, `dpkg-dev`, or mandatory system-wide policies.

### 2: Surgical Precision
Unlike `apt-get source`, which may pull in massive build-dependency trees, `runepkg source` downloads only the "raw runes" (upstream source + Debian patches). This allows for direct inspection and modification of the `rules` build script or `control` metadata. **runepkg** remains independent of standard Debian tools; its **Native Build Fallback** can forge packages even when `debhelper` or `dpkg-dev` are missing.

### 3: The "Hacker" Build Loop
**runepkg** enables a streamlined "fetch-edit-build" workflow:
- **Fetch**: Use `runepkg source` to unearth a source package into your build directory.
- **Dependency Management**: Use `source-depends` to download a source package along with its runtime-dependencies, or `source-build-depends` to automate the retrieval of all binary packages required for a successful build.
- **Edit**: Modify `debian/rules`, `control`, or the source code itself.
- **Build**: Use `runepkg build <pkg|dir|.dsc>` to trigger a build. **runepkg** attempts the build without the strict dependency gatekeeping of mainstream tools. It automatically handles package names (auto-fetching), extracted directories, or `.dsc` files.
- **Multi-Package Builds**: `runepkg buildpkg-split <pkg|dir|.dsc> [target]` builds a source package and splits it into multiple `.deb` fragments (e.g., `bin`, `dev`, `doc`). You can optionally specify a `[target]` package name to forge only that specific fragment from the split list.

## Installation
---

### Dependencies
Building **runepkg** is straightforward. We provide a modular script to unearth all necessary dependencies on Debian/Ubuntu/Kali systems.

#### 1. Automatic Dependency Installation
Use the provided script to gather the dependencies required for your specific build:

```bash
# Install everything (Complete Installation)
./debian-depends.sh --all

# OR install only what you need:
./debian-depends.sh --core      # Basic C build tools
./debian-depends.sh --musl      # musl-libc toolchain
./debian-depends.sh --extended  # C++ FFI & Networking
```

#### 2. Runtime Utilities
For package extraction, compression, and archival operations, **runepkg** relies on standard low-level utilities. It is engineered to be compatible with both full-featured GNU tools and lightweight alternatives:

- **Standard GNU/Linux:** `ar` (from `binutils`), `tar`, and compression suites (`gzip`, `xz`).
- **BusyBox Compatibility:** **runepkg** is fully operational in resource-constrained environments using **BusyBox**. 

> [!IMPORTANT]
> **BusyBox Setup:** When using with BusyBox, ensure that your build includes the required applets (`ar`, `tar`, `gzip`, `xz`). You must either enable the **"install symlinks"** option during the BusyBox build process or manually configure symlinks in your `$PATH` so the plumbing for these utilities resolves correctly for the **runepkg** engine.

## Configuration & Flexibility
---

**runepkg** is designed to be highly flexible and non-intrusive. Configuration is handled via `runepkgconfig`, typically installed to `/etc/runepkg/runepkgconfig`. It uses a cascading configuration system to manage paths and behavior:

1.  **System Default:** `/etc/runepkg/runepkgconfig`
2.  **User Override:** Set the `RUNEPKG_CONFIG_PATH` environment variable to point to a custom config file.

### Diagnostic Commands
Use these commands to inspect your current environment and ensure the engine is targeting the correct locations:
- `runepkg --print-config`: Prints all active path and repository settings.
- `runepkg --print-config-file`: Shows the absolute path to the configuration file currently in use.

### Relocatable Roots (Sandboxing)
One of the most powerful features of **runepkg** is the `install_dir` setting. By changing this path in your config, you can install `.deb` packages into any directory (e.g., a custom development root or a project-specific sandbox) without touching your host system.

- **Check Active Config:** Always verify your config with `runepkg --print-config-file` before performing operations in a sandbox.
- **Bootstrapping:** When installing to a non-root directory, **runepkg** automatically initializes a Filesystem Hierarchy Standard (FHS) skeleton in the target path.

> [!CAUTION]
> **Power User Responsibility:** If your `install_dir` is set to `/` (system-wide), **runepkg** will remove files from your host system during a removal (`-r`) or upgrade. Always verify your active configuration using `--print-config-file` before executing such commands. Ensuring that the target root is correct is the user's responsibility.

### Inside the runepkg_db
For maximum performance, **runepkg** utilizes a high-speed binary database stored in the `runepkg_db` directory (typically `/var/lib/runepkg_dir/runepkg_db`).

- **`runepkg_autocomplete.bin`**: A memory-mapped binary index powering lightning-fast shell autocompletion for over 70,000+ packages.
- **`runes_graph.bin`**: A high-performance serialized dependency graph. This allows the resolver to calculate complex build trees in milliseconds.
- **`repo_index.bin` / `repo_src_index.bin`**: Optimized binary metadata indices for rapid repository searches and source package lookups.
- **`repo_url_mapping.txt`**: Maps packages and source components to their respective repository base URLs for efficient downloading.

### System Registry & State
To ensure stability across reboots and support multiple environments, the core registry and toolchain state are stored independently of the package database:

- **`config_registry.txt`**: A persistent ledger (typically in `/etc/runepkg/`) that tracks the locations of active configuration files. This allows **runepkg** to reliably find its bearings even when custom config paths are used.
- **`active_target.conf`**: This file tracks your currently active cross-compilation profile. Keeping it in the system configuration path ensures it is properly reloaded and persists even when the ephemeral build directories are cleaned.

### Security & Package Removal
When using the `-r` or `--remove` command, **runepkg** deletes files based on the `install_dir` defined in your **active configuration**. **runepkg** operates with surgical precision within its defined root.

## Compiler Options

**Custom Toolchain:**
*Efficient clean, build, and install using your preferred compiler.*
```bash
make clean && sudo make uninstall
CC=clang CXX=clang++ make all && sudo make install
```

## Build Instructions
---

The following sections provide instructions for building **runepkg** against different C standard libraries.

### 1. Standard glibc (Host Default)
---
By default, **runepkg** links against the system's **glibc** and standard shared libraries. These builds are ideal for general use on distributions like Debian, Ubuntu, and Kali.

#### Fresh Install / Clean Rebuild Sequence
For developers and power users who prefer a **surgical** clean state before a new build, follow this sequence. This prevents overwriting existing binaries and ensures that no stale configurations or metadata persist between versions.

##### Surgical Purge (Recommended)
*Removing the old state entirely before forging the new one.*

```bash
# Clean local artifacts and remove the existing system binary
make clean && sudo make uninstall
# Purge persistent directories and system configuration if needed
sudo rm -rf /var/lib/runepkg_dir/ /etc/runepkg/
```

##### Fresh Build & Installation

**Minimal Core (C Only):**
```bash
make clean && make uninstall
make runepkg && sudo make install
```

**Extended Suite (Full C/C++):**
```bash
make clean && make uninstall
make all && sudo make install
```

### 2. musl libc Build Options
---
**runepkg** provides a comprehensive build suite for **musl libc**, offering both minimal and extended functionality in dynamic and static configurations.

#### Minimal Core (C Only)
Designed for low-level, local package management on memory-constrained or embedded systems.

- **Dynamic Build** (Smallest binary; requires `ld-musl-x86_64.so.1` on the target):

```bash
make clean && make uninstall
make MUSL=1 runepkg && sudo make install
```

> [!TIP]
> **Verification Note:** On glibc-based hosts (like Debian/Kali), standard `ldd` may report an "invalid ELF header" error for musl binaries. To correctly verify a dynamic musl build, use the musl loader itself: `/lib/ld-musl-x86_64.so.1 --list ./runepkg`.

- **Static Build** (Completely self-contained; zero runtime dependencies):

```bash
make clean && make uninstall
make MUSL=1 LDFLAGS="-static" runepkg && sudo make install
```

#### Extended Suite (C++ FFI)
Includes advanced parallel networking, repository synchronization, and the native source package builder.

- **Dynamic Build** (Requires musl-compiled `libcurl` and `zlib` on the target):

```bash
make clean && make uninstall
make MUSL=1 WITH_CPP=1 build && sudo make install
```

- **Full Static Build** (The "Invincible" ELF; bundles all dependencies including curl/zlib):

```bash
# Automated ritual: Forges a 100% self-contained Extended binary
make clean-all && sudo make uninstall
make musl-all && sudo make install
```

> [!TIP]
> The **Full Static Build** uses an automated script (`make musl-all`) to construct an isolated musl toolchain and compile all required dependencies from source, ensuring the resulting binary is 100% independent of the host system. Detailed technical background can be found in [HOLY_GRAIL.md](./HOLY_GRAIL.md).

### 3. uClibc-NG Build Options (Future)
---
*(Support for uClibc-NG is planned for future minimal environment deployments.)*

## Post-Installation
---

### Build as a .deb
To create a `.deb` package of **runepkg** itself:

```bash
chmod +x make_runepkg_deb.sh
./make_runepkg_deb.sh
```

### Lightning Fast Autocomplete
To enable the advanced binary-driven autocompletion engine, run command in terminal and add this to your `~/.bashrc` for persistence...

```bash
complete -o nospace -C runepkg runepkg
```

## Testing & Validation
---

*(Instructions for running the automated test suite and manual verification rituals will be documented here.)*

## Usage
---
For basic commands, see the help output below.

```bash
runepkg (fast efficient old-school .deb package manager)

Usage:
  runepkg <COMMAND> [OPTIONS] [ARGUMENTS]

Core Package Management (Local/Low-Level):
  -i, --install <deb|pkg>...              Install .deb files or repository packages.
      --install -                         Read .deb paths from stdin.
      --install @file                     Read .deb paths from a list file.
  -r, --remove <package-name>             Remove an installed package.
      --remove -                          Read package names from stdin.
  -l, --list [pattern]                    List installed packages (optionally matching pattern).
  -s, --status <package-name>             Show detailed info about an installed package.
  -L, --list-files <package-name>         List all files owned by an installed package.
  -S, --search <file-path>                Search installed packages for a specific file.
  -u, --unpack <path-to-package.deb>      Unpack a .deb into build_dir.
  -m, --md5check <package-name>           Verify MD5 checksums of an installed package.
  -b, --build [dir] [output.deb]          Build a .deb from a directory structure.
  -v, --verbose                           Enable verbose output (detailed logging).
  -d, --debug                             Enable debug output (developer traces).
  -f, --force                             Force install/upgrade despite missing dependencies.
      --version                           Print version and license information.
  -h, --help                              Display this help message.

Advanced Repository Management (Network/FFI):
  update                                  Sync metadata and check for upgradable packages.
  upgrade                                 Download and install all available upgrades.
  search <pkg|pattern>                    Search repositories for packages or patterns.
                                          (Use "quotes" to search for multiple words).
  source <pkg>                            Download source package files into build_dir.
  source-depends <pkg>                    Download source package and its runtime-dependencies.
  source-build-depends <pkg>              Download source package and its build-dependencies.

  info <pkg>                              Show repository information for a package.
  build <pkg|path>                        Build a source package by name or path to .dsc.
  buildpkg-split <pkg|path> [target]      Build and split a source package (optional: targeted sub-package).
                                          (Will auto-fetch from repo if name provided).

  download-only <pkg>                     Download a .deb to download_dir without dependencies.
  download-depends <pkg>                  Download a .deb and its binary dependencies.
  download-build-depends <pkg>            Download binary .debs required to build a source package.

Maintenance & Diagnostics:
      --print-config                      Print all active path and repository settings.
      --print-config-file                 Show the path to the runepkgconfig file in use.
      --print-pkglist-file                Show paths to the autocomplete index files.
      --print-autopool                    Print the contents of the consolidated autocomplete pool.
      --rebuild-autocomplete              Rebuild the local package name index.

Experimental/Future:
  depends <pkg>                           Placeholder: Graphical dependency visualizer.
  verify <pkg>                            Cryptographic package verification using GPG.

Note: Commands can be interleaved, e.g., 'runepkg -v -i pkg1.deb -s pkg2 -i pkg3.deb'
Note: FFI features (C++) are enabled based on your build target (`make all`).
```

<p align="left">
  <img src="./runepkg/runepkg_logo.svg" width="400" alt="runepkg Logo">
  <br>
</p>

## Philosophy & Background
---
*Built with ❤️ for the GNU/Linux community. **runepkg** treats packages as building blocks rather than strict system policies, allowing power users to build and compile .deb packages exactly as they wish.*

Developed from years of experience with Custom Cross Linux From Scratch (LFS), **runepkg** views ancient `.deb` packages as "runes"—valuable historical artifacts. This tool empowers you to unearth and run legacy software from Debian archives safely in modern environments.

## Contact
---
🍆 [michkochris@gmail.com](mailto:michkochris@gmail.com) | [runepkg@gmail.com](mailto:runepkg@gmail.com)
