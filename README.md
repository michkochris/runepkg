[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://github.com/michkochris/runepkg)
[![FFI: C++](https://img.shields.io/badge/FFI-C%2B%2B-blue.svg)](https://isocpp.org/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

**runepkg** is a fast, efficient, old-school `.deb` package manager.

---

## **Installation**

### **Embedded Installation (Minimal)**
For embedded systems, you can build the core `runepkg` in pure C. This version excludes the C++ FFI and extended networking functionality.

```bash
make runepkg
sudo make install
```

### **Standard Installation (Full)**
For a full installation including advanced C++ networking features (similar to `apt` or `apt-get`), use:

```bash
make all
sudo make install
```

### **Uninstallation & Cleanup**
To remove build artifacts or uninstall the program:

```bash
make clean
sudo make uninstall
```

*Note: You may receive a notification if certain system directories require manual removal.*

---

### **Usage**
```bash
runepkg (fast efficient old-school .deb package manager)

Usage:
  runepkg <COMMAND> [OPTIONS] [ARGUMENTS]

Package Management (Local):
  -i, --install <path-to-package.deb>...  Install one or more .deb files.
      --install -                         Read .deb paths from stdin.
      --install @file                     Read .deb paths from a list file.
  -r, --remove <package-name>             Remove an installed package.
      --remove -                          Read package names from stdin.
  -l, --list [pattern]                    List installed packages (optionally matching pattern).
  -s, --status <package-name>             Show detailed info about an installed package.
  -L, --list-files <package-name>         List all files owned by an installed package.
  -S, --search <file-path>                Search installed packages for a specific file.

Repository Management (Network):
  update                                  Sync metadata and check for upgradable packages.
  upgrade                                 Download and install all available upgrades.
  search <pkg|pattern>                    Search repositories for packages or patterns.
                                          (Use "quotes" to search for multiple words).
  source <pkg>                            Download source package files into download_dir.
  download-only <pkg>                     Download a .deb to download_dir without installing.

Global Options:
  -v, --verbose                           Enable verbose output (detailed logging).
  -d, --debug                             Enable debug output (developer traces).
  -f, --force                             Force install/upgrade despite missing dependencies.
      --version                           Print version and license information.
  -h, --help                              Display this help message.

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
```

---

```ansi
[90m [#######][0m  [1;37mrunepkg[0m
[90m [# [1;36m\ / [90m#][0m  [37mversion 1.0.4[0m
[90m [#  [1;36mV  [90m#][0m
[90m [#######][0m  [37mGPL-V3[0m
```

*Built with ❤️ for the old school GNU/Linux community...*

## **License**
This project is licensed under the **GNU General Public License v3.0**.
