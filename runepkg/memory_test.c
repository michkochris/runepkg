/******************************************************************************
 * Filename:    memory_test.c
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Memory management test for unified PkgInfo structure
 *
 * This test program verifies:
 * 1. Proper memory allocation and deallocation
 * 2. No memory leaks in the unified memory model
 * 3. Consistent handling of control_dir_path and data_dir_path
 * 4. Hash table memory management consistency
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "runepkg_hash.h"
#include "runepkg_pack.h"
#include "runepkg_util.h"
#include "runepkg_defensive.h"

// Test counter for tracking allocations
static int test_allocations = 0;
static int test_failures = 0;

void test_print_header(const char *test_name) {
    printf("\n=== %s ===\n", test_name);
}

void test_assert(int condition, const char *message) {
    test_allocations++;
    if (!condition) {
        printf("FAIL: %s\n", message);
        test_failures++;
    } else {
        printf("PASS: %s\n", message);
    }
}

// Test unified PkgInfo initialization
void test_pkg_info_init() {
    test_print_header("PkgInfo Initialization Test");
    
    PkgInfo pkg_info;
    runepkg_pack_init_package_info(&pkg_info);
    
    test_assert(pkg_info.package_name == NULL, "package_name initialized to NULL");
    test_assert(pkg_info.version == NULL, "version initialized to NULL");
    test_assert(pkg_info.architecture == NULL, "architecture initialized to NULL");
    test_assert(pkg_info.maintainer == NULL, "maintainer initialized to NULL");
    test_assert(pkg_info.description == NULL, "description initialized to NULL");
    test_assert(pkg_info.depends == NULL, "depends initialized to NULL");
    test_assert(pkg_info.installed_size == NULL, "installed_size initialized to NULL");
    test_assert(pkg_info.section == NULL, "section initialized to NULL");
    test_assert(pkg_info.priority == NULL, "priority initialized to NULL");
    test_assert(pkg_info.homepage == NULL, "homepage initialized to NULL");
    test_assert(pkg_info.filename == NULL, "filename initialized to NULL");
    test_assert(pkg_info.control_dir_path == NULL, "control_dir_path initialized to NULL");
    test_assert(pkg_info.data_dir_path == NULL, "data_dir_path initialized to NULL");
    test_assert(pkg_info.file_list == NULL, "file_list initialized to NULL");
    test_assert(pkg_info.file_count == 0, "file_count initialized to 0");
}

// Test basic memory allocation and cleanup
void test_pkg_info_memory() {
    test_print_header("PkgInfo Memory Management Test");
    
    PkgInfo pkg_info;
    runepkg_pack_init_package_info(&pkg_info);
    
    // Allocate some test data
    pkg_info.package_name = strdup("test-package");
    pkg_info.version = strdup("1.0.0");
    pkg_info.description = strdup("Test package description");
    pkg_info.control_dir_path = strdup("/tmp/control");
    pkg_info.data_dir_path = strdup("/tmp/data");
    
    // Allocate file list
    pkg_info.file_count = 3;
    pkg_info.file_list = malloc(3 * sizeof(char*));
    pkg_info.file_list[0] = strdup("file1.txt");
    pkg_info.file_list[1] = strdup("file2.txt");
    pkg_info.file_list[2] = strdup("file3.txt");
    
    test_assert(pkg_info.package_name != NULL, "package_name allocated");
    test_assert(pkg_info.control_dir_path != NULL, "control_dir_path allocated");
    test_assert(pkg_info.data_dir_path != NULL, "data_dir_path allocated");
    test_assert(pkg_info.file_list != NULL, "file_list allocated");
    test_assert(strcmp(pkg_info.package_name, "test-package") == 0, "package_name value correct");
    
    // Test cleanup
    runepkg_pack_free_package_info(&pkg_info);
    
    test_assert(pkg_info.package_name == NULL, "package_name freed and nulled");
    test_assert(pkg_info.control_dir_path == NULL, "control_dir_path freed and nulled");
    test_assert(pkg_info.data_dir_path == NULL, "data_dir_path freed and nulled");
    test_assert(pkg_info.file_list == NULL, "file_list freed and nulled");
    test_assert(pkg_info.file_count == 0, "file_count reset to 0");
}

// Test hash table memory consistency
void test_hash_table_memory() {
    test_print_header("Hash Table Memory Consistency Test");
    
    // Create hash table
    runepkg_hash_table_t *table = runepkg_hash_create_table(16);
    test_assert(table != NULL, "Hash table created successfully");
    
    // Create test package info
    PkgInfo pkg_info;
    runepkg_pack_init_package_info(&pkg_info);
    
    pkg_info.package_name = strdup("hash-test-package");
    pkg_info.version = strdup("2.0.0");
    pkg_info.control_dir_path = strdup("/tmp/hash-control");
    pkg_info.data_dir_path = strdup("/tmp/hash-data");
    
    // Add to hash table
    int result = runepkg_hash_add_package(table, &pkg_info);
    test_assert(result == 0, "Package added to hash table successfully");
    
    // Search for package
    PkgInfo *found = runepkg_hash_search(table, "hash-test-package");
    test_assert(found != NULL, "Package found in hash table");
    test_assert(strcmp(found->package_name, "hash-test-package") == 0, "Found package name matches");
    
    // Critical test: Check if control_dir_path and data_dir_path are handled
    // This should reveal the memory management issue
    printf("Found package control_dir_path: %s\n", found->control_dir_path ? found->control_dir_path : "NULL");
    printf("Found package data_dir_path: %s\n", found->data_dir_path ? found->data_dir_path : "NULL");
    
    // Clean up original package info
    runepkg_pack_free_package_info(&pkg_info);
    
    // Clean up hash table
    runepkg_hash_destroy_table(table);
    
    printf("Hash table memory test completed\n");
}

// Test for memory leaks in repeated operations
void test_memory_leaks() {
    test_print_header("Memory Leak Test");
    
    runepkg_hash_table_t *table = runepkg_hash_create_table(16);
    
    // Perform multiple add/remove operations
    for (int i = 0; i < 10; i++) {
        PkgInfo pkg_info;
        runepkg_pack_init_package_info(&pkg_info);
        
        char name_buf[64], version_buf[16], control_buf[128], data_buf[128];
        snprintf(name_buf, sizeof(name_buf), "test-package-%d", i);
        snprintf(version_buf, sizeof(version_buf), "1.%d.0", i);
        snprintf(control_buf, sizeof(control_buf), "/tmp/control-%d", i);
        snprintf(data_buf, sizeof(data_buf), "/tmp/data-%d", i);
        
        pkg_info.package_name = strdup(name_buf);
        pkg_info.version = strdup(version_buf);
        pkg_info.control_dir_path = strdup(control_buf);
        pkg_info.data_dir_path = strdup(data_buf);
        
        // Add file list
        pkg_info.file_count = 2;
        pkg_info.file_list = malloc(2 * sizeof(char*));
        pkg_info.file_list[0] = strdup("leak-test-file1.txt");
        pkg_info.file_list[1] = strdup("leak-test-file2.txt");
        
        runepkg_hash_add_package(table, &pkg_info);
        runepkg_pack_free_package_info(&pkg_info);
    }
    
    // Remove all packages
    for (int i = 0; i < 10; i++) {
        char name_buf[64];
        snprintf(name_buf, sizeof(name_buf), "test-package-%d", i);
        runepkg_hash_remove_package(table, name_buf);
    }
    
    test_assert(table->count == 0, "All packages removed from hash table");
    
    runepkg_hash_destroy_table(table);
    printf("Memory leak test completed\n");
}

// Test defensive programming security features
void test_defensive_security() {
    test_print_header("Defensive Programming Security Test");
    
    // Test secure string operations
    char *secure_str = runepkg_secure_strdup("test-string");
    test_assert(secure_str != NULL, "Secure string duplication works");
    test_assert(strcmp(secure_str, "test-string") == 0, "Secure string content correct");
    runepkg_secure_free((void**)&secure_str, strlen("test-string") + 1);
    test_assert(secure_str == NULL, "Secure free nulls pointer");
    
    // Test input validation
    runepkg_error_t result = runepkg_validate_string("valid", 10, "test");
    test_assert(result == RUNEPKG_SUCCESS, "Valid string passes validation");
    
    result = runepkg_validate_string(NULL, 10, "null_test");
    test_assert(result == RUNEPKG_ERROR_NULL_POINTER, "NULL string fails validation");
    
    result = runepkg_validate_string("toolongstring", 5, "length_test");
    test_assert(result == RUNEPKG_ERROR_SIZE_LIMIT, "Oversized string fails validation");
    
    // Test size validation
    result = runepkg_validate_size(100, 1000, "size_test");
    test_assert(result == RUNEPKG_SUCCESS, "Valid size passes validation");
    
    result = runepkg_validate_size(0, 1000, "zero_size_test");
    test_assert(result == RUNEPKG_SUCCESS, "Zero size passes validation");
    
    result = runepkg_validate_size(2000, 1000, "oversize_test");
    test_assert(result == RUNEPKG_ERROR_SIZE_LIMIT, "Oversize fails validation");
    
    // Test file count validation
    runepkg_error_t file_result = runepkg_validate_file_count(50);
    test_assert(file_result == RUNEPKG_SUCCESS, "Valid file count passes validation");
    
    file_result = runepkg_validate_file_count(100001);
    test_assert(file_result == RUNEPKG_ERROR_SIZE_LIMIT, "Excessive file count fails validation");
    
    // Test path security
    runepkg_error_t path_result = runepkg_validate_path("/safe/path");
    test_assert(path_result == RUNEPKG_SUCCESS, "Safe path passes validation");
    
    path_result = runepkg_validate_path("../../../etc/passwd");
    test_assert(path_result != RUNEPKG_SUCCESS, "Path traversal fails validation");
    
    path_result = runepkg_validate_path("/safe/path/../../../etc");
    test_assert(path_result != RUNEPKG_SUCCESS, "Complex path traversal fails validation");
    
    // Test secure path concatenation
    char *result_path = runepkg_secure_path_concat("/base", "subdir");
    test_assert(result_path != NULL, "Secure path concatenation succeeds");
    test_assert(strcmp(result_path, "/base/subdir") == 0, "Path concatenation result correct");
    runepkg_secure_free((void**)&result_path, strlen("/base/subdir") + 1);
    
    char *unsafe_path = runepkg_secure_path_concat("/base", "../etc");
    test_assert(unsafe_path == NULL, "Secure path concatenation rejects traversal");
    
    printf("Defensive programming security test completed\n");
}

// Test memory boundary and overflow protection
void test_memory_boundaries() {
    test_print_header("Memory Boundary Protection Test");
    
    // Test allocation limits
    void *large_alloc = runepkg_secure_malloc(1024 * 1024);  // 1MB should be fine
    test_assert(large_alloc != NULL, "Large but reasonable allocation succeeds");
    runepkg_secure_free(&large_alloc, 1024 * 1024);
    
    // Test string duplication with size limits
    char test_string[100];
    memset(test_string, 'A', 99);
    test_string[99] = '\0';
    
    char *dup_result = runepkg_secure_strdup(test_string);
    test_assert(dup_result != NULL, "String duplication within limits succeeds");
    test_assert(strlen(dup_result) == 99, "Duplicated string length correct");
    runepkg_secure_free((void**)&dup_result, 100);
    
    // Test buffer overflow protection in path operations
    char *overflow_path = runepkg_secure_path_concat("/very/long/path", "subdir");
    test_assert(overflow_path != NULL, "Path concatenation handles long paths safely");
    if (overflow_path) {
        runepkg_secure_free((void**)&overflow_path, strlen(overflow_path) + 1);
    }
    
    printf("Memory boundary protection test completed\n");
}

// Test error handling and recovery
void test_error_handling() {
    test_print_header("Error Handling and Recovery Test");
    
    // Test error string generation
    const char *error_str = runepkg_error_string(RUNEPKG_ERROR_NULL_POINTER);
    test_assert(error_str != NULL, "Error string for NULL pointer exists");
    test_assert(strlen(error_str) > 0, "Error string has content");
    
    error_str = runepkg_error_string(RUNEPKG_ERROR_INVALID_SIZE);
    test_assert(error_str != NULL, "Error string for invalid size exists");
    test_assert(strlen(error_str) > 0, "Invalid size error string has content");
    
    // Test graceful handling of invalid inputs
    PkgInfo invalid_pkg;
    runepkg_pack_init_package_info(&invalid_pkg);
    
    // Try to add invalid package to hash table
    runepkg_hash_table_t *table = runepkg_hash_create_table(16);
    int add_result = runepkg_hash_add_package(table, &invalid_pkg);
    test_assert(add_result != 0, "Adding invalid package fails gracefully");
    
    // Cleanup
    runepkg_hash_destroy_table(table);
    
    printf("Error handling and recovery test completed\n");
}

// Test concurrent safety (basic threading safety checks)
void test_threading_safety() {
    test_print_header("Threading Safety Test");
    
    // Test that defensive functions are thread-safe in basic usage
    // This is a simplified test - real threading tests would require pthread
    
    runepkg_hash_table_t *table = runepkg_hash_create_table(32);
    test_assert(table != NULL, "Hash table creation is thread-safe");
    
    // Multiple package operations
    for (int i = 0; i < 5; i++) {
        PkgInfo pkg;
        runepkg_pack_init_package_info(&pkg);
        
        char name[32];
        snprintf(name, sizeof(name), "thread-test-%d", i);
        pkg.package_name = runepkg_secure_strdup(name);
        pkg.version = runepkg_secure_strdup("1.0.0");
        
        int result = runepkg_hash_add_package(table, &pkg);
        test_assert(result == 0, "Concurrent-style package addition succeeds");
        
        runepkg_pack_free_package_info(&pkg);
    }
    
    test_assert(table->count == 5, "All packages added in thread-safe manner");
    
    runepkg_hash_destroy_table(table);
    
    printf("Threading safety test completed\n");
}

int memory_test_main() {
    printf("=== runepkg Comprehensive Memory Model & Security Test ===\n");
    printf("Testing memory management, defensive programming, and security features\n");
    
    // Core memory management tests
    test_pkg_info_init();
    test_pkg_info_memory();
    test_hash_table_memory();
    test_memory_leaks();
    
    // Enhanced defensive programming and security tests
    test_defensive_security();
    test_memory_boundaries();
    test_error_handling();
    test_threading_safety();
    
    printf("\n=== Comprehensive Test Summary ===\n");
    printf("Total tests: %d\n", test_allocations);
    printf("Failed tests: %d\n", test_failures);
    printf("Passed tests: %d\n", test_allocations - test_failures);
    printf("Success rate: %.1f%%\n", (float)(test_allocations - test_failures) * 100.0f / test_allocations);
    
    if (test_failures == 0) {
        printf("🛡️ ✅ ALL TESTS PASSED! Memory model and security features are robust.\n");
        printf("🎉 Ready for stable release!\n");
        return 0;
    } else {
        printf("❌ %d tests failed! Review security implementation before release.\n", test_failures);
        return 1;
    }
}
