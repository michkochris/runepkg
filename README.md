# runepkg

[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://github.com/michkochris/runepkg)
[![FFI: C++](https://img.shields.io/badge/FFI-C%2B%2B-blue.svg)](https://isocpp.org/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

**runepkg** is a lightning-fast, high-performance .deb package manager. It is designed to be versatile: serving as both a high-level tool like `apt` or `apt-get` for managing repositories and dependencies, and a low-level tool for minimal embedded systems. It offers the surgical precision required for custom builds and installs, providing the freedom to bypass the rigid policies of mainstream distributions.

## **Architecture & Portability**

**runepkg** is built with a dual-tier architecture to suit different environments:

- **Core Engine (Pure C):** The heart of the tool is written in standard C. This allows for minimal, low-level installations (similar to `dpkg`) on memory-constrained embedded systems. It is highly portable, supporting **musl libc**, `gcc`, `clang`, `tcc`, and integration with `busybox`.
- **Advanced Features (C++ FFI):** An optional C++ FFI layer provides high-speed parallel networking, repository synchronization, and a native Debian source package builder.

### **Philosophy & Background**
*Built with ❤️ for the GNU/Linux community. **runepkg** treats packages as building blocks rather than strict system policies, allowing power users to build and compile .deb packages exactly as they wish.*

Developed from years of experience with Custom Cross Linux From Scratch (LFS), **runepkg** views ancient `.deb` packages as "runes"—valuable historical artifacts. This tool empowers you to unearth and run legacy software from Debian archives safely in modern environments.

## **The runepkg Difference**

**runepkg** evolves package management by replacing the sequential, text-heavy bottlenecks of traditional tools with a performance-first hybrid architecture:

- **High-Speed Metadata:** Instead of parsing large flat files on every call, **runepkg** uses **binary-serialized metadata** (`pkginfo.bin`) and **memory-mapped indices** (`mmap`). This enables $O(\log n)$ search speeds across tens of thousands of packages.
- **Efficient Lookups:** A custom **FNV-1a hash table** with prime-sized buckets maintains $O(1)$ lookup performance regardless of database size.
- **Parallel Execution:** Features **parallel file extraction** via `pthread` for faster installs.
- **"Clandestine" Dependency Resolution:** Intelligently detects local sibling `.deb` files to satisfy dependencies before reaching for the network.
- **Hardened Plumbing:** The core C engine features `secure_malloc` with zero-wiping for enhanced security and reliability.

Technical details on these architectural decisions can be found in [INTERNALS.md](./INTERNALS.md).

## **Key Features**

### **1. Surgical Precision**
Unlike `apt-get source`, which may pull in massive build-dependency trees, `runepkg source` downloads only the "raw runes" (upstream source + Debian patches). This allows for direct inspection and modification of the `rules` build script or `control` metadata. **runepkg** remains independent of standard Debian tools; its **Native Build Fallback** can forge packages even when `debhelper` or `dpkg-dev` are missing.

### **2. The "Hacker" Build Loop**
**runepkg** enables a streamlined "fetch-edit-build" workflow:
- **Fetch**: Use `runepkg source` to unearth a source package into your build directory.
- **Edit**: Modify `debian/rules`, `control`, or the source code itself.
- **Build**: Use `runepkg build <pkg|dir|.dsc>` to trigger a build. **runepkg** attempts the build without the strict dependency gatekeeping of mainstream tools. It automatically handles package names (auto-fetching), extracted directories, or `.dsc` files.

### **3. Manual Assembly & Custom Builders**
- **Direct Build**: `runepkg build <dir> [output.deb]` builds a `.deb` instantly. It intelligently detects if the directory is a Debian source tree or a binary structure.
- **Multi-Package Ritual**: `runepkg buildpkg-split <pkg|dir|.dsc>` builds a source package and splits it into multiple `.deb` fragments (e.g., `bin`, `dev`, `doc`) based on `debian/control` stanzas.
- **FHS Initialization**: The C API provides `runepkg_util_init_fhs` to bootstrap a full filesystem skeleton in seconds.

## **Installation**

### **Dependencies**

#### **1. Compilation Dependencies**
- **Core (Minimal):** Requires only a standard C compiler (`gcc`, `clang`, or `tcc`) and `make`.
- **Standard (Full):** Includes the C++ FFI layer. Requires a C++ compiler, **libcurl**, **OpenSSL**, and **zlib**.

#### **2. Runtime Utilities**
For extraction and building, **runepkg** is fully compatible with **BusyBox** and standard utilities:
- **Standard:** `ar` (from `binutils`), `tar`, and compression tools (`gzip`, `xz`).
- **BusyBox:** Provides all necessary applets in resource-constrained environments.

**Required Packages (Debian/Ubuntu):**
- **Core:** `binutils`, `tar`, `gzip`, `xz-utils`, `gcc`, `make`, `libc6-dev`
- **Full:** Add `g++`, `libcurl4-openssl-dev`, `libssl-dev`, `zlib1g-dev`, `libncurses-dev`

## **Configuration**
Configuration is handled via `runepkgconfig`, typically installed to `/etc/runepkg/runepkgconfig`. You can define paths like `install_dir` and manage repository information in standard Debian format at the bottom of the file.

### **Build Options**

**Custom Compiler:**
```bash
CC=clang CXX=clang++ make all
sudo make install
```

**Embedded (Minimal C Only):**
```bash
make runepkg
sudo make install
```

**Standard (Full C/C++):**
```bash
make all
sudo make install
```

### **📦 Build as a .deb**
To create a `.deb` package of **runepkg** itself:
```bash
chmod +x make_runepkg_deb.sh
./make_runepkg_deb.sh
```

### **⚡ Lightning Fast Autocomplete**
To enable the advanced binary-driven completion engine, add this to your `~/.bashrc`:
```bash
complete -C runepkg runepkg
```

## **Usage**
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

  build <pkg|path>                        Build a source package by name or path to .dsc.
  buildpkg-split <package.dsc>            Build and split a source package into separate .debs.
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
  verify <pkg>                            Placeholder: Cryptographic package verification.

Note: Commands can be interleaved, e.g., 'runepkg -v -i pkg1.deb -s pkg2 -i pkg3.deb'
Note: FFI features (C++) are enabled based on your build target (`make all`).
```

<p align="center">
  <img src="./runepkg/docs/runepkg_logo.svg" width="400" alt="runepkg Logo">
  <br>
  <b>Built with ❤️ for the old school GNU/Linux community...</b><br>
  Copyright (c) 2025 runepkg (Runar Linux) All rights reserved.
</p>

## **Contact**
For feedback, bug reports, or "rune" discoveries, reach out at:
[michkochris@gmail.com](mailto:michkochris@gmail.com) | [runepkg@gmail.com](mailto:runepkg@gmail.com)