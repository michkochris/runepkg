# C++ FFI Documentation - runepkg

**Advanced networking and dependency resolution system for runepkg**

## Overview

The C++ FFI system provides runepkg with sophisticated networking capabilities, leveraging modern C++ standard library features and established libraries to handle complex package management operations.

## Architecture

### Core Components

```cpp
┌─────────────────────────────────────────────────────────────┐
│                    C++ FFI Architecture                     │
├─────────────────────┬─────────────────────┬─────────────────┤
│   Network Layer     │  Dependency Engine  │   Cache System  │
│                     │                     │                 │
│ • HTTP/HTTPS Client │ • Dependency Graph  │ • Package Cache │
│ • Progress Tracking │ • Conflict Detection│ • Metadata Cache│
│ • Mirror Management │ • Version Resolution│ • Integrity     │
│ • Retry Logic       │ • Circular Deps     │   Verification  │
│ • Bandwidth Control │ • Update Strategies │ • Compression   │
└─────────────────────┴─────────────────────┴─────────────────┘
```

### Technology Stack

- **C++17 Standard**: Modern language features (std::filesystem, std::optional)
- **libcurl**: Industrial-strength HTTP/HTTPS networking
- **nlohmann::json**: Modern C++ JSON parsing and generation
- **OpenSSL**: Cryptographic operations and GPG verification
- **Threading**: std::thread, std::async for parallel operations

## Planned Features

### 🚧 Network Operations
- **Repository Management**
  - Multiple repository support
  - Mirror failover and load balancing
  - Repository metadata caching
  - Bandwidth throttling and progress tracking

- **Package Downloads**
  - Parallel downloading with thread pool
  - Resume interrupted downloads
  - Checksum verification during download
  - GPG signature validation

### 🚧 Dependency Resolution
- **Advanced Graph Algorithms**
  - Topological sorting for install order
  - Circular dependency detection
  - Conflict resolution strategies
  - Version constraint solving

- **Package Analysis**
  - Deep dependency scanning
  - Size calculation and disk space checking
  - Compatibility verification
  - Security vulnerability scanning

### 🚧 Caching System
- **Intelligent Caching**
  - LRU cache for package metadata
  - Compressed package storage
  - Incremental updates
  - Cache cleanup and optimization

## File Structure (Planned)

```
runepkg/
├── include/
│   ├── runepkg_network_ffi.h      # C FFI interface
│   ├── runepkg_network_cpp.hpp    # C++ class declarations
│   ├── runepkg_dependency.hpp     # Dependency resolution
│   ├── runepkg_cache.hpp          # Cache management
│   └── runepkg_repository.hpp     # Repository handling
├── src/
│   ├── network/
│   │   ├── http_client.cpp        # libcurl wrapper
│   │   ├── progress_tracker.cpp   # Download progress
│   │   ├── mirror_manager.cpp     # Repository mirrors
│   │   └── bandwidth_limiter.cpp  # Network throttling
│   ├── dependency/
│   │   ├── graph_resolver.cpp     # Dependency graph
│   │   ├── conflict_detector.cpp  # Conflict resolution
│   │   ├── version_solver.cpp     # Version constraints
│   │   └── circular_detector.cpp  # Circular dependencies
│   ├── cache/
│   │   ├── package_cache.cpp      # Package caching
│   │   ├── metadata_cache.cpp     # Metadata storage
│   │   ├── integrity_checker.cpp  # Verification
│   │   └── compression_manager.cpp # Cache compression
│   └── runepkg_cpp_ffi.cpp        # Main FFI interface
├── tests/
│   ├── test_network.cpp           # Network functionality
│   ├── test_dependency.cpp        # Dependency resolution
│   ├── test_cache.cpp             # Cache operations
│   └── test_integration.cpp       # Full integration
├── Makefile.cpp                   # C++ build rules
└── README_CPP_FFI.md             # This file
```

## FFI Interface Design

### C Function Bindings

