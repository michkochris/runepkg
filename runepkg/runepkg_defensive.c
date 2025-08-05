/******************************************************************************
 * Filename:    runepkg_defensive.c
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Implementation of defensive programming utilities
 ******************************************************************************/

#include "runepkg_defensive.h"
#include "runepkg_util.h"
#include <ctype.h>

// Memory debugging globals
#ifdef RUNEPKG_DEBUG_MEMORY
static size_t total_allocated = 0;
static size_t allocation_count = 0;
#endif

// --- Secure Memory Management ---

void* runepkg_secure_malloc(size_t size) {
    // Validate size
    if (size == 0) {
        runepkg_util_error("Attempted to allocate 0 bytes\n");
        errno = EINVAL;
        return NULL;
    }
    
    if (size > RUNEPKG_MAX_ALLOC_SIZE) {
        runepkg_util_error("Allocation size %zu exceeds maximum %d bytes\n", 
                          size, RUNEPKG_MAX_ALLOC_SIZE);
        errno = ENOMEM;
        return NULL;
    }
    
    // Allocate and zero memory
    void* ptr = malloc(size);
    if (!ptr) {
        runepkg_util_error("Failed to allocate %zu bytes (errno: %d)\n", size, errno);
        return NULL;
    }
    
    // Zero the memory for security
    memset(ptr, 0, size);
    
#ifdef RUNEPKG_DEBUG_MEMORY
    total_allocated += size;
    allocation_count++;
    runepkg_util_log_debug("Allocated %zu bytes at %p (total: %zu)\n", 
                          size, ptr, total_allocated);
#endif
    
    return ptr;
}

void* runepkg_secure_calloc(size_t count, size_t size) {
    // Check for overflow
    if (count > 0 && size > SIZE_MAX / count) {
        runepkg_util_error("Integer overflow in calloc: %zu * %zu\n", count, size);
        errno = ENOMEM;
        return NULL;
    }
    
    size_t total_size = count * size;
    if (total_size > RUNEPKG_MAX_ALLOC_SIZE) {
        runepkg_util_error("Calloc size %zu exceeds maximum %d bytes\n", 
                          total_size, RUNEPKG_MAX_ALLOC_SIZE);
        errno = ENOMEM;
        return NULL;
    }
    
    void* ptr = calloc(count, size);
    if (!ptr && total_size > 0) {
        runepkg_util_error("Failed to calloc %zu elements of %zu bytes\n", count, size);
        return NULL;
    }
    
#ifdef RUNEPKG_DEBUG_MEMORY
    total_allocated += total_size;
    allocation_count++;
    runepkg_util_log_debug("Calloced %zu bytes at %p (total: %zu)\n", 
                          total_size, ptr, total_allocated);
#endif
    
    return ptr;
}

void* runepkg_secure_realloc(void* ptr, size_t new_size) {
    if (new_size > RUNEPKG_MAX_ALLOC_SIZE) {
        runepkg_util_error("Realloc size %zu exceeds maximum %d bytes\n", 
                          new_size, RUNEPKG_MAX_ALLOC_SIZE);
        errno = ENOMEM;
        return NULL;
    }
    
    void* new_ptr = realloc(ptr, new_size);
    if (!new_ptr && new_size > 0) {
        runepkg_util_error("Failed to realloc to %zu bytes\n", new_size);
        return NULL;
    }
    
#ifdef RUNEPKG_DEBUG_MEMORY
    runepkg_util_log_debug("Realloced %p to %zu bytes -> %p\n", ptr, new_size, new_ptr);
#endif
    
    return new_ptr;
}

char* runepkg_secure_strdup(const char* str) {
    if (!str) {
        runepkg_util_error("Attempted to strdup NULL string\n");
        errno = EINVAL;
        return NULL;
    }
    
    size_t len = strlen(str);
    if (len > RUNEPKG_MAX_STRING_LEN) {
        runepkg_util_error("String length %zu exceeds maximum %d\n", 
                          len, RUNEPKG_MAX_STRING_LEN);
        errno = ENOMEM;
        return NULL;
    }
    
    char* dup = runepkg_secure_malloc(len + 1);
    if (!dup) {
        return NULL;
    }
    
    memcpy(dup, str, len + 1);
    return dup;
}

