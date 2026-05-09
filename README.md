## runepkg (fast efficient old school .deb package manager)

[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://github.com/michkochris/runepkg)
[![FFI: Rust](https://img.shields.io/badge/FFI-Rust-orange.svg)](https://www.rust-lang.org/)
[![FFI: C++](https://img.shields.io/badge/FFI-C%2B%2B-blue.svg)](https://isocpp.org/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

**runepkg** is a high-performance, repository-aware .deb package manager designed for Runar Linux and other Debian-based systems. It bridges the gap between low-level tools like `dpkg` and high-level managers like `apt`, offering extreme speed through a custom binary indexing system and C++ FFI extensions.

### **Core Features**
- 🚀 **Extreme Speed**: Parallel repository updates and $O(\log N)$ binary name indexing for 70,000+ packages.
- 📦 **Smart Installation**: Intelligent `-i` command that installs local `.deb` files OR automatically downloads them from repositories if not found locally.
- 🔍 **Advanced Search**: Apt-like full-text search across package names and descriptions.
- 🛠️ **Dependency Resolution**: Automatic resolution of dependencies from both local directories and remote repositories.
- ⌨️ **Intelligent Autocompletion**: Self-completing binary that provides instant `<TAB>` suggestions for commands, flags, and repository packages.
- 🔗 **FFI Extensions**: High-level functionality implemented via Rust (highlighter) and C++ (networking/indexing) FFI.

---

### **Installation**
```bash
git clone https://github.com/michkochris/runepkg.git
cd runepkg/runepkg

# Full build (includes Rust/C++ FFI for repository extensions)
make all

# Core low-level build only (no FFI)
make runepkg

# System-wide install
sudo make install

# Termux install
make termux-install
```

### **Configuration**
Customize your paths and repositories in `/etc/runepkg/runepkgconfig`. You can define custom `install_dir`, `download_dir`, and standard Debian/Kali `deb` source lines.

---

### **Usage Guide**

#### **Repository Management**
```bash
runepkg update             # Sync metadata from repositories and check for upgradable packages
runepkg search <query>     # Search for packages in the repository (names & descriptions)
runepkg download-only <pkg># Download a .deb to the configured download_dir
```

#### **Package Installation & Removal**
```bash
runepkg -i package.deb     # Install a local .deb file
runepkg -i tree            # Auto-download and install 'tree' from repository
runepkg -r <pkg-name>      # Remove an installed package
```

#### **Inspection & Queries**
```bash
runepkg -l [pattern]       # List installed packages
runepkg -s <pkg-name>      # Show detailed status/metadata for a package
runepkg -L <pkg-name>      # List all files owned by a package
runepkg -S <file-path>     # Search installed packages for a specific file
```

---

### **Technical Highlights**

#### **Hybrid Binary Indexing**
runepkg uses a three-tier metadata strategy:
1. **Tier 1**: A sorted binary mmap'd index (`repo_index.bin`) for instant package lookups.
2. **Tier 2**: A flat-file metadata cache for full-text description searches.
3. **Tier 3**: An in-memory version mapping for real-time upgrade detection.

#### **FFI Multi-Language Architecture**
- **C Core**: Handles the filesystem, installation logic, and basic package management.
- **C++ Extension**: Manages parallel HTTP/HTTPS downloads (via libcurl), zlib decompression, and high-performance repository indexing.
- **Rust Extension**: Integrated for advanced syntax highlighting and future safety-critical modules.

---

### **Uninstallation**
```bash
sudo make uninstall
make clean
```

*Advancing the art of .deb package management through performance, automatic resolution, and multi-language integration.*

*Built with ❤️ for the old school GNU/Linux community by Chris Michael Koch.*

## License
This project is licensed under the **GNU General Public License v3.0** - see the [LICENSE](LICENSE) file for complete details.
