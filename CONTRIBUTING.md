# Contributing to runepkg

Thank you for your interest in contributing to runepkg! This guide will help you get started with contributing to our security-hardened, multi-language package manager.

## 🎯 **Project Overview**

runepkg is a production-ready package manager featuring:
- **C Core System** - Memory-safe package management with defensive programming
- **Rust FFI** - Advanced syntax highlighting and secure script processing
- **C++ FFI** - High-performance networking (planned)
- **100% Test Coverage** - Comprehensive testing framework with security validation

## 🚀 **Getting Started**

### Prerequisites
- GCC compiler
- Make build system
- Rust toolchain (for FFI contributions)
- Git for version control

### Setting Up Development Environment
```bash
# Fork and clone the repository
git clone https://github.com/your-username/runepkg.git
cd runepkg

# Build with all available features
make with-all

# Run comprehensive tests to verify setup
make test-all
# Expected: 33/33 tests passing (unified test suite)
```

## 🛠️ **Development Workflow**

### 1. **Creating Issues**
- Use our issue templates for bug reports and feature requests
- Check existing issues to avoid duplicates
- Provide detailed information including system specs and test results

### 2. **Making Changes**
```bash
# Create a feature branch
git checkout -b feature/your-feature-name

# Make your changes
# ... code changes ...

# Test your changes
make test-comprehensive

# Commit with descriptive messages
git commit -m "feat: add secure path validation to config system"
```

### 3. **Pull Request Process**
- Ensure all tests pass: `make test-comprehensive`
- Update documentation if needed
- Add tests for new functionality
- Follow our code style guidelines

## 📋 **Code Style Guidelines**

### **C Code**
- Follow existing naming conventions (`runepkg_` prefix for functions)
- Use defensive programming practices
- Always validate input parameters
- Include comprehensive error handling
- Add security-focused comments

```c
// Example: Secure function signature
runepkg_error_t runepkg_secure_operation(const char *input, size_t max_len, const char *context) {
    // Validate inputs
    if (!input || !context) {
        return RUNEPKG_ERROR_NULL_POINTER;
    }
    
    // Validate size limits
    if (strlen(input) > max_len) {
        return RUNEPKG_ERROR_SIZE_LIMIT;
    }
    
    // Implementation...
}
```

### **Rust Code**
- Use `cargo fmt` for formatting
- Pass `cargo clippy` without warnings
- Follow Rust naming conventions
- Ensure memory safety in FFI boundaries

### **Documentation**
- Update relevant README files
- Add inline code comments for complex logic
- Include security considerations in documentation
- Update test documentation for new features

## 🧪 **Testing Requirements**

### **Required Testing**
All contributions must pass:
- **Simple Tests**: `make test-simple` (38/38 tests)
- **Memory Tests**: `make test-memory` (63/63 tests)
- **Security Tests**: `make test-security` (49/49 tests)

### **Adding New Tests**
```c
// Example: Adding a memory test
void test_new_feature_memory_safety(void) {
    // Setup
    char *test_data = runepkg_secure_malloc(1024);
    
    // Test the feature
    runepkg_error_t result = your_new_function(test_data);
    
    // Verify results
    test_assert(result == RUNEPKG_SUCCESS, "Function should succeed with valid input");
    
    // Cleanup
    runepkg_secure_free(&test_data);
}
```

## 🔒 **Security Guidelines**

### **Security-First Principles**
- **Input Validation**: Validate all external input
- **Memory Safety**: Use secure allocation functions
- **Error Handling**: Provide secure error messages
- **Resource Limits**: Enforce reasonable limits

### **Security Review Process**
- All security-related changes require thorough review
- Run security tests: `make test-security`
- Consider attack scenarios and edge cases
- Document security implications

## 📊 **Contribution Areas**

### **High Priority**
- **C++ FFI Development** - Networking and dependency resolution
- **Platform Support** - macOS and Windows builds
- **Security Auditing** - Third-party security reviews
- **Performance Optimization** - Profiling and bottleneck analysis

### **Medium Priority**
- **Documentation Enhancement** - User guides and API references
- **Test Coverage Expansion** - Edge cases and stress testing
- **Build System Improvements** - Cross-platform compatibility
- **Error Message Enhancement** - User-friendly error reporting

### **Future Vision**
- **GUI Interface** - Modern graphical package manager
- **Plugin System** - Extensible architecture
- **Package Building Tools** - Automated build pipeline
- **Repository Management** - Server-side infrastructure

## 🚦 **Release Process**

### **Version Numbering**
- **Major** (x.0.0): Breaking changes, new FFI systems
- **Minor** (1.x.0): New features, backward compatible
- **Patch** (1.0.x): Bug fixes, security patches

### **Release Criteria**
- 100% test pass rate on all test suites
- Security validation completed
- Documentation updated
- Cross-platform compatibility verified

## 🤝 **Community Guidelines**

### **Code of Conduct**
- Be respectful and inclusive
- Focus on constructive feedback
- Help others learn and improve
- Maintain professional communication

### **Getting Help**
- **GitHub Issues**: For bug reports and feature requests
- **Discussions**: For questions and community support
- **Email**: michkochris@gmail.com for security issues

## 📧 **Contact**

- **Primary Developer**: michkochris@gmail.com
- **Project**: runepkg (Runar Linux Package Manager)
- **License**: GNU General Public License v3.0

---

**Thank you for contributing to the future of package management! 🚀📦**