```c
// Network operations
int cpp_download_package(const char* url, const char* output_path, 
                        progress_callback_t callback);
int cpp_download_parallel(const char* urls[], int count, 
                         const char* output_dir);
int cpp_verify_signature(const char* package_path, const char* sig_path);

// Repository management
int cpp_update_repository_metadata(const char* repo_url);
int cpp_search_packages(const char* query, search_result_t** results);
int cpp_get_package_info(const char* package_name, package_info_t* info);

// Dependency resolution
int cpp_resolve_dependencies(const char* package_name, 
                            dependency_list_t** deps);
int cpp_check_conflicts(const char* packages[], int count);
int cpp_calculate_install_order(const char* packages[], int count,
                               install_order_t** order);

// Cache management
int cpp_initialize_cache(const char* cache_dir);
int cpp_cache_package(const char* package_path);
int cpp_get_cached_package(const char* package_name, char* output_path);
int cpp_cleanup_cache(cache_cleanup_policy_t policy);
```

### Error Handling

```c
typedef enum {
    CPP_SUCCESS = 0,
    CPP_ERROR_NETWORK = -1,
    CPP_ERROR_DEPENDENCY = -2,
    CPP_ERROR_CACHE = -3,
    CPP_ERROR_SIGNATURE = -4,
    CPP_ERROR_MEMORY = -5,
    CPP_ERROR_FILE_IO = -6,
    CPP_ERROR_JSON_PARSE = -7,
    CPP_ERROR_THREAD = -8
} cpp_ffi_error_t;
```

## Build System Integration

### Makefile Rules (Planned)

```makefile
# C++ FFI detection
HAS_CPP := $(shell which g++ >/dev/null 2>&1 && echo yes || echo no)
HAS_LIBCURL := $(shell pkg-config --exists libcurl 2>/dev/null && echo yes || echo no)
HAS_NLOHMANN_JSON := $(shell find /usr -name "nlohmann" -type d 2>/dev/null | head -1)

# C++ FFI build flags
CPP_CXXFLAGS := -std=c++17 -Wall -Wextra -O2
CPP_LDFLAGS := -lcurl -lssl -lcrypto -lpthread

# Conditional compilation
ifeq ($(HAS_CPP),yes)
    ifeq ($(HAS_LIBCURL),yes)
        ENABLE_CPP_FFI := yes
        CFLAGS += -DENABLE_CPP_FFI
        CPP_SOURCES := $(wildcard src/*.cpp src/*/*.cpp)
        CPP_OBJECTS := $(CPP_SOURCES:.cpp=.o)
    endif
endif

# Build targets
with-cpp: check-cpp-deps $(TARGET)
	@echo "Built with C++ FFI networking support"

check-cpp-deps:
ifeq ($(ENABLE_CPP_FFI),yes)
	@echo "✓ C++ FFI enabled"
	@echo "  - C++ compiler: $(shell which g++)"
	@echo "  - libcurl: $(shell pkg-config --modversion libcurl)"
	@echo "  - OpenSSL: $(shell pkg-config --modversion openssl)"
else
	@echo "✗ C++ FFI disabled (missing dependencies)"
	@echo "  Install: g++ libcurl4-openssl-dev nlohmann-json3-dev"
endif
```

## Usage Examples (Planned)

### Repository Operations
```c
#include "runepkg_network_ffi.h"

// Update repository metadata
if (cpp_update_repository_metadata("https://packages.runar.org") != CPP_SUCCESS) {
    fprintf(stderr, "Failed to update repository\n");
    return -1;
}

// Search for packages
search_result_t* results;
int count = cpp_search_packages("text-editor", &results);
for (int i = 0; i < count; i++) {
    printf("Package: %s - %s\n", results[i].name, results[i].description);
}
```

### Dependency Resolution
```c
// Resolve dependencies for a package
dependency_list_t* deps;
if (cpp_resolve_dependencies("vim", &deps) == CPP_SUCCESS) {
    printf("Dependencies for vim:\n");
    for (int i = 0; i < deps->count; i++) {
        printf("  %s (>= %s)\n", deps->packages[i].name, deps->packages[i].version);
    }
}

// Check for conflicts
const char* packages[] = {"vim", "emacs", "nano"};
if (cpp_check_conflicts(packages, 3) != CPP_SUCCESS) {
    printf("Warning: Package conflicts detected\n");
}
```

### Parallel Downloads
```c
// Download multiple packages in parallel
const char* urls[] = {
    "https://packages.runar.org/vim_8.2-1_amd64.deb",
    "https://packages.runar.org/git_2.34-1_amd64.deb",
    "https://packages.runar.org/curl_7.81-1_amd64.deb"
};

if (cpp_download_parallel(urls, 3, "/var/cache/runepkg") == CPP_SUCCESS) {
    printf("All packages downloaded successfully\n");
}
```

