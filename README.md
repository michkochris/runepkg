# runepkg

[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://github.com/michkochris/runepkg)
[![FFI: C++](https://img.shields.io/badge/FFI-C%2B%2B-blue.svg)](https://isocpp.org/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

**runepkg** is a fast, efficient, old-school `.deb` package manager.

---

**runepkg** can be used as a standalone, low-level package management tool (similar to `dpkg`) for embedded systems, or as an advanced high-level package manager (similar to `apt` or `apt-get`). It can safely coexist with `dpkg` and `apt`. Additionally, you can customize the `install_dir` in the configuration file to install packages to alternate locations.

### **Philosophy & Background**
This program was built for the love of Open Source software, drawing on my experience as an old-school GNU/Linux hobbyist and a passion for Custom Cross Linux From Scratch (LFS).

If you are interested in the technical and insightful architectural decisions behind how **runepkg** was built, you can find a detailed deep-dive in [INTERNALS.md](./INTERNALS.md).

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

## **Configuration**
The configuration file is located at `/etc/runepkg/runepkgconfig`. This file contains various path variables, including the `install_dir`. Repository information is stored at the bottom of the file in a standard Debian `sources.list` format. You can use any Debian-based repositories (including Ubuntu, Kali, or legacy archives).

---

## **Usage**
For basic commands, see the help output below. For comprehensive examples, advanced workflows, and repository management details, please refer to [USAGELONG.md](./USAGELONG.md).
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

  [#######]  runepkg
  [# \ / #]  version 1.0.4
  [#  V  #]
  [#######]  GPL-V3

*Built with ❤️ for the old school GNU/Linux community...*
Copyright (c) 2025 runepkg (Runar Linux) All rights reserved.
Licensed under GPL v3
```

---


