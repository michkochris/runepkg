#!/bin/bash
#
# ******************************************************************************
# * Filename:    test_cpp_ffi.sh
# * Author:      <michkochris@gmail.com>
# * Date:        August 3, 2025
# * Description: Test script for runepkg C++ FFI networking system
# *
# * Copyright (c) 2025 runepkg (Runar Linux) All rights reserved.
# * GPLV3
# ******************************************************************************/

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Test configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNEPKG_BINARY="$SCRIPT_DIR/runepkg"
TEST_LOG="$SCRIPT_DIR/test_cpp_ffi.log"

# Test counters
TESTS_TOTAL=0
TESTS_PASSED=0
TESTS_FAILED=0

# Logging function
log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$TEST_LOG"
}

# Test result functions
pass() {
    echo -e "${GREEN}✓ PASS${NC}: $1"
    log "PASS: $1"
    ((TESTS_PASSED++))
}

fail() {
    echo -e "${RED}✗ FAIL${NC}: $1"
    log "FAIL: $1"
    ((TESTS_FAILED++))
}

warn() {
    echo -e "${YELLOW}⚠ WARN${NC}: $1"
    log "WARN: $1"
}

info() {
    echo -e "${BLUE}ℹ INFO${NC}: $1"
    log "INFO: $1"
}

# Test function wrapper
test_case() {
    local test_name="$1"
    shift
    ((TESTS_TOTAL++))
    
    echo -e "\n${PURPLE}Running test:${NC} $test_name"
    log "Starting test: $test_name"
    
    if "$@"; then
        pass "$test_name"
        return 0
    else
        fail "$test_name"
        return 1
    fi
}

# Check if C++ FFI is available
check_cpp_ffi_availability() {
    info "Checking C++ FFI availability..."
    
    # Check if binary exists and was built with C++ FFI
    if [ ! -f "$RUNEPKG_BINARY" ]; then
        warn "runepkg binary not found at $RUNEPKG_BINARY"
        return 1
    fi
    
    # Check if binary was compiled with C++ FFI support
    if strings "$RUNEPKG_BINARY" 2>/dev/null | grep -q "ENABLE_CPP_FFI"; then
        info "Binary compiled with C++ FFI support"
        return 0
    else
        info "Binary compiled without C++ FFI support (fallback mode)"
        return 1
    fi
}

# Test basic C++ FFI functionality
test_cpp_ffi_basic() {
    "$RUNEPKG_BINARY" --test-cpp-ffi 2>/dev/null
    return $?
}

# Test network availability check
test_network_availability() {
    info "Testing network availability check..."
    
    # This should return an error code since it's not implemented yet
    output=$("$RUNEPKG_BINARY" --check-network 2>&1) || true
    
    if echo "$output" | grep -q "not available"; then
        return 0  # Expected behavior for groundwork phase
    else
        return 1
    fi
}

# Test dependency resolution fallback
test_dependency_fallback() {
    info "Testing dependency resolution fallback..."
    
    # Create a test package scenario
    output=$("$RUNEPKG_BINARY" --resolve-deps vim 2>&1) || true
    
    if echo "$output" | grep -q "not available\|fallback"; then
        return 0  # Expected behavior for groundwork phase
    else
        return 1
    fi
}

# Test build system integration
test_build_system() {
    info "Testing build system integration..."
    
    # Check if Makefile has C++ FFI targets
    if grep -q "with-cpp\|cpp-info\|clean-cpp" "$SCRIPT_DIR/Makefile"; then
        return 0
    else
        return 1
    fi
}

# Test header files
test_header_files() {
    info "Testing C++ FFI header files..."
    
    local headers=(
        "runepkg_network_ffi.h"
    )
    
    for header in "${headers[@]}"; do
        if [ ! -f "$SCRIPT_DIR/$header" ]; then
            warn "Header file missing: $header"
            return 1
        fi
        
        # Check if header has proper include guards
        if ! grep -q "#ifndef.*#define.*#endif" "$SCRIPT_DIR/$header"; then
            warn "Header file $header may be missing include guards"
            return 1
        fi
    done
    
    return 0
}

# Test C++ dependencies
test_cpp_dependencies() {
    info "Testing C++ dependencies..."
    
    local deps_available=0
    
    # Check for g++
    if command -v g++ >/dev/null 2>&1; then
        info "g++ compiler: $(g++ --version | head -1)"
        ((deps_available++))
    else
        warn "g++ compiler not found"
    fi
    
    # Check for pkg-config
    if command -v pkg-config >/dev/null 2>&1; then
        info "pkg-config: Available"
        
        # Check for libcurl
        if pkg-config --exists libcurl 2>/dev/null; then
            info "libcurl: $(pkg-config --modversion libcurl)"
            ((deps_available++))
        else
            warn "libcurl development headers not found"
        fi
        
        # Check for OpenSSL
        if pkg-config --exists openssl 2>/dev/null; then
            info "OpenSSL: $(pkg-config --modversion openssl)"
            ((deps_available++))
        else
            warn "OpenSSL development headers not found"
        fi
    else
        warn "pkg-config not found"
    fi
    
    # Return success if we have at least g++ (minimum for groundwork)
    [ $deps_available -ge 1 ]
}

