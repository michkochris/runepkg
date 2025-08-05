/******************************************************************************
 * Filename:    simple_memory_test.c
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Simplified memory test focused on hash and pack modules
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "runepkg_hash.h"
#include "runepkg_pack.h"
#include "runepkg_defensive.h"
#include "runepkg_util.h"

// Global variable required by modules
bool g_verbose_mode = false;

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
    
    // Allocate some test data using defensive programming functions
    pkg_info.package_name = runepkg_secure_strdup("test-package");
    pkg_info.version = runepkg_secure_strdup("1.0.0");
    pkg_info.description = runepkg_secure_strdup("Test package description");
    pkg_info.control_dir_path = runepkg_secure_strdup("/tmp/control");
    pkg_info.data_dir_path = runepkg_secure_strdup("/tmp/data");
    
    // Allocate file list
    pkg_info.file_count = 3;
    pkg_info.file_list = runepkg_secure_malloc(3 * sizeof(char*));
    pkg_info.file_list[0] = runepkg_secure_strdup("file1.txt");
    pkg_info.file_list[1] = runepkg_secure_strdup("file2.txt");
    pkg_info.file_list[2] = runepkg_secure_strdup("file3.txt");
    
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
    
    pkg_info.package_name = runepkg_secure_strdup("hash-test-package");
    pkg_info.version = runepkg_secure_strdup("2.0.0");
    pkg_info.control_dir_path = runepkg_secure_strdup("/tmp/hash-control");
    pkg_info.data_dir_path = runepkg_secure_strdup("/tmp/hash-data");
    
    // Add to hash table
    int result = runepkg_hash_add_package(table, &pkg_info);
    test_assert(result == 0, "Package added to hash table successfully");
    
    // Search for package
    PkgInfo *found = runepkg_hash_search(table, "hash-test-package");
    test_assert(found != NULL, "Package found in hash table");
    test_assert(strcmp(found->package_name, "hash-test-package") == 0, "Found package name matches");
    
    // Critical test: Check if control_dir_path and data_dir_path are handled
    printf("Found package control_dir_path: %s\n", found->control_dir_path ? found->control_dir_path : "NULL");
    printf("Found package data_dir_path: %s\n", found->data_dir_path ? found->data_dir_path : "NULL");
    
    test_assert(found->control_dir_path != NULL, "control_dir_path is preserved in hash table");
    test_assert(found->data_dir_path != NULL, "data_dir_path is preserved in hash table");
    test_assert(strcmp(found->control_dir_path, "/tmp/hash-control") == 0, "control_dir_path value matches");
    test_assert(strcmp(found->data_dir_path, "/tmp/hash-data") == 0, "data_dir_path value matches");
    
    // Clean up original package info
    runepkg_pack_free_package_info(&pkg_info);
    
    // Clean up hash table
    runepkg_hash_destroy_table(table);
    
    printf("Hash table memory test completed\n");
}

// Test defensive programming functions
void test_defensive_functions() {
    test_print_header("Defensive Programming Functions Test");
    
    // Test secure string duplication
    char *test_str = runepkg_secure_strdup("Hello, World!");
    test_assert(test_str != NULL, "runepkg_secure_strdup returned non-NULL");
    test_assert(strcmp(test_str, "Hello, World!") == 0, "String duplicated correctly");
    runepkg_secure_free((void**)&test_str, strlen("Hello, World!") + 1);
    
    // Test secure memory allocation
    void *test_mem = runepkg_secure_malloc(1024);
    test_assert(test_mem != NULL, "runepkg_secure_malloc returned non-NULL");
    runepkg_secure_free(&test_mem, 1024);
    
    // Test input validation
    runepkg_error_t result = runepkg_validate_string("valid-string", 20, "test_string");
    test_assert(result == RUNEPKG_SUCCESS, "Valid string passes validation");
    
    result = runepkg_validate_string(NULL, 20, "null_string");
    test_assert(result != RUNEPKG_SUCCESS, "NULL string fails validation");
    
    printf("Defensive programming test completed\n");
}

int main() {
    printf("=== runepkg Simplified Memory Model Test ===\n");
    printf("Testing memory management for PkgInfo structure with defensive programming\n");
    
    test_pkg_info_init();
    test_pkg_info_memory();
    test_hash_table_memory();
    test_defensive_functions();
    
    printf("\n=== Test Summary ===\n");
    printf("Total tests: %d\n", test_allocations);
    printf("Failed tests: %d\n", test_failures);
    printf("Passed tests: %d\n", test_allocations - test_failures);
    
    if (test_failures == 0) {
        printf("✅ All tests passed! Memory model and defensive programming working correctly.\n");
        return 0;
    } else {
        printf("❌ %d tests failed!\n", test_failures);
        return 1;
    }
}
