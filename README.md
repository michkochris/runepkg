# runepkg - Advanced Package Manager

**Modern package management for Runar Linux with multi-language FFI architecture**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![FFI: Rust](https://img.shields.io/badge/FFI-Rust-orange.svg)](https://www.rust-lang.org/)
[![FFI: C++](https://img.shields.io/badge/FFI-C++-red.svg)](https://isocpp.org/)

## Overview

runepkg is a sophisticated package manager that leverages a multi-language FFI (Foreign Function Interface) architecture to provide the best of each programming language:

- **C Core** - Rock-solid package management, memory efficiency, system integration
- **Rust FFI** - Advanced syntax highlighting, secure script processing  
- **C++ FFI** - Complex networking, dependency resolution, modern standard library features

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    runepkg Multi-Language Architecture      │
├─────────────────────┬─────────────────────┬─────────────────┤
│      C Core         │     Rust FFI        │    C++ FFI      │
│                     │                     │                 │
│ • Package Management│ • Syntax Highlighting│ • HTTP/HTTPS   │
│ • Hash Tables       │ • Script Validation │ • Dependency    │
│ • Configuration     │ • Metadata Extraction│   Resolution   │
│ • File Operations   │ • Secure Execution  │ • Parallel      │
│ • Memory Management │ • Theme Support     │   Downloads     │
│ • Storage System    │ • Error Reporting   │ • JSON Parsing  │
└─────────────────────┴─────────────────────┴─────────────────┘
```

## Quick Start

### Prerequisites
- GCC compiler
- Make build system
- Rust toolchain (optional, for highlighting)
- C++ compiler (optional, for networking)

### Build Options

```bash
# Basic build (C only)
make

# With Rust FFI highlighting
make with-rust

# With C++ FFI networking (planned)
make with-cpp

# Full-featured build (all FFI systems)
make with-all
```

### Installation

```bash
# Install system-wide
sudo make install

# Install for user
make install-user

# Termux installation
make termux-install
```

## Features

### ✅ **Core Package Management** (C)
- **.deb package extraction and installation**
- **Hash table-based package database**
- **Cascading configuration system**
- **Memory-safe operations**
- **Robust error handling**

### ✅ **Advanced Highlighting** (Rust FFI)
- **Professional syntax highlighting** using syntect
- **Multiple color themes** (nano, vim, default)
- **Script type detection** (Shell, Python, Perl, Ruby)
- **Metadata extraction** from script comments
- **Secure script execution**

### 🚧 **Network Operations** (C++ FFI - Planned)
- **Repository management** with mirror support
- **Dependency resolution** with conflict detection
- **Parallel downloads** with progress tracking
- **Package caching** and verification
- **GPG signature validation**

## 📚 Documentation

### 📖 **Core Documentation**
- [**Main README**](README.md) - This file, project overview and quick start
- [**Current State Report**](CURRENT_STATE.md) - Complete project status and roadmap
- [Build & Installation Guide](docs/BUILD.md) - Detailed build instructions *(planned)*
- [Configuration Guide](docs/CONFIGURATION.md) - Configuration system documentation *(planned)*
- [API Reference](docs/API.md) - Core C API documentation *(planned)*

### 🔧 **FFI System Documentation**
- [**Rust FFI Documentation**](README_RUST_FFI.md) - ✅ Complete Rust highlighting system
- [**C++ FFI Documentation**](README_CPP_FFI.md) - 🚧 C++ networking system (planned)
- [FFI Architecture Guide](docs/FFI_ARCHITECTURE.md) - Multi-language integration *(planned)*

### 👥 **Development Documentation**
- [Contributing Guide](docs/CONTRIBUTING.md) - How to contribute *(planned)*
- [Development Setup](docs/DEVELOPMENT.md) - Development environment *(planned)*
- [Testing Guide](docs/TESTING.md) - Testing procedures *(planned)*
- [Release Process](docs/RELEASE.md) - Release management *(planned)*

### 📋 **User Documentation**
- [User Manual](docs/USER_MANUAL.md) - Complete user guide *(planned)*
- [Troubleshooting](docs/TROUBLESHOOTING.md) - Common issues and solutions *(planned)*
- [FAQ](docs/FAQ.md) - Frequently asked questions *(planned)*

## Current State

### 🟢 **Implemented & Stable**
- ✅ Core C package management system
- ✅ Hash table-based package database
- ✅ Configuration cascade system
- ✅ .deb package extraction and processing
- ✅ Complete Rust FFI highlighting system
- ✅ Memory-safe operations with proper cleanup
- ✅ Comprehensive test suite

### 🟡 **In Development**
- 🚧 C++ FFI networking system (groundwork laid)
- 🚧 Advanced dependency resolution
- 🚧 Repository management
- 🚧 Package verification system

### 🔴 **Planned Features**
- ⏳ GUI interface
- ⏳ Plugin system
- ⏳ Package building tools
- ⏳ Automated testing framework

## Usage Examples

### Basic Package Operations
```bash
# Install a package
runepkg -i package.deb

# Show package information
runepkg --info package.deb

# List installed packages
runepkg --list

# Remove a package
runepkg --remove package-name
```

### Configuration
```bash
# Show current configuration
runepkg --print-config

# Show configuration file path
runepkg --print-config-file

# Verbose operation
runepkg -v -i package.deb
```

### With Rust FFI Features
```bash
# Highlight package scripts (when implemented)
runepkg --highlight-scripts package.deb

# Validate package scripts
runepkg --validate-scripts package.deb
```

## Build System

### Make Targets
```bash
# Core builds
make                    # Basic C build
make debug             # Debug build with symbols
make release           # Optimized release build

# FFI builds
make with-rust         # Build with Rust FFI
make with-cpp          # Build with C++ FFI (planned)
make with-all          # Build with all FFI systems

# Testing
make test              # Basic functionality tests
make test-rust         # Rust FFI tests
make test-cpp          # C++ FFI tests (planned)
make test-all          # Complete test suite

# Maintenance
make clean             # Clean C build artifacts
make clean-rust        # Clean Rust artifacts
make clean-cpp         # Clean C++ artifacts (planned)
make clean-all         # Clean everything

# Information
make info              # Show build configuration
make rust-info         # Rust FFI status
make cpp-info          # C++ FFI status (planned)
```

## Dependencies

### Core Dependencies (C)
- GCC or compatible C compiler
- Standard C library
- POSIX-compliant system
- `ar` utility (binutils)
- `tar` utility

### Rust FFI Dependencies (Optional)
- Rust toolchain (rustc, cargo)
- syntect crate
- libc crate
- once_cell crate

### C++ FFI Dependencies (Planned)
- C++17 compatible compiler
- libcurl development headers
- nlohmann::json library
- Threading support (pthread)

## Contributing

We welcome contributions! Please see our [Contributing Guide](docs/CONTRIBUTING.md) for details.

### Development Workflow
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests for new functionality
5. Ensure all tests pass
6. Submit a pull request

### Code Style
- **C Code**: Follow existing style conventions
- **Rust Code**: Use `cargo fmt` and `cargo clippy`
- **C++ Code**: Follow modern C++ best practices
- **Documentation**: Update relevant README files

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.

## Authors

- **Primary Developer**: michkochris@gmail.com
- **Project**: runepkg (Runar Linux)

## Acknowledgments

- **syntect** - Rust syntax highlighting library
- **libcurl** - HTTP/HTTPS networking (planned)
- **nlohmann::json** - Modern C++ JSON library (planned)
- The open source community for inspiration and tools

---

**Ready to manage packages like a pro! 📦⚡**