# Test FFI interface definitions
test_ffi_interface() {
    info "Testing FFI interface definitions..."
    
    # Check if header defines the essential functions
    local required_functions=(
        "cpp_download_package"
        "cpp_resolve_dependencies"
        "cpp_test_cpp_ffi"
        "cpp_get_error_string"
    )
    
    for func in "${required_functions[@]}"; do
        if ! grep -q "$func" "$SCRIPT_DIR/runepkg_network_ffi.h"; then
            warn "Function $func not found in FFI interface"
            return 1
        fi
    done
    
    return 0
}

# Test fallback implementations
test_fallback_implementations() {
    info "Testing fallback implementations..."
    
    # Check if wrapper file exists and has fallback functions
    if [ -f "$SCRIPT_DIR/runepkg_network_cpp.c" ]; then
        if grep -q "cpp_fallback_download\|CPP_ERROR_NOT_AVAILABLE" "$SCRIPT_DIR/runepkg_network_cpp.c"; then
            return 0
        else
            warn "Fallback implementations not found"
            return 1
        fi
    else
        warn "C++ FFI wrapper file not found"
        return 1
    fi
}

# Main test execution
main() {
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}  runepkg C++ FFI Test Suite${NC}"
    echo -e "${CYAN}========================================${NC}"
    
    # Initialize log
    echo "C++ FFI Test Suite - $(date)" > "$TEST_LOG"
    log "Starting C++ FFI tests..."
    
    # Check C++ FFI availability first
    if check_cpp_ffi_availability; then
        info "C++ FFI support detected"
        CPP_FFI_AVAILABLE=true
    else
        info "C++ FFI support not detected (testing groundwork)"
        CPP_FFI_AVAILABLE=false
    fi
    
    echo -e "\n${BLUE}Running groundwork tests...${NC}"
    
    # Basic groundwork tests (always run)
    test_case "Build system integration" test_build_system
    test_case "Header files present" test_header_files
    test_case "FFI interface definitions" test_ffi_interface
    test_case "Fallback implementations" test_fallback_implementations
    test_case "C++ dependencies check" test_cpp_dependencies
    
    # Conditional tests based on C++ FFI availability
    if [ "$CPP_FFI_AVAILABLE" = true ]; then
        echo -e "\n${BLUE}Running C++ FFI functionality tests...${NC}"
        test_case "Basic C++ FFI functionality" test_cpp_ffi_basic
        test_case "Network availability check" test_network_availability
        test_case "Dependency resolution" test_dependency_fallback
    else
        echo -e "\n${YELLOW}Skipping C++ FFI functionality tests (not available)${NC}"
        info "To enable C++ FFI: install g++, libcurl-dev, then run 'make with-cpp'"
    fi
    
    # Test summary
    echo -e "\n${CYAN}========================================${NC}"
    echo -e "${CYAN}  Test Results Summary${NC}"
    echo -e "${CYAN}========================================${NC}"
    
    echo -e "Total tests: ${BLUE}$TESTS_TOTAL${NC}"
    echo -e "Passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Failed: ${RED}$TESTS_FAILED${NC}"
    
    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "\n${GREEN}🎉 All tests passed!${NC}"
        if [ "$CPP_FFI_AVAILABLE" = false ]; then
            echo -e "${YELLOW}Note: C++ FFI is in groundwork phase${NC}"
            echo -e "${YELLOW}Full functionality will be available in September 2025${NC}"
        fi
    else
        echo -e "\n${RED}❌ Some tests failed${NC}"
        echo -e "Check the log file: $TEST_LOG"
    fi
    
    # Additional information
    echo -e "\n${BLUE}Next steps for C++ FFI development:${NC}"
    echo -e "  1. ${CYAN}September 2025:${NC} HTTP client implementation"
    echo -e "  2. ${CYAN}October 2025:${NC} Parallel download system"
    echo -e "  3. ${CYAN}November 2025:${NC} Advanced dependency resolution"
    echo -e "  4. ${CYAN}December 2025:${NC} Package caching and GPG verification"
    
    log "Test suite completed. Passed: $TESTS_PASSED, Failed: $TESTS_FAILED"
    
    # Exit with appropriate code
    [ $TESTS_FAILED -eq 0 ]
}

# Run the test suite
main "$@"