char* runepkg_secure_strndup(const char* str, size_t max_len) {
    if (!str) {
        runepkg_util_error("Attempted to strndup NULL string\n");
        errno = EINVAL;
        return NULL;
    }
    
    if (max_len > RUNEPKG_MAX_STRING_LEN) {
        runepkg_util_error("Max length %zu exceeds maximum %d\n", 
                          max_len, RUNEPKG_MAX_STRING_LEN);
        errno = ENOMEM;
        return NULL;
    }
    
    // Find actual length, capped at max_len
    size_t len = 0;
    while (len < max_len && str[len] != '\0') {
        len++;
    }
    
    char* dup = runepkg_secure_malloc(len + 1);
    if (!dup) {
        return NULL;
    }
    
    memcpy(dup, str, len);
    dup[len] = '\0';
    return dup;
}

void runepkg_secure_free(void** ptr, size_t size) {
    if (!ptr || !*ptr) {
        return;
    }
    
    // Optional: Wipe memory for security
    if (size > 0) {
        memset(*ptr, 0, size);
    }
    
#ifdef RUNEPKG_DEBUG_MEMORY
    runepkg_util_log_debug("Freeing %p (size: %zu)\n", *ptr, size);
    allocation_count--;
#endif
    
    free(*ptr);
    *ptr = NULL;
}

// --- Secure String Operations ---

runepkg_error_t runepkg_secure_strcpy(char* dest, size_t dest_size, const char* src) {
    if (!dest || !src) {
        runepkg_util_error("NULL pointer in secure_strcpy\n");
        return RUNEPKG_ERROR_NULL_POINTER;
    }
    
    if (dest_size == 0) {
        runepkg_util_error("Zero destination size in secure_strcpy\n");
        return RUNEPKG_ERROR_INVALID_SIZE;
    }
    
    size_t src_len = strlen(src);
    if (src_len >= dest_size) {
        runepkg_util_error("Source string too long for destination: %zu >= %zu\n", 
                          src_len, dest_size);
        return RUNEPKG_ERROR_BUFFER_OVERFLOW;
    }
    
    memcpy(dest, src, src_len + 1);
    return RUNEPKG_SUCCESS;
}

runepkg_error_t runepkg_secure_strcat(char* dest, size_t dest_size, const char* src) {
    if (!dest || !src) {
        runepkg_util_error("NULL pointer in secure_strcat\n");
        return RUNEPKG_ERROR_NULL_POINTER;
    }
    
    if (dest_size == 0) {
        runepkg_util_error("Zero destination size in secure_strcat\n");
        return RUNEPKG_ERROR_INVALID_SIZE;
    }
    
    size_t dest_len = strlen(dest);
    size_t src_len = strlen(src);
    
    if (dest_len >= dest_size) {
        runepkg_util_error("Destination already full: %zu >= %zu\n", dest_len, dest_size);
        return RUNEPKG_ERROR_BUFFER_OVERFLOW;
    }
    
    if (dest_len + src_len >= dest_size) {
        runepkg_util_error("Combined string too long: %zu + %zu >= %zu\n", 
                          dest_len, src_len, dest_size);
        return RUNEPKG_ERROR_BUFFER_OVERFLOW;
    }
    
    memcpy(dest + dest_len, src, src_len + 1);
    return RUNEPKG_SUCCESS;
}

