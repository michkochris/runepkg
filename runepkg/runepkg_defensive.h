/******************************************************************************
 * Filename:    runepkg_defensive.h
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Defensive programming utilities and secure memory management
 *
 * This header provides security-hardened memory management functions and
 * defensive programming utilities for runepkg.
 ******************************************************************************/

#ifndef RUNEPKG_DEFENSIVE_H
#define RUNEPKG_DEFENSIVE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>

// --- Security Limits ---
#define RUNEPKG_MAX_STRING_LEN      (1024 * 1024)  // 1MB max string
#define RUNEPKG_MAX_PATH_LEN        4096           // 4KB max path
#define RUNEPKG_MAX_FILE_COUNT      100000         // 100K max files
#define RUNEPKG_MAX_ALLOC_SIZE      (256 * 1024 * 1024) // 256MB max allocation

// --- Defensive Return Codes ---
typedef enum {
    RUNEPKG_SUCCESS = 0,
    RUNEPKG_ERROR_NULL_POINTER = -1,
    RUNEPKG_ERROR_INVALID_SIZE = -2,
    RUNEPKG_ERROR_MEMORY_ALLOCATION = -3,
    RUNEPKG_ERROR_BUFFER_OVERFLOW = -4,
    RUNEPKG_ERROR_INVALID_INPUT = -5,
    RUNEPKG_ERROR_SIZE_LIMIT = -6
} runepkg_error_t;

// --- Secure Memory Management ---

/**
 * @brief Secure malloc with size validation and zero initialization
 * @param size Size to allocate (must be > 0 and < RUNEPKG_MAX_ALLOC_SIZE)
 * @return Allocated memory or NULL on failure
 */
void* runepkg_secure_malloc(size_t size);

/**
 * @brief Secure calloc with overflow detection
 * @param count Number of elements
 * @param size Size of each element
 * @return Allocated and zeroed memory or NULL on failure
 */
void* runepkg_secure_calloc(size_t count, size_t size);

/**
 * @brief Secure realloc with size validation
 * @param ptr Existing pointer (can be NULL)
 * @param new_size New size (must be < RUNEPKG_MAX_ALLOC_SIZE)
 * @return Reallocated memory or NULL on failure
 */
void* runepkg_secure_realloc(void* ptr, size_t new_size);

/**
 * @brief Secure string duplication with length validation
 * @param str Source string (must not be NULL)
 * @return Duplicated string or NULL on failure
 */
char* runepkg_secure_strdup(const char* str);

/**
 * @brief Secure string duplication with maximum length
 * @param str Source string (must not be NULL)
 * @param max_len Maximum length to copy
 * @return Duplicated string or NULL on failure
 */
char* runepkg_secure_strndup(const char* str, size_t max_len);

/**
 * @brief Secure free with automatic NULL setting and optional wiping
 * @param ptr Pointer to the pointer to free
 * @param size Size of the memory to wipe (0 = don't wipe)
 */
void runepkg_secure_free(void** ptr, size_t size);

// --- Secure String Operations ---

/**
 * @brief Secure string copy with bounds checking
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source string
 * @return RUNEPKG_SUCCESS or error code
 */
runepkg_error_t runepkg_secure_strcpy(char* dest, size_t dest_size, const char* src);

/**
 * @brief Secure string concatenation with bounds checking
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source string to append
 * @return RUNEPKG_SUCCESS or error code
 */
runepkg_error_t runepkg_secure_strcat(char* dest, size_t dest_size, const char* src);

/**
 * @brief Secure path concatenation with validation
 * @param dir Directory path (must not be NULL)
 * @param file File name (must not be NULL)
 * @return Allocated path string or NULL on failure
 */
char* runepkg_secure_path_concat(const char* dir, const char* file);

/**
 * @brief Secure formatted string creation
 * @param max_len Maximum length for result
 * @param format Format string
 * @param ... Format arguments
 * @return Allocated formatted string or NULL on failure
 */
char* runepkg_secure_sprintf(size_t max_len, const char* format, ...);

// --- Input Validation ---

/**
 * @brief Validate pointer is not NULL
 * @param ptr Pointer to check
 * @param name Name for error messages
 * @return RUNEPKG_SUCCESS or RUNEPKG_ERROR_NULL_POINTER
 */
runepkg_error_t runepkg_validate_pointer(const void* ptr, const char* name);

/**
 * @brief Validate string is not NULL and has reasonable length
 * @param str String to validate
 * @param max_len Maximum allowed length
 * @param name Name for error messages
 * @return RUNEPKG_SUCCESS or error code
 */
runepkg_error_t runepkg_validate_string(const char* str, size_t max_len, const char* name);

/**
 * @brief Validate size is within reasonable limits
 * @param size Size to validate
 * @param max_size Maximum allowed size
 * @param name Name for error messages
 * @return RUNEPKG_SUCCESS or error code
 */
runepkg_error_t runepkg_validate_size(size_t size, size_t max_size, const char* name);

/**
 * @brief Validate file count is reasonable
 * @param count File count to validate
 * @return RUNEPKG_SUCCESS or RUNEPKG_ERROR_SIZE_LIMIT
 */
runepkg_error_t runepkg_validate_file_count(int count);

// --- Secure File Operations ---

/**
 * @brief Secure file reading with size limits
 * @param filepath Path to file (must be validated)
 * @param max_size Maximum file size to read
 * @param out_size Pointer to store actual size read
 * @return Allocated buffer with file contents or NULL on failure
 */
char* runepkg_secure_read_file(const char* filepath, size_t max_size, size_t* out_size);

/**
 * @brief Validate path for security (no .. traversal, etc.)
 * @param path Path to validate
 * @return RUNEPKG_SUCCESS or error code
 */
runepkg_error_t runepkg_validate_path(const char* path);

// --- Memory Debugging (if enabled) ---
#ifdef RUNEPKG_DEBUG_MEMORY
void runepkg_memory_stats(void);
size_t runepkg_memory_usage(void);
#endif

// --- Error Messages ---
const char* runepkg_error_string(runepkg_error_t error);

#endif // RUNEPKG_DEFENSIVE_H