## Development Timeline

### Phase 1: Foundation (4-6 weeks)
- [ ] Basic HTTP client wrapper around libcurl
- [ ] Simple progress tracking and error handling
- [ ] JSON parsing for repository metadata
- [ ] Basic C FFI interface

### Phase 2: Core Networking (6-8 weeks)
- [ ] Mirror management and failover
- [ ] Parallel download system with thread pool
- [ ] Resume interrupted downloads
- [ ] Bandwidth limiting and throttling

### Phase 3: Dependency System (8-10 weeks)
- [ ] Dependency graph construction
- [ ] Topological sorting for install order
- [ ] Conflict detection and resolution
- [ ] Version constraint solving

### Phase 4: Advanced Features (6-8 weeks)
- [ ] Package caching with compression
- [ ] GPG signature verification
- [ ] Security vulnerability scanning
- [ ] Performance optimization

### Phase 5: Integration & Testing (4-6 weeks)
- [ ] Complete C integration
- [ ] Comprehensive test suite
- [ ] Documentation and examples
- [ ] Performance benchmarking

## Dependencies

### Required Libraries
```bash
# Ubuntu/Debian
sudo apt-get install g++ libcurl4-openssl-dev nlohmann-json3-dev libssl-dev

# CentOS/RHEL
sudo yum install gcc-c++ libcurl-devel nlohmann-json-devel openssl-devel

# Arch Linux
sudo pacman -S gcc curl nlohmann-json openssl

# macOS (Homebrew)
brew install curl nlohmann-json openssl
```

### Optional Dependencies
- **libgpgme**: For GPG signature verification
- **liblzma**: For XZ compression support
- **zlib**: For GZIP compression
- **benchmark**: For performance testing

## Testing Strategy

### Unit Tests
- Network operations with mock servers
- Dependency resolution with synthetic packages
- Cache operations with temporary directories
- JSON parsing with malformed data

### Integration Tests
- Full download-install-verify cycles
- Complex dependency scenarios
- Repository failover testing
- Performance benchmarks

### Stress Tests
- Large dependency trees (1000+ packages)
- Concurrent download limits
- Memory usage under load
- Network failure recovery

## Performance Goals

### Network Performance
- **Parallel Downloads**: 4-8 concurrent connections
- **Bandwidth Efficiency**: 95%+ utilization
- **Resume Success Rate**: >98% for interrupted downloads
- **Mirror Failover**: <5 second detection and switch

### Dependency Resolution
- **Small Trees**: <100ms for packages with <20 dependencies
- **Medium Trees**: <1s for packages with <100 dependencies
- **Large Trees**: <10s for packages with <1000 dependencies
- **Memory Usage**: <50MB for dependency graphs

### Cache Performance
- **Hit Rate**: >80% for repeated operations
- **Compression Ratio**: >60% space savings
- **Access Time**: <10ms for cached metadata
- **Cleanup Efficiency**: >95% obsolete data removal

## Security Considerations

### Network Security
- **TLS Verification**: Strict certificate validation
- **Signature Verification**: GPG validation for all packages
- **Hash Verification**: SHA256 checksums for integrity
- **Rate Limiting**: Protection against DoS attacks

### Memory Safety
- **RAII Principles**: Automatic resource management
- **Bounds Checking**: Vector and string safety
- **Exception Safety**: Strong exception guarantees
- **Smart Pointers**: Automatic memory management

## Contributing

### Code Style
```cpp
// Use modern C++ features
auto packages = std::make_unique<PackageList>();

// Prefer const and references
void process_package(const Package& pkg);

// Use range-based loops
for (const auto& dependency : package.dependencies()) {
    // Process dependency
}

// Handle errors explicitly
if (auto result = download_package(url); !result) {
    return handle_error(result.error());
}
```

### Testing Guidelines
- Write unit tests for all public functions
- Use Google Test framework for C++ tests
- Mock external dependencies (network, filesystem)
- Achieve >90% code coverage

### Documentation
- Document all public APIs with Doxygen
- Provide usage examples for complex functions
- Update README for new features
- Include performance characteristics

---

**Advanced networking for next-generation package management! 🌐📦**
