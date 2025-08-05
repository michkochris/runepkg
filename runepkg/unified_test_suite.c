/**
 * runepkg Unified Test Suite
 * 
 * Single comprehensive test file that covers:
 * - Memory management and security
 * - Hash table operations  
 * - Package management
 * - Defensive programming
 * - Rust FFI integration
 * - Performance benchmarks
 * 
 * Philosophy: One file, all tests, minimal dependencies
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

// Include all runepkg headers
#include "runepkg_hash.h"
#include "runepkg_pack.h"
#include "runepkg_defensive.h"
#include "runepkg_util.h"
#include "runepkg_config.h"
#include "runepkg_storage.h"

// Include Rust FFI if available
#ifdef WITH_RUST
#include "runepkg_highlight_ffi.h"
#endif

// Global test state
static int total_tests = 0;
static int failed_tests = 0;
static bool verbose_output = false;

// Required globals for runepkg modules
bool g_verbose_mode = false;

// ============================================================================
// TEST FRAMEWORK
// ============================================================================

#define TEST_ASSERT(condition, message) do { \
    total_tests++; \
    if (!(condition)) { \
        printf("❌ FAIL: %s\n", message); \
        failed_tests++; \
    } else if (verbose_output) { \
        printf("✅ PASS: %s\n", message); \
    } \
} while(0)

#define TEST_SECTION(name) do { \
    printf("\n🧪 === %s ===\n", name); \
} while(0)

static double get_time_diff(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
}

// ============================================================================
// MEMORY AND SECURITY TESTS
// ============================================================================

void test_memory_security() {
    TEST_SECTION("Memory Management & Security");
    
    // Test 1: Basic PkgInfo lifecycle
    PkgInfo pkg;
    runepkg_pack_init_package_info(&pkg);
    TEST_ASSERT(pkg.package_name == NULL, "PkgInfo initialized to NULL");
    TEST_ASSERT(pkg.file_count == 0, "PkgInfo file_count initialized to 0");
    
    // Test 2: Secure memory allocation
    pkg.package_name = runepkg_secure_strdup("test-package");
    pkg.version = runepkg_secure_strdup("1.0.0");
    TEST_ASSERT(pkg.package_name != NULL, "Secure strdup works");
    TEST_ASSERT(strcmp(pkg.package_name, "test-package") == 0, "Secure strdup content correct");
    
    // Test 3: Buffer overflow protection
    runepkg_error_t result = runepkg_validate_string("normal", 10, "test");
    TEST_ASSERT(result == RUNEPKG_SUCCESS, "Normal string validates");
    
    result = runepkg_validate_string(NULL, 10, "null_test");
    TEST_ASSERT(result != RUNEPKG_SUCCESS, "NULL string rejected");
    
    // Test 4: Path traversal protection
    result = runepkg_validate_path("../../../etc/passwd");
    TEST_ASSERT(result != RUNEPKG_SUCCESS, "Path traversal blocked");
    
    result = runepkg_validate_path("/safe/normal/path");
    TEST_ASSERT(result == RUNEPKG_SUCCESS, "Normal path accepted");
    
    // Test 5: Memory cleanup
    runepkg_pack_free_package_info(&pkg);
    TEST_ASSERT(pkg.package_name == NULL, "Memory cleaned up correctly");
    
    printf("Memory & Security: %d tests completed\n", 8);
}

// ============================================================================
// HASH TABLE TESTS
// ============================================================================

void test_hash_operations() {
    TEST_SECTION("Hash Table Operations");
    
    // Test 1: Create hash table
    runepkg_hash_table_t *table = runepkg_hash_create_table(16);
    TEST_ASSERT(table != NULL, "Hash table created");
    
    // Test 2: Add packages
    PkgInfo pkg1, pkg2;
    runepkg_pack_init_package_info(&pkg1);
    runepkg_pack_init_package_info(&pkg2);
    
    pkg1.package_name = runepkg_secure_strdup("package-one");
    pkg1.version = runepkg_secure_strdup("1.0.0");
    
    pkg2.package_name = runepkg_secure_strdup("package-two");
    pkg2.version = runepkg_secure_strdup("2.0.0");
    
    int add_result1 = runepkg_hash_add_package(table, &pkg1);
    int add_result2 = runepkg_hash_add_package(table, &pkg2);
    TEST_ASSERT(add_result1 == 0, "First package added successfully");
    TEST_ASSERT(add_result2 == 0, "Second package added successfully");
    
    // Test 3: Search packages
    PkgInfo *found1 = runepkg_hash_search(table, "package-one");
    PkgInfo *found2 = runepkg_hash_search(table, "package-two");
    PkgInfo *not_found = runepkg_hash_search(table, "nonexistent");
    
    TEST_ASSERT(found1 != NULL, "First package found");
    TEST_ASSERT(found2 != NULL, "Second package found");
    TEST_ASSERT(not_found == NULL, "Nonexistent package not found");
    
    if (found1) {
        TEST_ASSERT(strcmp(found1->package_name, "package-one") == 0, "Found package name correct");
    }
    
    // Test 4: Remove packages
    runepkg_hash_remove_package(table, "package-one");
    PkgInfo *removed = runepkg_hash_search(table, "package-one");
    TEST_ASSERT(removed == NULL, "Package removed successfully");
    
    // Test 5: Cleanup
    runepkg_pack_free_package_info(&pkg1);
    runepkg_pack_free_package_info(&pkg2);
    runepkg_hash_destroy_table(table);
    
    printf("Hash Operations: %d tests completed\n", 8);
}

// ============================================================================
// PERFORMANCE BENCHMARKS
// ============================================================================

void test_performance() {
    TEST_SECTION("Performance Benchmarks");
    
    struct timeval start, end;
    
    // Test 1: Hash table performance
    gettimeofday(&start, NULL);
    runepkg_hash_table_t *table = runepkg_hash_create_table(64);
    
    // Add 1000 packages
    for (int i = 0; i < 1000; i++) {
        PkgInfo pkg;
        runepkg_pack_init_package_info(&pkg);
        
        char name[64], version[16];
        snprintf(name, sizeof(name), "benchmark-package-%d", i);
        snprintf(version, sizeof(version), "1.%d.0", i);
        
        pkg.package_name = runepkg_secure_strdup(name);
        pkg.version = runepkg_secure_strdup(version);
        
        runepkg_hash_add_package(table, &pkg);
        runepkg_pack_free_package_info(&pkg);
    }
    
    gettimeofday(&end, NULL);
    double hash_time = get_time_diff(start, end);
    TEST_ASSERT(hash_time < 1.0, "Hash operations under 1 second");
    printf("📊 Hash performance: 1000 packages in %.3f seconds\n", hash_time);
    
    // Test 2: Search performance
    gettimeofday(&start, NULL);
    int found_count = 0;
    for (int i = 0; i < 1000; i++) {
        char name[64];
        snprintf(name, sizeof(name), "benchmark-package-%d", i);
        if (runepkg_hash_search(table, name)) {
            found_count++;
        }
    }
    gettimeofday(&end, NULL);
    double search_time = get_time_diff(start, end);
    
    TEST_ASSERT(found_count == 1000, "All packages found in search");
    TEST_ASSERT(search_time < 0.1, "Search performance under 100ms");
    printf("📊 Search performance: 1000 searches in %.3f seconds\n", search_time);
    
    // Test 3: Memory allocation performance
    gettimeofday(&start, NULL);
    for (int i = 0; i < 10000; i++) {
        char *test_str = runepkg_secure_strdup("performance-test-string");
        runepkg_secure_free((void**)&test_str, strlen("performance-test-string") + 1);
    }
    gettimeofday(&end, NULL);
    double alloc_time = get_time_diff(start, end);
    
    TEST_ASSERT(alloc_time < 0.5, "Memory allocation performance acceptable");
    printf("📊 Memory allocation: 10000 alloc/free in %.3f seconds\n", alloc_time);
    
    runepkg_hash_destroy_table(table);
    printf("Performance: %d tests completed\n", 3);
}

// ============================================================================
// RUST FFI TESTS
// ============================================================================

#ifdef WITH_RUST
void test_rust_ffi() {
    TEST_SECTION("Rust FFI Integration");
    
    // Test 1: Basic FFI connection
    int ffi_result = rust_test_ffi();
    TEST_ASSERT(ffi_result == 42, "Rust FFI basic test works");
    
    // Test 2: Initialization
    int init_result = rust_init_highlighting();
    TEST_ASSERT(init_result == 1, "Rust highlighting initialized");
    
    // Test 3: Version information
    const char *version = rust_get_version();
    TEST_ASSERT(version != NULL, "Rust version string available");
    TEST_ASSERT(strstr(version, "runepkg-highlight") != NULL, "Version contains expected text");
    
    // Test 4: Theme system
    int theme_count = rust_get_theme_count();
    TEST_ASSERT(theme_count == 3, "Expected number of themes available");
    
    const char *theme0 = rust_get_theme_name(0);
    const char *theme1 = rust_get_theme_name(1);
    TEST_ASSERT(theme0 != NULL, "First theme name available");
    TEST_ASSERT(theme1 != NULL, "Second theme name available");
    
    // Test 5: Highlighting functionality
    const char *test_script = "#!/bin/bash\necho \"Hello World\"\n# Comment\n";
    char *highlighted = (char*)rust_highlight_shell_script(test_script, strlen(test_script), 0);
    
    TEST_ASSERT(highlighted != NULL, "Highlighting function returns result");
    if (highlighted) {
        TEST_ASSERT(strlen(highlighted) > strlen(test_script), "Highlighted text is longer (contains colors)");
        rust_free_highlighted_string(highlighted);
    }
    
    printf("Rust FFI: %d tests completed\n", 7);
}
#endif

// ============================================================================
// STRESS TESTS
// ============================================================================

void test_stress() {
    TEST_SECTION("Stress Testing");
    
    // Test 1: Large hash table
    runepkg_hash_table_t *big_table = runepkg_hash_create_table(1024);
    TEST_ASSERT(big_table != NULL, "Large hash table created");
    
    // Test 2: Memory stress
    for (int i = 0; i < 5000; i++) {
        PkgInfo pkg;
        runepkg_pack_init_package_info(&pkg);
        
        char name[64];
        snprintf(name, sizeof(name), "stress-package-%d", i);
        pkg.package_name = runepkg_secure_strdup(name);
        
        int result = runepkg_hash_add_package(big_table, &pkg);
        if (i < 10) { // Only check first few to avoid spam
            TEST_ASSERT(result == 0, "Stress package added");
        }
        
        runepkg_pack_free_package_info(&pkg);
    }
    
    // Test 3: Search stress
    int stress_found = 0;
    for (int i = 0; i < 1000; i += 100) {
        char name[64];
        snprintf(name, sizeof(name), "stress-package-%d", i);
        if (runepkg_hash_search(big_table, name)) {
            stress_found++;
        }
    }
    TEST_ASSERT(stress_found == 10, "Stress search found expected packages");
    
    runepkg_hash_destroy_table(big_table);
    printf("Stress Testing: %d tests completed\n", 3);
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

void print_help() {
    printf("runepkg Unified Test Suite\n");
    printf("Usage: %s [options]\n", "test_suite");
    printf("Options:\n");
    printf("  -v, --verbose    Verbose output\n");
    printf("  -q, --quick      Quick tests only\n");
    printf("  -h, --help       Show this help\n");
}

int main(int argc, char *argv[]) {
    bool quick_mode = false;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose_output = true;
            g_verbose_mode = true;
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quick") == 0) {
            quick_mode = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        }
    }
    
    printf("🚀 runepkg Unified Test Suite\n");
    printf("===============================\n");
    
    struct timeval suite_start, suite_end;
    gettimeofday(&suite_start, NULL);
    
    // Run all test suites
    test_memory_security();
    test_hash_operations();
    
    if (!quick_mode) {
        test_performance();
        test_stress();
    }
    
#ifdef WITH_RUST
    test_rust_ffi();
#else
    printf("\n🦀 Rust FFI: DISABLED (compile with WITH_RUST=1)\n");
#endif
    
    gettimeofday(&suite_end, NULL);
    double total_time = get_time_diff(suite_start, suite_end);
    
    // Final results
    printf("\n==================================================\n");
    printf("📊 TEST SUITE RESULTS\n");
    printf("=====================\n");
    printf("Total tests:    %d\n", total_tests);
    printf("Passed tests:   %d\n", total_tests - failed_tests);
    printf("Failed tests:   %d\n", failed_tests);
    printf("Success rate:   %.1f%%\n", ((double)(total_tests - failed_tests) / total_tests) * 100);
    printf("Execution time: %.3f seconds\n", total_time);
    
    if (failed_tests == 0) {
        printf("\n🎉 ALL TESTS PASSED! System is ready for production.\n");
        return 0;
    } else {
        printf("\n❌ %d TESTS FAILED! Please review and fix issues.\n", failed_tests);
        return 1;
    }
}
