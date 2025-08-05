/******************************************************************************
 * Filename:    security_test.c
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Security and defensive programming test suite
 *
 * This test program validates:
 * 1. Secure memory allocation and bounds checking
 * 2. Input validation and sanitization
 * 3. Buffer overflow protection
 * 4. Path traversal attack prevention
 * 5. Resource limit enforcement
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include "runepkg_defensive.h"
#include "runepkg_util.h"

// Global variable for verbose logging (required by utility functions)
bool g_verbose_mode = true;

// Test counter
static int security_tests = 0;
static int security_failures = 0;

void security_test_header(const char *test_name) {
    printf("\n=== %s ===\n", test_name);
}

void security_assert(int condition, const char *message) {
    security_tests++;
    if (!condition) {
        printf("SECURITY FAIL: %s\n", message);
        security_failures++;
    } else {
        printf("SECURITY PASS: %s\n", message);
    }
}

// Test secure memory allocation
void test_secure_memory_allocation() {
    security_test_header("Secure Memory Allocation Tests");
    
    // Test normal allocation
    void *ptr = runepkg_secure_malloc(1024);
    security_assert(ptr != NULL, "Normal allocation succeeds");
    runepkg_secure_free(&ptr, 1024);
    security_assert(ptr == NULL, "Pointer set to NULL after free");
    
    // Test zero allocation (should fail)
    ptr = runepkg_secure_malloc(0);
    security_assert(ptr == NULL, "Zero allocation fails");
    
    // Test oversized allocation (should fail)
    ptr = runepkg_secure_malloc(RUNEPKG_MAX_ALLOC_SIZE + 1);
    security_assert(ptr == NULL, "Oversized allocation fails");
    
    // Test secure calloc with overflow detection
    ptr = runepkg_secure_calloc(SIZE_MAX / 2, SIZE_MAX / 2);
    security_assert(ptr == NULL, "Calloc overflow detection works");
    
    // Test normal calloc
    ptr = runepkg_secure_calloc(10, sizeof(int));
    security_assert(ptr != NULL, "Normal calloc succeeds");
    
    // Verify zeroed memory
    int *int_ptr = (int*)ptr;
    security_assert(int_ptr[0] == 0 && int_ptr[9] == 0, "Calloc properly zeros memory");
    runepkg_secure_free(&ptr, 10 * sizeof(int));
}

// Test secure string operations
void test_secure_string_operations() {
    security_test_header("Secure String Operations Tests");
    
    // Test secure strdup
    char *dup = runepkg_secure_strdup("test string");
    security_assert(dup != NULL, "Secure strdup succeeds");
    security_assert(strcmp(dup, "test string") == 0, "Secure strdup content correct");
    runepkg_secure_free((void**)&dup, 0);
    
    // Test NULL strdup (should fail)
    dup = runepkg_secure_strdup(NULL);
    security_assert(dup == NULL, "NULL strdup fails");
    
    // Test oversized string (create a very long string)
    char *long_str = malloc(RUNEPKG_MAX_STRING_LEN + 100);
    if (long_str) {
        memset(long_str, 'A', RUNEPKG_MAX_STRING_LEN + 50);
        long_str[RUNEPKG_MAX_STRING_LEN + 50] = '\0';
        
        dup = runepkg_secure_strdup(long_str);
        security_assert(dup == NULL, "Oversized strdup fails");
        free(long_str);
    }
    
    // Test secure strcpy
    char dest[100];
    runepkg_error_t err = runepkg_secure_strcpy(dest, sizeof(dest), "safe string");
    security_assert(err == RUNEPKG_SUCCESS, "Safe strcpy succeeds");
    security_assert(strcmp(dest, "safe string") == 0, "Safe strcpy content correct");
    
    // Test buffer overflow protection
    err = runepkg_secure_strcpy(dest, 5, "this string is too long");
    security_assert(err == RUNEPKG_ERROR_BUFFER_OVERFLOW, "Buffer overflow detected");
    
    // Test secure strcat
    strcpy(dest, "Hello ");
    err = runepkg_secure_strcat(dest, sizeof(dest), "World!");
    security_assert(err == RUNEPKG_SUCCESS, "Safe strcat succeeds");
    security_assert(strcmp(dest, "Hello World!") == 0, "Safe strcat content correct");
    
    // Test strcat buffer overflow
    strcpy(dest, "This is a very long string that takes up most of the buffer space");
    err = runepkg_secure_strcat(dest, sizeof(dest), " and this addition will definitely overflow the buffer");
    security_assert(err == RUNEPKG_ERROR_BUFFER_OVERFLOW, "Strcat overflow detected");
}

// Test input validation
void test_input_validation() {
    security_test_header("Input Validation Tests");
    
    // Test pointer validation
    runepkg_error_t err = runepkg_validate_pointer("valid", "test_ptr");
    security_assert(err == RUNEPKG_SUCCESS, "Valid pointer accepted");
    
    err = runepkg_validate_pointer(NULL, "test_ptr");
    security_assert(err == RUNEPKG_ERROR_NULL_POINTER, "NULL pointer rejected");
    
    // Test string validation
    err = runepkg_validate_string("normal string", 100, "test_string");
    security_assert(err == RUNEPKG_SUCCESS, "Normal string accepted");
    
    err = runepkg_validate_string("toolongstring", 5, "test_string");
    security_assert(err == RUNEPKG_ERROR_SIZE_LIMIT, "Oversized string rejected");
    
    err = runepkg_validate_string(NULL, 100, "test_string");
    security_assert(err == RUNEPKG_ERROR_NULL_POINTER, "NULL string rejected");
    
    // Test size validation
    err = runepkg_validate_size(1000, 2000, "test_size");
    security_assert(err == RUNEPKG_SUCCESS, "Normal size accepted");
    
    err = runepkg_validate_size(3000, 2000, "test_size");
    security_assert(err == RUNEPKG_ERROR_SIZE_LIMIT, "Oversized value rejected");
    
    // Test file count validation
    err = runepkg_validate_file_count(1000);
    security_assert(err == RUNEPKG_SUCCESS, "Normal file count accepted");
    
    err = runepkg_validate_file_count(-1);
    security_assert(err == RUNEPKG_ERROR_INVALID_INPUT, "Negative file count rejected");
    
    err = runepkg_validate_file_count(RUNEPKG_MAX_FILE_COUNT + 1);
    security_assert(err == RUNEPKG_ERROR_SIZE_LIMIT, "Excessive file count rejected");
}

// Test path security
void test_path_security() {
    security_test_header("Path Security Tests");
    
    // Test normal path concatenation
    char *path = runepkg_secure_path_concat("/home/user", "document.txt");
    security_assert(path != NULL, "Normal path concat succeeds");
    security_assert(strcmp(path, "/home/user/document.txt") == 0, "Normal path concat correct");
    runepkg_secure_free((void**)&path, 0);
    
    // Test path traversal attack prevention
    path = runepkg_secure_path_concat("/home/user", "../../../etc/passwd");
    security_assert(path == NULL, "Path traversal attack blocked");
    
    // Test absolute path injection
    path = runepkg_secure_path_concat("/home/user", "/etc/passwd");
    security_assert(path == NULL, "Absolute path injection blocked");
    
    // Test double slash injection
    path = runepkg_secure_path_concat("/home/user", "//etc/passwd");
    security_assert(path == NULL, "Double slash injection blocked");
    
    // Test path validation
    runepkg_error_t err = runepkg_validate_path("/normal/path/file.txt");
    security_assert(err == RUNEPKG_SUCCESS, "Normal path accepted");
    
    err = runepkg_validate_path("/path/with/../traversal");
    security_assert(err == RUNEPKG_ERROR_INVALID_INPUT, "Path traversal rejected");
    
    // Test oversized path
    char *long_path = malloc(RUNEPKG_MAX_PATH_LEN + 100);
    if (long_path) {
        memset(long_path, 'A', RUNEPKG_MAX_PATH_LEN + 50);
        long_path[RUNEPKG_MAX_PATH_LEN + 50] = '\0';
        
        err = runepkg_validate_path(long_path);
        security_assert(err == RUNEPKG_ERROR_SIZE_LIMIT, "Oversized path rejected");
        free(long_path);
    }
}

// Test resource limits
void test_resource_limits() {
    security_test_header("Resource Limit Tests");
    
    // Test secure sprintf with size limits
    char *result = runepkg_secure_sprintf(100, "Hello %s %d", "World", 42);
    security_assert(result != NULL, "Normal sprintf succeeds");
    security_assert(strcmp(result, "Hello World 42") == 0, "Sprintf content correct");
    runepkg_secure_free((void**)&result, 0);
    
    // Test oversized sprintf limit
    result = runepkg_secure_sprintf(RUNEPKG_MAX_STRING_LEN + 1, "test");
    security_assert(result == NULL, "Oversized sprintf limit enforced");
    
    // Test format string that would exceed buffer
    result = runepkg_secure_sprintf(10, "This is a very long string that exceeds the buffer");
    security_assert(result == NULL, "Sprintf buffer limit enforced");
}

// Test error handling
void test_error_handling() {
    security_test_header("Error Handling Tests");
    
    // Test error string functions
    const char *msg = runepkg_error_string(RUNEPKG_SUCCESS);
    security_assert(strcmp(msg, "Success") == 0, "Success message correct");
    
    msg = runepkg_error_string(RUNEPKG_ERROR_NULL_POINTER);
    security_assert(strstr(msg, "NULL") != NULL, "NULL pointer error message contains 'NULL'");
    
    msg = runepkg_error_string(RUNEPKG_ERROR_BUFFER_OVERFLOW);
    security_assert(strstr(msg, "overflow") != NULL, "Buffer overflow error message contains 'overflow'");
    
    msg = runepkg_error_string((runepkg_error_t)-999);
    security_assert(strcmp(msg, "Unknown error") == 0, "Unknown error handled");
}

// Test realistic attack scenarios
void test_attack_scenarios() {
    security_test_header("Attack Scenario Tests");
    
    // Test multiple path traversal techniques
    char *attack_paths[] = {
        "../../../etc/passwd",
        "..\\..\\..\\windows\\system32\\config\\sam",
        "....//....//....//etc/passwd",
        "..%2f..%2f..%2fetc%2fpasswd",
        NULL
    };
    
    for (int i = 0; attack_paths[i] != NULL; i++) {
        char *path = runepkg_secure_path_concat("/safe/dir", attack_paths[i]);
        security_assert(path == NULL, "Path traversal attack variant blocked");
    }
    
    // Test format string attacks
    char buffer[100];
    runepkg_error_t err = runepkg_secure_strcpy(buffer, sizeof(buffer), "%s%s%s%s%s");
    security_assert(err == RUNEPKG_SUCCESS, "Format string stored safely");
    // The key is that we're not using it unsafely in printf-style functions
    
    // Test extremely long input
    size_t huge_size = 1024 * 1024 * 10; // 10MB
    char *huge_input = malloc(huge_size);
    if (huge_input) {
        memset(huge_input, 'X', huge_size - 1);
        huge_input[huge_size - 1] = '\0';
        
        char *dup = runepkg_secure_strdup(huge_input);
        security_assert(dup == NULL, "Huge input rejected");
        free(huge_input);
    }
}

int main() {
    printf("=== runepkg Security & Defensive Programming Test Suite ===\n");
    printf("Testing security hardening and defensive programming measures\n");
    
    test_secure_memory_allocation();
    test_secure_string_operations();
    test_input_validation();
    test_path_security();
    test_resource_limits();
    test_error_handling();
    test_attack_scenarios();
    
    printf("\n=== Security Test Summary ===\n");
    printf("Total security tests: %d\n", security_tests);
    printf("Failed security tests: %d\n", security_failures);
    printf("Passed security tests: %d\n", security_tests - security_failures);
    
    if (security_failures == 0) {
        printf("🔒 All security tests passed! System is hardened.\n");
        return 0;
    } else {
        printf("⚠️  %d security tests failed! Review security measures.\n", security_failures);
        return 1;
    }
}
