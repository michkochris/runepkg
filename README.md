# runepkg - Fast Old School linux .deb Package Manager

[![Language: C FFI: Rust C++](https://img.shields.io/badge/Language-C-blue.svg)](https://github.com/michkochris/runepkg)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

### **Installation**
```bash
git clone https://github.com/michkochris/runepkg.git
cd runepkg/runepkg

# Core build only (no Rust/C++ FFI)
make runepkg

# Full build (includes optional Rust/C++ FFI if available)
make all

# Install
sudo make install
```
⚡ Build complete messages are shown by the build system.
### **Uninstallation**
```bash
make clean
make uninstall
```
🧹 Clean complete is shown after cleanup.
### **Basic Usage**
```
runepkg - The Runar Linux package manager.

Usage:
    runepkg <COMMAND> [OPTIONS] [ARGUMENTS]

Commands and Options:
    -i, --install <path-to-package.deb>...  Install one or more .deb files.
    -r, --remove <package-name>             Remove a package.
    -l, --list                              List all installed packages.
    -s, --status <package-name>             Show detailed information about a package.
    -L, --list-files <package-name>         List files for a package.
    -S, --search <query>                    Search for a package by name.
    -v, --verbose                           Verbosity: -v=verbose, -vv=very verbose.
    -f, --force                             Force install even if dependencies are missing.
            --version                           Print version information.
    -h, --help                              Display this help message.

            --print-config                      Print current configuration settings.
            --print-config-file                 Print path to configuration file in use.
Note: Commands can be interleaved, e.g., 'runepkg -v -i pkg1.deb -s pkg2 -i pkg3.deb'
```

### **Batch Install & Summary Output**
runepkg prints a single hybrid summary line per batch, followed by dpkg-style output lines:
```
pkgs=2 fields=29 extract=152ms hash=OK 0ms (hits=0 lf=0.40 depth=1 size=5 resizes=1) | store=OK 1ms (bytes=3.8KB)
Install summary: needed=3.8KB avail=952.6GB
Preparing to unpack file (1:5.46-5)...
Setting up file (1:5.46-5)...
Preparing to unpack busybox-static (1:1.37.0-9)...
Setting up busybox-static (1:1.37.0-9)...
```

You can also feed a list of .deb files via stdin:
```
runepkg -i debs/*.deb

ls debs/*.deb > debs_list.txt
cat debs_list.txt | ./runepkg -i

printf "%s\n" debs/*.deb | ./runepkg -i

```
### **Dependency Resolution**
runepkg automatically resolves and installs package dependencies from .deb files in the same directory. Use `--force` to install despite missing dependencies.

For example:
```
runepkg -i package.deb  # Installs package and its deps if available
runepkg -f -i package.deb  # Forces install even if deps are missing
```
### **Core Components**
```
runepkg/
├── runepkg_cli.c              # Command-line interface
├── runepkg_install.*          # Install workflow + batch diagnostics
├── runepkg_remove.*           # Remove workflow + batch diagnostics
├── runepkg_config.*           # Configuration management
├── runepkg_hash.*             # High-performance hashing
├── runepkg_pack.*             # Package creation/extraction
├── runepkg_storage.*          # Database and file management
├── runepkg_util.*             # Common utilities
├── runepkg_defensive.*        # Security and validation
├── runepkg_rust_ffi.h         # Rust FFI ABI (optional)
├── runepkg_rust_api.*         # Rust FFI wrapper (optional)
├── runepkg_cpp_ffi.h          # C++ FFI ABI (optional)
├── runepkg_cpp_api.c          # C++ FFI wrapper (optional)
├── runepkg_cpp_impl.cpp       # C++ implementation (optional)
└── gccrs-src/                 # Rust components
```

### **FFI Notes**
- Rust/C++ FFI are optional. Core builds use `make runepkg`.
- Full builds use `make all` and will enable FFI if the toolchains are present.

*runepkg - *Advancing the art of .deb package management through performance, automatic dependency resolution, and multi-language integration.*

*Built with ❤️ for the Linux community by developers who believe in secure, efficient, and intelligent package management.*

## 📄 License
This project is licensed under the **GNU General Public License v3.0** - see the [LICENSE](LICENSE) file for complete details.