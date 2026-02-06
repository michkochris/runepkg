# runepkg

[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://github.com/michkochris/runepkg)
[![FFI: Rust](https://img.shields.io/badge/FFI-Rust-orange.svg)](https://www.rust-lang.org/)
[![FFI: C++](https://img.shields.io/badge/FFI-C%2B%2B-blue.svg)](https://isocpp.org/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

**Latest Milestone**: Intelligent autocompletion with interleaved command support and automatic package list updates! 🚀

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

# This automatically installs:
# - Binary to /usr/bin/runepkg
# - Bash completion to /usr/share/bash-completion/completions/runepkg
# - System config to /etc/runepkg/runepkgconfig
```
⚡ Build complete messages are shown by the build system.

### **Uninstallation**
```bash
make clean
sudo make uninstall
```
🧹 Clean complete is shown after cleanup. This removes the binary, completion, and config files.
### **Basic Usage**
```
runepkg - The Runar Linux package manager.

Usage:
    runepkg <COMMAND> [OPTIONS] [ARGUMENTS]

Commands and Options:
    -i, --install <path-to-package.deb>...  Install one or more .deb files.
        --install -                         Read .deb paths from stdin.
        --install @file                     Read .deb paths from a list file.
    -r, --remove <package-name>             Remove a package.
        --remove -                          Read package names from stdin.
    -l, --list                              List all installed packages.
        --list <pattern>                    List installed packages matching pattern.
    -s, --status <package-name>             Show detailed information about a package.
    -L, --list-files <package-name>         List files for a package.
    -S, --search <file-path>                Search for packages containing files matching path.
    -v, --verbose                           Enable verbose output.
    -f, --force                             Force install even if dependencies are missing.
        --version                           Print version information.
    -h, --help                              Display this help message.

        --print-config                      Print current configuration settings.
        --print-config-file                 Print path to configuration file in use.
        --print-pkglist-file                Print paths to autocomplete files.
Note: Commands can be interleaved, e.g., 'runepkg -v -i pkg1.deb -s pkg2 -i pkg3.deb'
```

### **Autocompletion**
runepkg supports intelligent bash autocompletion for commands, options, and package names. It handles interleaved commands seamlessly.

#### Setup
```bash
# Source the completion script
source runepkg/runepkg_completions

# Or add to your ~/.bashrc for permanent setup
echo "source /path/to/runepkg/runepkg_completions" >> ~/.bashrc
```

#### Features
- **Interleaved Commands**: Completes correctly in complex command sequences (e.g., `runepkg -i pkg1 -r <tab>` suggests package names).
- **Context-Aware**: 
  - After `-i` or `-S`: Completes file paths.
  - After `-r`, `-s`, `-L`: Completes installed package names.
  - Otherwise: Completes options and package names.
- **Automatic Updates**: Package list is automatically updated after successful installations, keeping completions current.

#### Example
```bash
runepkg -i debs/binutils<TAB>  # Completes .deb files
runepkg -r binutils-<TAB>      # Completes installed packages
runepkg -v -i pkg1.deb -s <TAB>  # Completes package names for status
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

*runepkg - Fast Old School linux .deb Package Manager*

*Advancing the art of .deb package management through performance, automatic dependency resolution, and multi-language integration.*

*Built with ❤️ for the Linux community by developers who believe in secure, efficient, and intelligent package management.*

## � Development Tasks

- [x] First task: Completed
- [x] Second task: Completed (call now handled by helper, validation done via assumption)

## �📄 License
This project is licensed under the **GNU General Public License v3.0** - see the [LICENSE](LICENSE) file for complete details.