char* runepkg_secure_path_concat(const char* dir, const char* file) {
    if (!dir || !file) {
        runepkg_util_error("NULL pointer in path concatenation\n");
        return NULL;
    }
    
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(file);
    
    if (dir_len > RUNEPKG_MAX_PATH_LEN || file_len > RUNEPKG_MAX_PATH_LEN) {
        runepkg_util_error("Path component too long: dir=%zu, file=%zu (max=%d)\n",
                          dir_len, file_len, RUNEPKG_MAX_PATH_LEN);
        return NULL;
    }
    
    // Check for directory traversal attempts
    if (strstr(file, "..") || strstr(file, "//") || file[0] == '/') {
        runepkg_util_error("Suspicious file path: %s\n", file);
        return NULL;
    }
    
    bool needs_slash = (dir_len > 0 && dir[dir_len - 1] != '/');
    size_t total_len = dir_len + file_len + (needs_slash ? 1 : 0) + 1;
    
    if (total_len > RUNEPKG_MAX_PATH_LEN) {
        runepkg_util_error("Combined path too long: %zu > %d\n", total_len, RUNEPKG_MAX_PATH_LEN);
        return NULL;
    }
    
    char* full_path = runepkg_secure_malloc(total_len);
    if (!full_path) {
        return NULL;
    }
    
    runepkg_error_t err = runepkg_secure_strcpy(full_path, total_len, dir);
    if (err != RUNEPKG_SUCCESS) {
        runepkg_secure_free((void**)&full_path, total_len);
        return NULL;
    }
    
    if (needs_slash) {
        err = runepkg_secure_strcat(full_path, total_len, "/");
        if (err != RUNEPKG_SUCCESS) {
            runepkg_secure_free((void**)&full_path, total_len);
            return NULL;
        }
    }
    
    err = runepkg_secure_strcat(full_path, total_len, file);
    if (err != RUNEPKG_SUCCESS) {
        runepkg_secure_free((void**)&full_path, total_len);
        return NULL;
    }
    
    return full_path;
}

char* runepkg_secure_sprintf(size_t max_len, const char* format, ...) {
    if (!format) {
        runepkg_util_error("NULL format in secure_sprintf\n");
        return NULL;
    }
    
    if (max_len > RUNEPKG_MAX_STRING_LEN) {
        runepkg_util_error("Max length %zu exceeds limit %d\n", max_len, RUNEPKG_MAX_STRING_LEN);
        return NULL;
    }
    
    char* buffer = runepkg_secure_malloc(max_len + 1);
    if (!buffer) {
        return NULL;
    }
    
    va_list args;
    va_start(args, format);
    int result = vsnprintf(buffer, max_len + 1, format, args);
    va_end(args);
    
    if (result < 0 || (size_t)result > max_len) {
        runepkg_util_error("sprintf formatting failed or truncated\n");
        runepkg_secure_free((void**)&buffer, max_len + 1);
        return NULL;
    }
    
    return buffer;
}

// --- Input Validation ---

runepkg_error_t runepkg_validate_pointer(const void* ptr, const char* name) {
    if (!ptr) {
        runepkg_util_security_blocked("NULL pointer: %s\n", name ? name : "unknown");
        return RUNEPKG_ERROR_NULL_POINTER;
    }
    return RUNEPKG_SUCCESS;
}

runepkg_error_t runepkg_validate_string(const char* str, size_t max_len, const char* name) {
    runepkg_error_t err = runepkg_validate_pointer(str, name);
    if (err != RUNEPKG_SUCCESS) {
        return err;
    }
    
    size_t len = strlen(str);
    if (len > max_len) {
        runepkg_util_error("String %s too long: %zu > %zu\n", 
                          name ? name : "unknown", len, max_len);
        return RUNEPKG_ERROR_SIZE_LIMIT;
    }
    
    return RUNEPKG_SUCCESS;
}

runepkg_error_t runepkg_validate_size(size_t size, size_t max_size, const char* name) {
    if (size > max_size) {
        runepkg_util_error("Size %s too large: %zu > %zu\n", 
                          name ? name : "unknown", size, max_size);
        return RUNEPKG_ERROR_SIZE_LIMIT;
    }
    return RUNEPKG_SUCCESS;
}

