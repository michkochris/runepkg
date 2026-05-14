# runepkg

[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://github.com/michkochris/runepkg)
[![FFI: C++](https://img.shields.io/badge/FFI-C%2B%2B-blue.svg)](https://isocpp.org/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

**runepkg** is a lightning-fast, high-performance .deb package manager. It is designed to be both a high-level tool such as `apt` or `apt-get` for managing repositories, packages and dependencies with incredible speed, or a low-level package management tool for minimal embedded systems. It provides surgical precision required for custom .deb package builds and installs with the freedom to bypass the strict or rigid policy requirements of mainstream distributions.

---

**runepkg's** core is written in standard C, allowing for minimal low level installs similar to `dpkg` can be used for memory constrained embedded systems with high portability across environments like **musl libc** and compatibility with compilers such as **gcc**, **clang**, or **tcc** and use with `busybox`. 

**runepkg's** advanced C++ FFI features include high-speed parallel networking, repository synchronization, and a pure C++ Debian source package builder.

### **Philosophy & Background** 
*Built with ❤️ for the old school GNU/Linux community and open source software with the freedom of compiling, building .deb's the way a power user wants without all the strict system specific rules such as an official Debian based Linux distribution. **runepkg** allows you to treat packages as building blocks or components rather than strict system policy...*

**runepkg** has been developed using my experience and expertise as an old-school GNU/Linux hobbyist and my obsession for Custom Cross Linux From Scratch (LFS). The name **runepkg** stems from the vision of treating ancient `.deb` packages as "runes"—valuable historical artifacts preserved in the Debian archives. This tool is designed to give you the power to unearth and run this legacy software from the debian archives safely in modern environments.

If you are interested in the technical and insightful architectural decisions behind how **runepkg** was built, you can find a detailed technical expalination in [INTERNALS.md](./INTERNALS.md).

---

## **The runepkg Difference: Beyond apt-get**
While traditional tools like **apt-get** are built to maintain a strict, consistent system state, **runepkg** is built for the builder. It excels in performance through its **Parallel FFI Engine**, which handles metadata updates and package fetches significantly faster than sequential tools. Beyond speed, runepkg's dependency resolution is designed to be "context-aware"—it prioritizes local 'sibling' packages and allows for manual overrides that would typically trigger a deadlock in apt. This makes it not just a faster alternative, but a more flexible one for experimental and custom environments. While **runepkg** can manage repositories like `apt` with incredible speed and efficiency, matching and often exceeding its core performance, it is fundamentally designed for manual control and surgical precision, making it an ideal tool for **Custom Linux From Scratch (LFS)** and advanced system builders.

### **1. Surgical Precision**
Unlike `apt-get source`, which often pulls in a massive tree of build-dependencies, `runepkg source` downloads only the "raw runes" (upstream source + Debian patches). This allows you to inspect and modify the code, the `rules` Debian build script, or the `control` file, metadata (package name, version, and dependencies) without being forced into a specific build environment or strict system policies...

### **2. The "Hacker" Build Loop**
**runepkg** provides high-speed package management that matches the convenience of `apt-get` while enabling a power user for a "fetch-edit-build" workflow:
- **Fetch**: Use `runepkg source` to downlaod a debian source package into your `build_dir`.
- **Edit**: Manually modify `debian/rules`, `control`, or the source code itself to strip dependencies or apply custom cross-compilation flags.
- **Build**: Use `runepkg source-build /path/to/<package.dsc>` to trigger a build. **runepkg** will attempt to build your modified source without the strict dependency gatekeeping found in mainstream tools.

### **3. Manual Assembly & Custom Builders**
For those creating custom Linux distros, bootable custom Linux iso's or using automatic build scripts (like [`some_linux_builder`](https://github.com/michkochris/some_linux_builder)), **runepkg** provides:
- **Direct Build**: `runepkg -b <dir> [output.deb]` builds a `.deb` instantly from any standard .deb folder structure.
- **FHS Initialization**: `runepkg_util_init_fhs` (available via C API) can bootstrap a full filesystem skeleton in seconds.
- **Standalone Mode**: The core is pure C and can run on minimal systems (musl) similar to `dpkg` when high level tools such as `c++`, are unavailable for example...

---

## **Installation**

### **Dependencies**

**runepkg** is built for extreme portability and minimal footprint. Its dependencies are split between the compilation requirements of the binary itself and the runtime utilities required for package manipulation and source building.

#### **1. Compilation Dependencies**
- **Core (Minimal):** Requires only a standard C compiler (e.g., `gcc`, `clang`, or `tcc`) and `make`. This version is ideal for minimal LFS or embedded targets.
- **Standard (Full):** Includes the C++ FFI layer for advanced networking and repository synchronization. Requires a C++ compiler (e.g., `g++` or `clang++`), **libcurl**, **OpenSSL**, and **zlib**.

#### **2. Runtime Utilities & BusyBox Integration**
For package extraction and assembly, and debian source package building, **runepkg** relies on standard system utilities. **runepkg is designed to be fully compatible with BusyBox**, making it perfect for minimal environments.

- **Standard Utilities:** Uses `ar` (from `binutils`) for managing the `.deb` archive format, and `tar` (along with `gzip` or `xz`) for extracting and compressing the control and data components.
- **BusyBox low level (Embedded/LFS):** In resource-constrained environments, BusyBox can provide all necessary applets (`ar`, `tar`, `gz`, `xz`).

**Required Packages by Distribution:**

**runepkg** can safely coexist with other package managers as long as the `install_dir` from the configuration file `runepkgconfig` is set to an alternate location.

- **Debian/Ubuntu:**
  - **Core Dependencies (All Runtime):** `binutils`, `tar`, `gzip`, `xz-utils`
  - **Build Dependencies (C Core):** `gcc`, `make`, `libc6-dev`
  - `sudo apt update && sudo apt install binutils tar gzip xz-utils gcc make libc6-dev`
  - **Full Advanced Dependencies (C++ FFI Features):** `g++`, `libcurl4-openssl-dev`, `libssl-dev`, `zlib1g-dev`
  - `sudo apt update && sudo apt install binutils tar gzip xz-utils gcc g++ make libc6-dev libcurl4-openssl-dev libssl-dev zlib1g-dev`
- 
- **Arch Linux:**
  - **Core Dependencies (All Runtime):** `binutils`, `tar`, `gzip`, `xz`
  - **Build Dependencies (C Core):** `base-devel`
  - `sudo pacman -S --needed binutils tar gzip xz base-devel`
  - **Full Advanced Dependencies (C++ FFI Features):** `curl`, `openssl`, `zlib`
  - `sudo pacman -S --needed binutils tar gzip xz base-devel curl openssl zlib`
- 
- **Fedora/RHEL:**
  - **Core Dependencies (All Runtime):** `binutils`, `tar`, `gzip`, `xz`
  - **Build Dependencies (C Core):** `gcc`, `make`, `glibc-devel`
  - `sudo dnf install binutils tar gzip xz gcc make glibc-devel`
  - **Full Advanced Dependencies (C++ FFI Features):** `gcc-c++`, `libcurl-devel`, `openssl-devel`, `zlib-devel`
  - `sudo dnf install binutils tar gzip xz gcc gcc-c++ make glibc-devel libcurl-devel openssl-devel zlib-devel`

## **Configuration runepkgconfig**
Edit before install for user preferences...
The configuration file will be installed to `/etc/runepkg/runepkgconfig` like other standard Linux programs. This file contains various path variables, including the `install_dir`. Repository information Debian (sources.list) info is stored at the bottom of the file in a standard Debian format. You can use any Debian-based repositories (including Debian, Ubuntu, Kali, or legacy debian archives)...

---

### **Customizing the Compiler**
The `Makefile` supports overriding the default compilers. If you prefer to use `clang` or `tcc` instead of `gcc`, you can pass the variables directly to `make`:

```bash
CC=clang CXX=clang++ make all
sudo make install
```

### **Embedded Installation (Minimal)**
For embedded systems, you can build the core `runepkg` in pure C. This version excludes the Extended C++ FFI features...

```bash
make runepkg
sudo make install
```

### **Standard Installation (Full)**
For a full installation including advanced C++ FFI networking features and debian source package building (similar to `apt` or `apt-get`), use:

```bash
make all
sudo make install
```

### **📦 Build as a .deb (Self-Building)**
**runepkg** is powerful enough to build its own .deb distribution package. If you want to create a `.deb` file of runepkg for install with traditional dpkg or busybox dpkg, run:

```bash
chmod +x make_runepkg_deb.sh
./make_runepkg_deb.sh
```
This script will compile the project and use the resulting binary to assemble a professional `.deb` package.

*Note: For a minimal embedded version, edit `make_runepkg_deb.sh` and change `make all` to `make runepkg`.*

### **⚡ Lightning Fast Autocomplete**
**runepkg** features an advanced, binary-driven completion engine that is significantly faster than standard shell scripts. To enable it, you must register the binary with your shell.

**Temporary (Current Session):**
```bash
complete -C runepkg runepkg
```

**Permanent (Recommended):**
Add the following line to the end of your `~/.bashrc`:
```bash
complete -C runepkg runepkg
```

### **🧹 Uninstallation & Cleanup**
To remove build artifacts or uninstall the program:

```bash
make clean
sudo make uninstall
```

*Note: You may receive a notification if certain system directories require manual removal...*

---

## **Usage**
For basic commands, see the help output below. For comprehensive examples, advanced workflows, and repository management details, please refer to [USAGELONG.md](./USAGELONG.md).
```bash
runepkg (fast efficient old-school .deb package manager)

Usage:
  runepkg <COMMAND> [OPTIONS] [ARGUMENTS]

Core Package Management (Local/Low-Level):
  -i, --install <path-to-package.deb>...  Install one or more .deb files.
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
  source-build <package.dsc>              Build a Debian source package into runepkg_debs.
  download-only <pkg>                     Download a .deb to download_dir without dependencies.
  download-depends <pkg>                  Download a .deb and its binary dependencies.
  download-build-depends <pkg>            Download binary .debs required to build a source package.

Maintenance & Diagnostics:
      --print-config                      Print all active path and repository settings.
      --print-config-file                 Show the path to the runepkgconfig file in use.
      --print-pkglist-file                Show paths to the autocomplete index files.
      --rebuild-autocomplete              Rebuild the local package name index.

Experimental/Future:
  depends <pkg>                           Placeholder: Graphical dependency visualizer.
  verify <pkg>                            Placeholder: Cryptographic package verification.

Note: Commands can be interleaved, e.g., 'runepkg -v -i pkg1.deb -s pkg2 -i pkg3.deb'
Note: FFI features (C++) are enabled based on your build target (`make all`).

  [#######]  runepkg
  [# \ / #]  version 1.0.4
  [#  V  #]
  [#######]  GPL-V3

*Built with ❤️ for the old school GNU/Linux community...*
Copyright (c) 2025 runepkg (Runar Linux) All rights reserved.
Contact: [michkochris@gmail.com] | [runepkg@gmail.com]
```

---

## **Contact**
For feedback, bug reports, or "rune" discoveries, you can reach out at:
- **Primary:** [michkochris@gmail.com](mailto:michkochris@gmail.com)
- **Project:** [runepkg@gmail.com](mailto:runepkg@gmail.com)

---


