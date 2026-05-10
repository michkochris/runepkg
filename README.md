```text
 [#######]  runepkg
 [# \ / #]  version 1.0.4
 [#  V  #]
 [#######]  GPL-V3
```

## runepkg (fast efficient old-school .deb package manager)

[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://github.com/michkochris/runepkg)
[![FFI: C++](https://img.shields.io/badge/FFI-C%2B%2B-blue.svg)](https://isocpp.org/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

**runepkg** is a high-performance, repository-aware package manager for `.deb` files, designed with an "old-school" philosophy but modern internal architecture. It bridges the gap between low-level tools like `dpkg` and high-level managers like `apt`.

### **Architecture & Internals**

runepkg is built with a clear separation of concerns:
- **C Core**: The low-level package management logic (installation, removal, filesystem operations) is written in pure C, maintaining a footprint and behavior similar to `dpkg`.
- **C++ FFI**: Extended "apt-like" features—including network operations, repository synchronization, and advanced dependency resolution—are implemented via a high-performance C++ FFI (Foreign Function Interface) layer.

#### **Hybrid Persistent Storage**
The storage engine is a custom hybrid design that borrows the best ideas from legendary package managers:
- **Arch Linux Inspired**: Fast package lookup is achieved through a `pkgname` subdirectory structure, significantly reducing directory traversal overhead during queries.
- **RPM Inspired**: Detailed package metadata is stored in binary `pkginfo` files, allowing for near-instant parsing of package state and requirements compared to traditional text-based control files.

#### **Binary Autocompletion**
runepkg features a fully-featured, interleaved binary autocompletion system. Unlike shell-script based completion, the `runepkg` binary itself handles completion logic:
- **Repo Detection**: It automatically detects package names from both local databases and remote repository indexes.
- **Interleaved Flags**: It understands interleaved commands and flags (e.g., `runepkg -v -i <TAB>`), providing context-aware suggestions for both local `.deb` files and repository packages.

---

### **Usage**

#### **Local Package Management**
```bash
runepkg -i package.deb     # Install a local .deb file
runepkg -r <pkg-name>      # Remove an installed package
runepkg -l [pattern]       # List installed packages
runepkg -s <pkg-name>      # Show detailed status for a package
runepkg -L <pkg-name>      # List all files owned by a package
runepkg -S <file-path>     # Search installed packages for a specific file
```

#### **Repository Management**
```bash
runepkg update             # Sync metadata and check for upgrades
runepkg upgrade            # Download and install available upgrades
runepkg search <query>     # Search repositories for packages
                           # (searches both package names and descriptions)
                           # Note: Use "quotes" for multi-word descriptions
                           # e.g., runepkg search "web browser"
runepkg download-only <pkg># Download a .deb without installing it
runepkg source <pkg>       # Download source package files
```

#### **Maintenance & Diagnostics**
```bash
runepkg --rebuild-autocomplete # Force a rebuild of the package index
runepkg --print-config         # Show active path and repo settings
```

---

### **Installation**

```bash
git clone https://github.com/michkochris/runepkg.git
cd runepkg/runepkg

# Build with FFI extensions (requires g++, libcurl)
make all

# System-wide install
sudo make install
```

*Built with ❤️ for the old school GNU/Linux community...*

## License
This project is licensed under the **GNU General Public License v3.0**.
