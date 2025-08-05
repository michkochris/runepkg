# runepkg Architectural Principles

**Organic Multi-Language System Design**

## 🎯 **Core Philosophy**

runepkg is designed as an **organic system** where each programming language serves a vital, specialized role - like organs in a living body. This approach maximizes each language's strengths while maintaining a cohesive, efficient whole.

## 🏗️ **Component Roles**

### 🧠 **C Core - The Brain & Skeleton**
*The decision-making center and structural foundation*

**Responsibilities:**
- **Decision Making**: Core package management logic and algorithms
- **Structural Foundation**: Hash tables, data structures, memory layout
- **System Integration**: POSIX APIs, file system operations, process management
- **Configuration Management**: Parsing, validation, and cascading configuration
- **Memory Management**: Unified memory model with leak detection

**Standards Compliance:**
- **glibc Only**: Standard C library functions exclusively
- **POSIX Compatible**: Portable system calls and APIs
- **GCC Compatible**: Aligned with GNU compiler collection standards
- **No External Dependencies**: Self-contained using system libraries

**Design Patterns:**
```c
// Example: C Core function signature
runepkg_error_t runepkg_core_function(const char *input, 
                                      size_t max_len, 
                                      runepkg_output_t *output);
```

### 🦀 **Rust FFI - The Secure Veins**
*Safe, memory-protected data transport and validation*

**Responsibilities:**
- **Security Transport**: Safe data flow between system components
- **Input Validation**: Memory-safe processing of external data
- **Data Transformation**: Syntax highlighting, script parsing, metadata extraction
- **Memory Safety**: Preventing buffer overflows and memory corruption
- **Script Processing**: Secure execution environment for package scripts

**gccrs Compatibility Philosophy:**
- **Core Language First**: Use fundamental Rust features (ownership, borrowing, enums, structs)
- **Avoid Experimental Features**: Stay away from advanced/complex compiler internals
- **libcore Priority**: Target libcore and liballoc for maximum gccrs compatibility
- **Simple FFI Bridge**: Clean `#[no_mangle] extern "C"` interfaces only
- **GNU Ecosystem Alignment**: Designed to work seamlessly with GCC toolchain

**Design Patterns:**
```rust
// Example: Rust FFI function
#[no_mangle]
pub extern "C" fn runepkg_rust_process_data(
    input: *const c_char,
    input_len: size_t,
    output: *mut RunepkgOutput,
) -> RunepkgError {
    // Safe Rust implementation
}
```

### ⚙️ **C++ FFI - The Hands & Feet**
*Action-oriented external operations and performance tasks*

**Responsibilities:**
- **External Interaction**: Network operations, HTTP requests, repository access
- **Performance Tasks**: Parallel downloads, concurrent operations
- **Action Execution**: Package verification, dependency resolution
- **I/O Operations**: Complex file operations, streaming data processing
- **System Integration**: Advanced POSIX features, modern C++ capabilities

**Linux-Only Standards:**
- **POSIX Sockets**: Low-level networking using `<sys/socket.h>`
- **Standard C++17**: Modern C++ features without external libraries
- **glibc Integration**: Seamless integration with Linux system libraries
- **No External Dependencies**: Socket-based networking, no libcurl

**Design Patterns:**
```cpp
// Example: C++ FFI function
extern "C" runepkg_error_t runepkg_cpp_network_operation(
    const char* url,
    runepkg_network_config_t* config,
    runepkg_network_result_t* result
);
```

## 🔄 **Inter-Component Communication**

### **Data Flow Principles**
1. **C Core Orchestrates**: All major decisions flow through the C brain
2. **Rust Validates**: All external data passes through Rust security validation
3. **C++ Acts**: External actions are executed by C++ hands/feet
4. **FFI Boundaries**: Clean, well-defined interfaces between components

### **Security Model**
```
┌─────────────┐    Validated    ┌─────────────┐    Commands    ┌─────────────┐
│   External  │────────────────►│  Rust FFI   │───────────────►│   C Core    │
│    Input    │                 │ (Security)  │                │  (Brain)    │
└─────────────┘                 └─────────────┘                └─────────────┘
                                                                       │
                                                                   Actions
                                                                       ▼
                                                               ┌─────────────┐
                                                               │  C++ FFI    │
                                                               │(Hands/Feet) │
                                                               └─────────────┘
```

## 📋 **Development Guidelines**

### **Language Selection Criteria**

**Use C Core When:**
- Managing package database operations
- Implementing core algorithms and data structures
- Handling configuration and system integration
- Performing memory management and cleanup

**Use Rust FFI When:**
- Processing untrusted external input
- Performing syntax highlighting or script analysis
- Implementing security-critical data validation
- Handling complex string processing safely

**Use C++ FFI When:**
- Implementing network operations and HTTP requests
- Performing parallel or concurrent operations
- Executing complex I/O operations
- Interfacing with advanced system features

### **Dependency Policy**

**Prohibited:**
- External libraries in any component (unless explicitly justified)
- Runtime dependencies beyond system libraries
- Platform-specific code (Linux/WSL only is acceptable)

**Permitted:**
- glibc and standard C library functions
- POSIX system calls and APIs
- Rust libcore and liballoc
- C++17 standard library features
- Linux system headers and libraries

**Future Considerations:**
- Simple Linux dependencies (ncurses, basic curl) may be added with explicit justification
- All dependency additions must be documented and approved
- gccrs compatibility must be maintained for Rust components

## 🎯 **Success Metrics**

### **Architectural Goals**
- ✅ **Single Responsibility**: Each language component has a clear, distinct role
- ✅ **Clean Interfaces**: Well-defined FFI boundaries with minimal coupling
- ✅ **Security First**: All external data processed through Rust validation
- ✅ **Performance**: C++ handles computationally intensive operations
- ✅ **Maintainability**: Clear separation of concerns and documentation

### **Technical Standards**
- ✅ **100% Test Coverage**: All critical components thoroughly tested
- ✅ **Memory Safety**: No memory leaks or buffer overflows
- ✅ **Standard Compliance**: glibc, POSIX, and C++17 standards only
- ✅ **gccrs Ready**: Rust code compatible with future gccrs releases
- ✅ **Documentation**: Comprehensive guides for each architectural component

## 🚀 **Future Evolution**

As runepkg grows, the organic architecture provides clear guidance:

- **Brain Expansion (C)**: Enhanced algorithms, more sophisticated decision making
- **Vein Strengthening (Rust)**: Advanced security features, complex data validation
- **Limb Development (C++)**: More capable networking, advanced I/O operations

This architectural foundation ensures runepkg can evolve while maintaining its core principles of security, performance, and standard library compliance.

---

**The organic architecture makes runepkg more than the sum of its parts! 🧠🦀⚙️**
