# upkg - Lightweight Debian Package Manager

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C99](https://img.shields.io/badge/Standard-C99-blue.svg)](https://en.wikipedia.org/wiki/C99)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Termux-lightgrey.svg)](https://github.com/upkg/upkg)

## Overview

**upkg** is a lightweight and creative alternative to traditional package managers like `dpkg`, `pacman`, and `rpm`. Designed with thought and ingenuity, upkg provides an efficient solution for installing and managing `.deb` Linux packages with enhanced performance and simplified configuration.

## Key Features

- 🚀 **Lightweight & Fast** - Minimal resource usage with maximum efficiency
- 🔧 **Simple Configuration** - Cascading configuration system with sensible defaults
- 📱 **Cross-Platform** - Works on standard Linux systems and Termux (Android)
- 🎯 **Debian Compatible** - Manages standard `.deb` packages seamlessly
- 🔍 **Enhanced Logging** - Verbose mode for debugging and system analysis
- ⚙️ **Modular Design** - Clean, maintainable C99 codebase

## Quick Start

### Installation
```bash
# Standard Linux
make && sudo make install

# Termux (Android)
make && make termux-install
```

### Basic Usage
```bash
# Install packages
upkg -i package.deb

# List installed packages
upkg -l

# Remove a package
upkg -r package-name

# Search packages
upkg -S search-term
```

## Documentation

### 📖 [Installation Guide](install.md)
Complete installation instructions for Linux and Termux, including:
- Prerequisites and build requirements
- Platform-specific installation steps
- Configuration system setup
- Troubleshooting and advanced options

### 🐧 [uLinux Distribution](ulinux.md)
Learn about uLinux, the custom Linux From Scratch distribution that uses upkg:
- Build system architecture
- Source-to-package automation
- Integration with upkg package management
- Comparison with other distributions

## Architecture

upkg consists of two main components:

### Core Components
- **`upkg_cli.c`** - Command-line interface and main program logic
- **`upkg_config.c`** - Self-contained configuration management system
- **`upkg_config.h`** - Clean API for configuration access

### Configuration System
upkg uses a **cascading configuration system** that provides flexibility and ease of use:

1. **Environment Variable**: `$UPKG_CONFIG_PATH` (highest priority)
2. **System-wide**: `/etc/upkg/upkgconfig` (Linux only)
3. **User-specific**: `~/.upkgconfig` (fallback)

## Commands

| Command | Description | Example |
|---------|-------------|---------|
| `-i, --install` | Install package(s) | `upkg -i package.deb` |
| `-r, --remove` | Remove package | `upkg -r package-name` |
| `-l, --list` | List installed packages | `upkg -l` |
| `-s, --status` | Show package status | `upkg -s package-name` |
| `-S, --search` | Search packages | `upkg -S keyword` |
| `-u, --update` | Update package database | `upkg -u` |
| `-v, --verbose` | Verbose output | `upkg -v -l` |
| `--help` | Show help message | `upkg --help` |
| `--version` | Show version info | `upkg --version` |

## Configuration

### Default Configuration File (`upkgconfig`)
```bash
# Main working directory
upkg_dir=~/upkg_dir

# Control files extraction
control_dir=~/upkg_dir/controldir

# Package files extraction  
unpack_dir=~/upkg_dir/unpackdir

# Installation target
install_dir=~/upkg_dir/installdir

# Package database
db_dir=~/upkg_dir/db
```

### Environment Override
```bash
export UPKG_CONFIG_PATH=/custom/path/to/upkgconfig
upkg -v --version  # Shows which config is loaded
```

## Building from Source

### Prerequisites
- GCC compiler with C99 support
- Make utility
- Standard C libraries
- POSIX-compliant system

### Build Process
```bash
# Clone the repository
git clone https://github.com/upkg/upkg.git
cd upkg

# Build
make

# Install (choose one)
sudo make install        # Standard Linux
make termux-install      # Termux/Android

# Optional: Create user config
make create-user-config
```

### Build Targets
```bash
make               # Standard build
make debug         # Debug build with symbols
make test          # Build and test functionality
make clean         # Clean build artifacts
make info          # Show build information
```

## Platform Support

### Linux Distributions
- **Ubuntu/Debian** - Native `.deb` package support
- **Fedora/RHEL** - Works with converted packages
- **Arch Linux** - Alternative to pacman for .deb packages
- **Custom Distributions** - Perfect for uLinux and LFS systems

### Termux (Android)
- Full functionality on Android devices
- Optimized for mobile environments
- No root access required
- Seamless integration with Termux ecosystem

## Comparison with Other Package Managers

| Feature | upkg | dpkg | pacman | rpm |
|---------|------|------|--------|-----|
| **Size** | ~50KB | ~2MB | ~1MB | ~3MB |
| **Dependencies** | Minimal | Many | Medium | Many |
| **Speed** | Very Fast | Fast | Fast | Medium |
| **Configuration** | Simple | Complex | Medium | Complex |
| **Mobile Support** | Yes | Limited | No | No |
| **Custom Distros** | Excellent | Good | Limited | Limited |

## Development

### Project Structure
```
upkg/
├── upkg_cli.c          # Main CLI interface
├── upkg_config.c       # Configuration management
├── upkg_config.h       # Configuration API
├── upkgconfig          # Default configuration
├── Makefile            # Build system
├── README.md           # This file
├── install.md          # Installation guide
└── ulinux.md           # uLinux distribution info
```

### Coding Standards
- **C99 Standard** - Modern C with GNU extensions
- **POSIX Compliance** - Portable across Unix-like systems
- **Memory Safety** - Careful memory management
- **Error Handling** - Comprehensive error checking
- **Documentation** - Well-commented code

### Contributing
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests for new functionality
5. Ensure all tests pass
6. Submit a pull request

## Integration with uLinux

upkg was designed specifically to work with **uLinux**, a custom Linux From Scratch distribution:

### Build Integration
- uLinux build scripts create `.deb` packages
- Metadata collection during compilation
- Automatic dependency resolution
- Seamless package installation

### Runtime Benefits
- Optimized for source-built systems
- Minimal overhead on custom distributions
- Enhanced debugging for build environments
- Perfect for embedded and minimal systems

[Learn more about uLinux →](ulinux.md)

## Use Cases

### System Administrators
- Lightweight alternative to dpkg
- Custom Linux distribution management
- Embedded system package management
- Development environment setup

### Developers
- Android development with Termux
- Cross-platform package testing
- Custom distribution development
- Minimal system requirements

### Enthusiasts
- Linux From Scratch systems
- Custom kernel distributions
- Learning package management internals
- Experimenting with package formats

## Troubleshooting

### Common Issues

**Configuration not found:**
```bash
upkg -v --version  # Check which config is loaded
make create-user-config  # Create default user config
```

**Permission denied:**
```bash
chmod 644 ~/.upkgconfig  # Fix config permissions
sudo chown $USER ~/.upkgconfig  # Fix ownership
```

**Build errors:**
```bash
make clean && make  # Clean rebuild
gcc --version  # Check compiler version
```

### Getting Help
- Check the [Installation Guide](install.md) for detailed instructions
- Use verbose mode (`upkg -v`) for debugging
- Review configuration file syntax
- Verify file permissions and paths

## License

upkg is licensed under the **GNU General Public License v3.0** (GPL-3.0).

```
Copyright (c) 2025 upkg (ulinux) All rights reserved.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
```

See the [LICENSE](https://www.gnu.org/licenses/gpl-3.0.html) file for details.

## Support

- **Documentation**: [Installation Guide](install.md) | [uLinux Info](ulinux.md)
- **Issues**: [GitHub Issues](https://github.com/upkg/upkg/issues)
- **Discussions**: [GitHub Discussions](https://github.com/upkg/upkg/discussions)
- **Email**: michkochris@gmail.com

## Acknowledgments

- **Linux From Scratch** - Inspiration for build-from-source philosophy
- **Debian Project** - Package format compatibility
- **Termux Team** - Android development environment
- **GNU Project** - Core utilities and licensing

---

**upkg** - Where lightweight meets powerful. 🚀
