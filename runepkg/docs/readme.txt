# runepkg

[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://github.com/michkochris/runepkg)
[![FFI: Rust](https://img.shields.io/badge/FFI-Rust-orange.svg)](https://www.rust-lang.org/)
[![FFI: C++](https://img.shields.io/badge/FFI-C%2B%2B-blue.svg)](https://isocpp.org/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)



### **Installation**
```bash
git clone https://github.com/michkochris/runepkg.git
cd runepkg/runepkg

# Core low level build only (no Rust/C++ FFI) no apt like extension
make runepkg

# Full build (includes optional Rust/C++ FFI if available) for apt like extensions
make all

# Install
sudo make install

# Termux install
make termux-install

# This automatically installs:
# - Binary to /usr/bin/runepkg
# bashrc here ---
# - System config to /etc/runepkg/runepkgconfig
# Termux install friendly 
```
⚡ Build complete messages are shown by the build system.

### **Uninstallation**
```bash
make clean
sudo make uninstall

# Termux uninstallation
make clean
make termux-uninstall

```
🧹 Clean complete is shown after cleanup. This removes the binary, completion, and config file.
### **Basic Usage**
```
runepkg - (fast effiecient old schoold .deb package manager)

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

### **Interleaved Autocompletion**
runepkg provides fast, built-in binary autocompletion for interleaved commands, options, and package names — no external shell scripts or manual setup required.

#### Example
```bash
runepkg -i somedir/somedeb<TAB>  # Completes path to some.deb files with smart dependency searching in same place...
runepkg -r binutils-<TAB>      # Completes installed packages
```

### **Dependency Resolution**
runepkg automatically resolves and installs package dependencies from .deb files in the same directory. Use `--force` to install despite missing dependencies.

For example:
```
runepkg -i package.deb  # Installs package and its .deb depends if available and found in same place
runepkg -f -i package.deb  # Forces install even if deps are missing
```

### **FFI Notes**
- Rust/C++ FFI are optional. Core builds use `make runepkg` for low level tool like dpkg...
- Full builds use `make all` and will enable rust and cpp FFI if the toolchains are present for debian apt like enhancements...


*Advancing the art of .deb package management through performance, automatic .deb dependency resolution, and multi-language integration.*

*Built with ❤️ for the old school gnu linux community by myself based off my knowledge and obssesion with custom cross linux from scratch.*

## License
This project is licensed under the **GNU General Public License v3.0** - see the [LICENSE](LICENSE) file for complete details.
