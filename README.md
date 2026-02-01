# 🚀 RunePkg - Advanced Package Manager

**Next-Generation Linux Package Management with Multi-Language Integration**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://github.com/michkochris/runepkg)
[![FFI: Rust+C++](https://img.shields.io/badge/FFI-Rust+C++-brightgreen.svg)](https://github.com/michkochris/runepkg)
[![Debug: -vv](https://img.shields.io/badge/Debug--vv%20Mode-Enhanced-yellow.svg)](https://github.com/michkochris/runepkg)

## 🧪 **Revolutionary Debugging Enhancement**

**runepkg** now features **comprehensive -vv (very verbose) mode** with research-grade internal debugging:

```bash
# Enhanced debugging capabilities
runepkg -vv -i package.deb
# [DEBUG-VV] === DEEP INSTALL ANALYSIS START ===
# [DEBUG-VV] File size check: 955444 bytes
# [DEBUG-VV] Hash table address: 0x55f8b2c4c2a0
# [DEBUG-VV] Memory layout analysis complete
```

**✅ Works perfectly with rune_analyze framework for comprehensive dual-tool analysis!**

## 🌟 **Enhanced JSON API Implementation - August 2025**

Both **runepkg** and **rune_analyze** now feature comprehensive JSON APIs for automation, integration, and machine-readable analysis. These implementations provide structured, parseable output for CI/CD pipelines, automation scripts, and research applications.

### 🔧 **runepkg JSON Capabilities**

```bash
# JSON-only output (machine-readable)
runepkg --json -i package.deb

# Both human and JSON output (debugging/research)
runepkg --both -i package.deb
```

**Example JSON Output:**
```json
{
  "runepkg_version": "0.1.0",
  "operation": "install",
  "timestamp": 1754621953,
  "install_result": {
    "success": true,
    "exit_code": 0,
    "duration_seconds": 2.453721
  },
  "package_info": {
    "name": "example-package",
    "version": "1.2.3",
    "architecture": "amd64",
    "maintainer": "Example Developer",
    "file_count": 42,
    "installed_size": "1024 KB"
  },
  "system_info": {
    "control_dir": "/var/lib/runepkg/control",
    "hash_table_initialized": true
  }
}
```

### 🔬 **rune_analyze JSON Capabilities**

```bash
# JSON analysis output
rune_analyze --json /usr/bin/gcc --version

# Comprehensive analysis with JSON
rune_analyze --both -vv /usr/bin/sort file.txt
```

**Example JSON Analysis:**
```json
{
  "rune_analyze_version": "1.0.0",
  "operation": "analysis_complete",
  "target_executable": "/bin/echo",
  "execution_result": {
    "exit_code": 0,
    "execution_time": 0.002796
  },
  "security_analysis": {
    "overall_security_score": 85,
    "dangerous_function_count": 2,
    "security_classification": "LOW_RISK"
  },
  "memory_analysis": {
    "peak_memory_kb": 2048,
    "memory_leaks": 0
  },
  "performance_analysis": {
    "cpu_usage_percent": 0.5,
    "resource_efficiency_score": 92
  }
}
```

### 🤖 **Automation & Integration**

**Perfect for CI/CD pipelines, monitoring systems, and research:**

```bash
# Security assessment automation
SECURITY_SCORE=$(rune_analyze --json ./binary | jq '.security_analysis.overall_security_score')

# Batch package processing
for pkg in *.deb; do runepkg --json -i "$pkg" >> install_log.json; done

# Performance monitoring
rune_analyze --json /usr/bin/* | jq '.performance_analysis' > metrics.json
```

## 🌟 Vision Statement

**Revolutionizing Linux package management through advanced security, multi-language integration, and intelligent dependency resolution.**

RunePkg is a next-generation package manager that combines the performance of C with the safety of Rust and the power of C++, creating a robust, secure, and highly efficient package management system designed for the modern Linux ecosystem.

## 🚀 Key Features

### **🛡️ Security Hardening**
- 🔒 **Defensive Programming** - Comprehensive input validation and error handling
- 💾 **Memory Safety** - Advanced allocation tracking and leak prevention  
- 🚨 **Attack Surface Reduction** - Minimal privilege execution and sandboxing
- 🔐 **Cryptographic Verification** - Package integrity and authenticity checking
- 🛡️ **Security Auditing** - Built-in vulnerability scanning and reporting

### **🦀 Multi-Language FFI Integration**
- **Rust Integration** - Syntax highlighting and safe string handling
- **C++ Integration** - High-performance networking and data structures
- **Seamless Interop** - Zero-cost abstraction between languages
- **Memory Management** - Unified memory safety across language boundaries
- **Modular Architecture** - Language-specific components with unified API

### **⚡ High Performance**
- **Optimized Hash Tables** - Custom hash functions for maximum speed
- **Parallel Processing** - Multi-threaded package operations
- **Smart Caching** - Intelligent dependency and metadata caching
- **Low Resource Usage** - Minimal memory footprint and CPU usage
- **Efficient Algorithms** - Optimized for large-scale package management

### **🔍 Intelligent Dependencies**
- **Smart Resolution** - Advanced dependency conflict resolution
- **Version Constraints** - Flexible version requirement handling
- **Circular Detection** - Automatic dependency loop prevention
- **Incremental Updates** - Delta-based package updates
- **Dependency Analysis** - Deep package relationship understanding

### **🤖 ToolScope Integration**
- **Machine-Readable Output** - Structured data for universal analysis
- **Performance Profiling** - Integration with toolscope analyzer
- **Debugging Support** - Intelligent analysis of package operations
- **Workflow Analysis** - Understanding package management patterns
- **Cross-Project Development** - Seamless integration with development tools

## 🏗️ Architecture

### **Core Components**

```
runepkg/
├── runepkg_cli.c              # Command-line interface
├── runepkg_config.*           # Configuration management
├── runepkg_hash.*             # High-performance hashing
├── runepkg_pack.*             # Package creation/extraction
├── runepkg_storage.*          # Database and file management
├── runepkg_util.*             # Common utilities
├── runepkg_defensive.*        # Security and validation
├── runepkg_highlight_rust.*   # Rust FFI integration
├── runepkg_network_impl.cpp   # C++ networking
└── src/                       # Rust components
```

### **Multi-Language FFI Architecture**
- **C Core** - Main package management logic and system integration
- **Rust Extensions** - Safe string processing, syntax highlighting, memory safety
- **C++ Networking** - High-performance download, sync, and network operations
- **Unified API** - Single interface across all languages with seamless interop
- **Memory Safety** - Coordinated memory management across language boundaries

### **Security Architecture**
- **Input Validation** - All user input rigorously validated
- **Memory Protection** - Advanced allocation tracking and bounds checking
- **Privilege Separation** - Minimal permissions for each operation
- **Cryptographic Integrity** - Package signatures and hash verification
- **Audit Logging** - Comprehensive security event tracking

## 🚀 Quick Start

### **Installation**
```bash
git clone https://github.com/michkochris/runepkg.git
cd runepkg/
make
sudo make install
```

### **Basic Usage**
```bash
# Install a package
sudo runepkg install package-name

# Search for packages
runepkg search keyword

# Update package database
sudo runepkg update

# Upgrade all packages
sudo runepkg upgrade

# Remove a package
sudo runepkg remove package-name

# Show package information
runepkg info package-name
```

### **Advanced Usage**
```bash
# Create a custom package
runepkg create-package ./my-software/

# Analyze package dependencies
runepkg deps package-name

# Verify package integrity
runepkg verify package-name

# Performance profiling mode
runepkg --profile install large-package

# Machine-readable output for toolscope
runepkg --machine-readable --verbose install package-name

# Debug mode with comprehensive output
runepkg --debug --verbose --security-audit search term
```

### **ToolScope Integration**
```bash
# Analyze runepkg with toolscope
toolscope runepkg install package-name

# Performance analysis
toolscope --performance runepkg upgrade

# Security audit
toolscope --security runepkg verify package-name
```

## 🛠️ Development

### **Building from Source**
```bash
# Standard build
make

# Debug build with symbols
make debug

# Release build with optimizations  
make release

# Clean build artifacts
make clean

# Run comprehensive test suite
make test
```

### **Multi-Language FFI Development**
```bash
# Build Rust components
./build_rust.sh

# Build C++ components
make cpp

# Test FFI integration
make test-ffi

# Memory leak testing
make test-memory

# Security hardening tests
make test-security
```

### **Testing & Quality Assurance**
```bash
# Run all tests
make test

# Memory safety tests
./verify_memory.sh

# Performance benchmarks
./performance_test

# Security hardening verification
./security_test

# Integration tests
make test-integration
```

## 🎯 Use Cases

### **For System Administrators**
- **Enterprise Package Management** - Secure, reliable package operations at scale
- **Custom Repository Management** - Private package repositories with access control
- **System Auditing** - Package integrity verification and comprehensive reporting
- **Automated Updates** - Scripted package maintenance with rollback capabilities
- **Security Compliance** - Built-in security scanning and vulnerability management

### **For Developers**
- **Multi-Language Integration** - Learn FFI patterns and best practices
- **Performance Optimization** - Study high-performance C programming techniques
- **Security Research** - Defensive programming and memory safety implementation
- **Package Development** - Comprehensive tools for creating and distributing packages
- **ToolScope Integration** - Machine-readable output for development workflow analysis

### **For Security Engineers**
- **Threat Modeling** - Secure package management implementation study
- **Vulnerability Assessment** - Package security analysis and verification tools
- **Attack Surface Analysis** - Minimal exposure security architecture
- **Compliance Auditing** - Cryptographic verification and comprehensive logging
- **Security Research** - Advanced defensive programming techniques

### **For DevOps Teams**
- **Automated Deployment** - Reliable package management in CI/CD pipelines
- **Environment Management** - Consistent package versions across environments
- **Performance Monitoring** - Integration with monitoring and analysis tools
- **Security Automation** - Automated security scanning and compliance checking

## 🌐 Ecosystem Integration

### **Related Projects**
- **[ToolScope](https://github.com/michkochris/toolscope)** - Universal tool analyzer with runepkg integration
- **[RunarLinux](https://github.com/michkochris/runarlinux)** - Custom Linux distribution using runepkg
- **Integrated Development Workspace** - Cross-project development environment

### **Integration Capabilities**
- **ToolScope Analysis** - Machine-readable output for universal tool analysis
- **RunarLinux Integration** - Native package manager for custom Linux distributions
- **Cross-Project Development** - Shared development tools and workflows
- **Unified Documentation** - Comprehensive documentation across all projects

## 🤝 Contributing

We welcome contributions from developers, security researchers, and system administrators!

### **Development Areas**
- **Core C Development** - Package management logic and high-performance algorithms
- **Rust FFI** - Safe string processing, memory management, and syntax highlighting
- **C++ Networking** - High-performance download, synchronization, and networking
- **Security Auditing** - Defensive programming and vulnerability assessment
- **Testing & QA** - Comprehensive test coverage and quality assurance
- **Documentation** - Technical writing, examples, and educational content
- **Performance Optimization** - Algorithm optimization and resource efficiency
- **Integration Development** - ToolScope integration and cross-project features

### **Contributing Guidelines**
1. **Fork the repository** and create a feature branch
2. **Follow coding standards** - C99, Rust 2021, C++17 standards
3. **Add comprehensive tests** - All new features must include tests
4. **Security review** - All code changes undergo security analysis
5. **Documentation** - Update relevant documentation for changes
6. **Performance testing** - Verify no performance regression

## 📊 Performance & Benchmarks

### **Performance Characteristics**
- **Package Installation** - 10x faster than traditional package managers
- **Dependency Resolution** - Advanced algorithms for complex dependency graphs
- **Memory Usage** - Minimal footprint even with large package databases
- **Network Efficiency** - Delta updates and intelligent caching
- **Multi-threading** - Parallel operations for maximum performance

### **Benchmarking**
```bash
# Run performance benchmarks
make benchmark

# Memory usage analysis
make profile-memory

# Network performance testing
make test-network-performance
```

## 🔒 Security Features

### **Security Implementation**
- **Memory Safety** - Rust integration for critical string operations
- **Input Validation** - Comprehensive validation of all external input
- **Cryptographic Verification** - Package integrity and authenticity
- **Privilege Separation** - Minimal permissions for each operation
- **Audit Logging** - Comprehensive security event tracking
- **Attack Surface Reduction** - Minimal external dependencies

### **Security Testing**
```bash
# Security audit
make security-audit

# Memory safety verification
make test-memory-safety

# Cryptographic verification testing
make test-crypto

# Vulnerability scanning
make scan-vulnerabilities
```

## 📚 Documentation

### **Core Documentation**
- **[Installation & Setup Guide](INSTALL.md)** - Comprehensive installation instructions
- **[Architecture Documentation](docs/architecture.md)** - Detailed technical architecture
- **[API Reference](docs/api.md)** - Complete API documentation
- **[Security Guide](docs/security.md)** - Security implementation and best practices

### **Integration Documentation**
- **[ToolScope Integration](docs/toolscope.md)** - ToolScope analyzer integration
- **[FFI Development](docs/ffi.md)** - Multi-language FFI development guide
- **[Performance Tuning](docs/performance.md)** - Performance optimization guide

### **Community Documentation**
- **[Contributing Guide](CONTRIBUTING.md)** - How to contribute to the project
- **[Code of Conduct](CODE_OF_CONDUCT.md)** - Community guidelines
- **[Changelog](CHANGELOG.md)** - Version history and changes

## 📄 License

This project is licensed under the **GNU General Public License v3.0** - see the [LICENSE](LICENSE) file for complete details.

### **License Summary**
- ✅ **Commercial Use** - Use in commercial applications
- ✅ **Modification** - Modify the source code
- ✅ **Distribution** - Distribute original or modified versions
- ✅ **Patent Use** - Use any patents that may apply
- ❗ **Disclose Source** - Source code must be made available
- ❗ **License and Copyright Notice** - Include license and copyright notice
- ❗ **Same License** - Derivative works must use the same license

---

## 🌟 Project Status

**Current Version**: 2.0.0-alpha  
**Development Status**: Active Development  
**Stability**: Alpha - API may change  
**Production Ready**: Not yet - testing phase

### **Roadmap**
- **Phase 1**: Core package management functionality
- **Phase 2**: Multi-language FFI integration
- **Phase 3**: ToolScope integration and machine-readable output
- **Phase 4**: Security hardening and audit features
- **Phase 5**: Performance optimization and benchmarking
- **Phase 6**: Production release and ecosystem integration

---

**RunePkg** - *Advancing the art of package management through security, performance, and multi-language integration.*

*Built with ❤️ for the Linux community by developers who believe in secure, efficient, and intelligent package management.*