runepkg_error_t runepkg_validate_file_count(int count) {
    if (count < 0) {
        runepkg_util_error("Negative file count: %d\n", count);
        return RUNEPKG_ERROR_INVALID_INPUT;
    }
    
    if (count > RUNEPKG_MAX_FILE_COUNT) {
        runepkg_util_error("File count too large: %d > %d\n", count, RUNEPKG_MAX_FILE_COUNT);
        return RUNEPKG_ERROR_SIZE_LIMIT;
    }
    
    return RUNEPKG_SUCCESS;
}

runepkg_error_t runepkg_validate_path(const char* path) {
    runepkg_error_t err = runepkg_validate_string(path, RUNEPKG_MAX_PATH_LEN, "path");
    if (err != RUNEPKG_SUCCESS) {
        return err;
    }
    
    // Check for path traversal attacks
    if (strstr(path, "..")) {
        runepkg_util_security_blocked("path traversal attempt: %s\n", path);
        return RUNEPKG_ERROR_INVALID_INPUT;
    }
    
    // Check for absolute paths when not expected
    if (path[0] == '/' && strlen(path) > 1) {
        runepkg_util_log_debug("Absolute path detected: %s\n", path);
    }
    
    return RUNEPKG_SUCCESS;
}

// --- Secure File Operations ---

char* runepkg_secure_read_file(const char* filepath, size_t max_size, size_t* out_size) {
    runepkg_error_t err = runepkg_validate_path(filepath);
    if (err != RUNEPKG_SUCCESS) {
        return NULL;
    }
    
    if (max_size > RUNEPKG_MAX_ALLOC_SIZE) {
        runepkg_util_error("Max file size %zu exceeds limit %d\n", max_size, RUNEPKG_MAX_ALLOC_SIZE);
        return NULL;
    }
    
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        runepkg_util_error("Failed to open file: %s (errno: %d)\n", filepath, errno);
        return NULL;
    }
    
    // Get file size
    if (fseek(file, 0, SEEK_END) != 0) {
        runepkg_util_error("Failed to seek to end of file: %s\n", filepath);
        fclose(file);
        return NULL;
    }
    
    long file_size = ftell(file);
    if (file_size < 0) {
        runepkg_util_error("Failed to get file size: %s\n", filepath);
        fclose(file);
        return NULL;
    }
    
    if ((size_t)file_size > max_size) {
        runepkg_util_error("File too large: %ld > %zu\n", file_size, max_size);
        fclose(file);
        return NULL;
    }
    
    rewind(file);
    
    char* buffer = runepkg_secure_malloc(file_size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    
    size_t bytes_read = fread(buffer, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != (size_t)file_size) {
        runepkg_util_error("Failed to read complete file: %zu != %ld\n", bytes_read, file_size);
        runepkg_secure_free((void**)&buffer, file_size + 1);
        return NULL;
    }
    
    buffer[file_size] = '\0';
    
    if (out_size) {
        *out_size = bytes_read;
    }
    
    return buffer;
}

// --- Error Messages ---

const char* runepkg_error_string(runepkg_error_t error) {
    switch (error) {
        case RUNEPKG_SUCCESS:
            return "Success";
        case RUNEPKG_ERROR_NULL_POINTER:
            return "NULL pointer error";
        case RUNEPKG_ERROR_INVALID_SIZE:
            return "Invalid size error";
        case RUNEPKG_ERROR_MEMORY_ALLOCATION:
            return "Memory allocation error";
        case RUNEPKG_ERROR_BUFFER_OVERFLOW:
            return "Buffer overflow error";
        case RUNEPKG_ERROR_INVALID_INPUT:
            return "Invalid input error";
        case RUNEPKG_ERROR_SIZE_LIMIT:
            return "Size limit exceeded error";
        default:
            return "Unknown error";
    }
}

// --- Memory Debugging ---

#ifdef RUNEPKG_DEBUG_MEMORY
void runepkg_memory_stats(void) {
    printf("=== Memory Statistics ===\n");
    printf("Total allocated: %zu bytes\n", total_allocated);
    printf("Active allocations: %zu\n", allocation_count);
    printf("========================\n");
}

size_t runepkg_memory_usage(void) {
    return total_allocated;
}
#endif
