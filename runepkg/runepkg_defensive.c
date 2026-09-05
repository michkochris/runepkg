/*****************************************************************************
 * Filename:    runepkg_defensive.c
 * Author:      <michkochris@gmail.com>
 * Date:        2025-01-04
 * Description: Defensive wrappers and safety helpers for runepkg
 * LICENSE:     GPL v3
 * THIS IS FREE SOFTWARE; YOU CAN REDISTRIBUTE IT AND/OR MODIFY IT UNDER
 * THE TERMS OF THE GNU GENERAL PUBLIC LICENSE AS PUBLISHED BY THE FREE
 * SOFTWARE FOUNDATION; EITHER VERSION 3 OF THE LICENSE, OR (AT YOUR OPTION)
 * ANY LATER VERSION.
 * THIS PROGRAM IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. SEE THE
 * GNU GENERAL PUBLIC LICENSE FOR MORE DETAILS.
 ******************************************************************************/

#include "runepkg_portable.h"
#include "runepkg_defensive.h"
#include "runepkg_util.h"
#include <ctype.h>

/* Memory debugging globals */
#ifdef RUNEPKG_DEBUG_MEMORY
static size_t total_allocated = 0;
static size_t allocation_count = 0;
#endif

/* --- Secure Memory Management --- */

void* runepkg_secure_malloc(size_t size) {
    void* ptr;

    /* Validate size */
    if (size == 0) {
        runepkg_util_error("Attempted to allocate 0 bytes\n");
        errno = EINVAL;
        return NULL;
    }
    
    if (size > RUNEPKG_MAX_ALLOC_SIZE) {
        runepkg_util_error("Allocation size %lu exceeds maximum %d bytes\n",
                          (unsigned long)size, RUNEPKG_MAX_ALLOC_SIZE);
        errno = ENOMEM;
        return NULL;
    }
    
    /* Allocate and zero memory */
    (void)errno;
    ptr = malloc(size);
    if (!ptr) {
        int err = ENOMEM;
        runepkg_util_error("Failed to allocate %lu bytes (errno: %d)\n", (unsigned long)size, err);
        return NULL;
    }
    
    /* Zero the memory for security */
    memset(ptr, 0, size);
    
#ifdef RUNEPKG_DEBUG_MEMORY
    total_allocated += size;
    allocation_count++;
    runepkg_util_log_debug("Allocated %lu bytes at %p (total: %lu)\n",
                          (unsigned long)size, ptr, (unsigned long)total_allocated);
#endif
    
    return ptr;
}

void* runepkg_secure_calloc(size_t count, size_t size) {
    size_t total_size;
    void* ptr;

    /* Check for overflow */
    if (count > 0 && size > SIZE_MAX / count) {
        runepkg_util_error("Integer overflow in calloc: %lu * %lu\n", (unsigned long)count, (unsigned long)size);
        errno = ENOMEM;
        return NULL;
    }
    
    total_size = count * size;
    if (total_size > RUNEPKG_MAX_ALLOC_SIZE) {
        runepkg_util_error("Calloc size %lu exceeds maximum %d bytes\n",
                          (unsigned long)total_size, RUNEPKG_MAX_ALLOC_SIZE);
        errno = ENOMEM;
        return NULL;
    }
    
    ptr = calloc(count, size);
    if (!ptr && total_size > 0) {
        runepkg_util_error("Failed to calloc %lu elements of %lu bytes\n", (unsigned long)count, (unsigned long)size);
        return NULL;
    }
    
#ifdef RUNEPKG_DEBUG_MEMORY
    total_allocated += total_size;
    allocation_count++;
    runepkg_util_log_debug("Calloced %lu bytes at %p (total: %lu)\n",
                          (unsigned long)total_size, ptr, (unsigned long)total_allocated);
#endif
    
    return ptr;
}

void* runepkg_secure_realloc(void* ptr, size_t new_size) {
    void* new_ptr;

    if (new_size > RUNEPKG_MAX_ALLOC_SIZE) {
        runepkg_util_error("Realloc size %lu exceeds maximum %d bytes\n",
                          (unsigned long)new_size, RUNEPKG_MAX_ALLOC_SIZE);
        errno = ENOMEM;
        return NULL;
    }
    
    new_ptr = realloc(ptr, new_size);
    if (!new_ptr && new_size > 0) {
        runepkg_util_error("Failed to realloc to %lu bytes\n", (unsigned long)new_size);
        return NULL;
    }
    
#ifdef RUNEPKG_DEBUG_MEMORY
    runepkg_util_log_debug("Realloced %p to %lu bytes -> %p\n", ptr, (unsigned long)new_size, new_ptr);
#endif
    
    return new_ptr;
}

char* runepkg_secure_strdup(const char* str) {
    size_t len;
    char* dup;

    if (!str) {
        runepkg_util_error("Attempted to strdup NULL string\n");
        errno = EINVAL;
        return NULL;
    }
    
    len = strlen(str);
    if (len > RUNEPKG_MAX_STRING_LEN) {
        runepkg_util_error("String length %lu exceeds maximum %d\n",
                          (unsigned long)len, RUNEPKG_MAX_STRING_LEN);
        errno = ENOMEM;
        return NULL;
    }
    
    dup = runepkg_secure_malloc(len + 1);
    if (!dup) {
        return NULL;
    }
    
    memcpy(dup, str, len + 1);
    return dup;
}

char* runepkg_secure_strndup(const char* str, size_t max_len) {
    size_t len = 0;
    char* dup;

    if (!str) {
        runepkg_util_error("Attempted to strndup NULL string\n");
        errno = EINVAL;
        return NULL;
    }
    
    if (max_len > RUNEPKG_MAX_STRING_LEN) {
        runepkg_util_error("Max length %lu exceeds maximum %d\n",
                          (unsigned long)max_len, RUNEPKG_MAX_STRING_LEN);
        errno = ENOMEM;
        return NULL;
    }
    
    /* Find actual length, capped at max_len */
    while (len < max_len && str[len] != '\0') {
        len++;
    }
    
    dup = runepkg_secure_malloc(len + 1);
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
    
    /* Optional: Wipe memory for security */
    if (size > 0) {
        memset(*ptr, 0, size);
    }
    
#ifdef RUNEPKG_DEBUG_MEMORY
    runepkg_util_log_debug("Freeing %p (size: %lu)\n", *ptr, (unsigned long)size);
    allocation_count--;
#endif
    
    free(*ptr);
    *ptr = NULL;
}

/* --- Secure String Operations --- */

runepkg_error_t runepkg_secure_strcpy(char* dest, size_t dest_size, const char* src) {
    size_t src_len;

    if (!dest || !src) {
        runepkg_util_error("NULL pointer in secure_strcpy\n");
        return RUNEPKG_ERROR_NULL_POINTER;
    }
    
    if (dest_size == 0) {
        runepkg_util_error("Zero destination size in secure_strcpy\n");
        return RUNEPKG_ERROR_INVALID_SIZE;
    }
    
    src_len = strlen(src);
    if (src_len >= dest_size) {
        runepkg_util_error("Source string too long for destination: %lu >= %lu\n",
                          (unsigned long)src_len, (unsigned long)dest_size);
        return RUNEPKG_ERROR_BUFFER_OVERFLOW;
    }
    
    memcpy(dest, src, src_len + 1);
    return RUNEPKG_SUCCESS;
}

runepkg_error_t runepkg_secure_strcat(char* dest, size_t dest_size, const char* src) {
    size_t dest_len;
    size_t src_len;

    if (!dest || !src) {
        runepkg_util_error("NULL pointer in secure_strcat\n");
        return RUNEPKG_ERROR_NULL_POINTER;
    }
    
    if (dest_size == 0) {
        runepkg_util_error("Zero destination size in secure_strcat\n");
        return RUNEPKG_ERROR_INVALID_SIZE;
    }
    
    dest_len = strlen(dest);
    src_len = strlen(src);
    
    if (dest_len >= dest_size) {
        runepkg_util_error("Destination already full: %lu >= %lu\n", (unsigned long)dest_len, (unsigned long)dest_size);
        return RUNEPKG_ERROR_BUFFER_OVERFLOW;
    }
    
    if (dest_len + src_len >= dest_size) {
        runepkg_util_error("Combined string too long: %lu + %lu >= %lu\n",
                          (unsigned long)dest_len, (unsigned long)src_len, (unsigned long)dest_size);
        return RUNEPKG_ERROR_BUFFER_OVERFLOW;
    }
    
    memcpy(dest + dest_len, src, src_len + 1);
    return RUNEPKG_SUCCESS;
}

char* runepkg_secure_path_concat(const char* dir, const char* file) {
    size_t dir_len;
    size_t file_len;
    bool is_traversal = false;
    size_t flen;
    bool needs_slash;
    size_t total_len;
    char* full_path;
    runepkg_error_t err;

    if (!dir || !file) {
        runepkg_util_error("NULL pointer in path concatenation\n");
        return NULL;
    }
    
    /* Normalize leading slashes or ./ from file path */
    while (file[0] == '/' || (file[0] == '.' && file[1] == '/')) {
        if (file[0] == '/') {
            while (*file == '/') file++;
        } else if (file[0] == '.' && file[1] == '/') {
            file += 2;
            while (*file == '/') file++;
        }
    }

    dir_len = strlen(dir);
    file_len = strlen(file);
    
    if (dir_len > RUNEPKG_MAX_PATH_LEN || file_len > RUNEPKG_MAX_PATH_LEN) {
        runepkg_util_error("Path component too long: dir=%lu, file=%lu (max=%d)\n",
                          (unsigned long)dir_len, (unsigned long)file_len, RUNEPKG_MAX_PATH_LEN);
        return NULL;
    }
    
    /* Check for directory traversal attempts */
    if (strcmp(file, "..") == 0 || strncmp(file, "../", 3) == 0 ||
        strstr(file, "/../") != NULL) {
        is_traversal = true;
    } else {
        flen = strlen(file);
        if (flen >= 3 && strcmp(file + flen - 3, "/..") == 0) is_traversal = true;
    }

    if (is_traversal || strstr(file, "//")) {
        runepkg_util_error("Suspicious file path: %s\n", file);
        return NULL;
    }
    
    needs_slash = (dir_len > 0 && dir[dir_len - 1] != '/');
    total_len = dir_len + file_len + (needs_slash ? 1 : 0) + 1;
    
    if (total_len > RUNEPKG_MAX_PATH_LEN) {
        runepkg_util_error("Combined path too long: %lu > %d\n", (unsigned long)total_len, RUNEPKG_MAX_PATH_LEN);
        return NULL;
    }
    
    full_path = runepkg_secure_malloc(total_len);
    if (!full_path) {
        return NULL;
    }
    
    err = runepkg_secure_strcpy(full_path, total_len, dir);
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
    char* buffer;
    va_list args;
    int result;

    if (!format) {
        runepkg_util_error("NULL format in secure_sprintf\n");
        return NULL;
    }
    
    if (max_len > RUNEPKG_MAX_STRING_LEN) {
        runepkg_util_error("Max length %lu exceeds limit %d\n", (unsigned long)max_len, RUNEPKG_MAX_STRING_LEN);
        return NULL;
    }
    
    buffer = runepkg_secure_malloc(max_len + 1);
    if (!buffer) {
        return NULL;
    }
    
    va_start(args, format);
    result = vsnprintf(buffer, max_len + 1, format, args);
    va_end(args);
    
    if (result < 0 || (size_t)result > max_len) {
        runepkg_util_error("sprintf formatting failed or truncated\n");
        runepkg_secure_free((void**)&buffer, max_len + 1);
        return NULL;
    }
    
    return buffer;
}

int runepkg_secure_snprintf(char* dest, size_t dest_size, const char* format, ...) {
    va_list args;
    int result;

    if (!dest || !format || dest_size == 0) {
        return -1;
    }

    va_start(args, format);
    result = vsnprintf(dest, dest_size, format, args);
    va_end(args);

    if (result < 0 || (size_t)result >= dest_size) {
        runepkg_util_error("snprintf formatting failed or truncated\n");
        if (dest_size > 0) dest[dest_size - 1] = '\0';
        return -1;
    }

    return result;
}

/* --- Input Validation --- */

runepkg_error_t runepkg_validate_pointer(const void* ptr, const char* name) {
    if (!ptr) {
        runepkg_util_security_blocked("NULL pointer: %s\n", name ? name : "unknown");
        return RUNEPKG_ERROR_NULL_POINTER;
    }
    return RUNEPKG_SUCCESS;
}

runepkg_error_t runepkg_validate_string(const char* str, size_t max_len, const char* name) {
    runepkg_error_t err = runepkg_validate_pointer(str, name);
    size_t len;
    if (err != RUNEPKG_SUCCESS) {
        return err;
    }
    
    len = strlen(str);
    if (len > max_len) {
        runepkg_util_error("String %s too long: %lu > %lu\n",
                          name ? name : "unknown", (unsigned long)len, (unsigned long)max_len);
        return RUNEPKG_ERROR_SIZE_LIMIT;
    }
    
    return RUNEPKG_SUCCESS;
}

runepkg_error_t runepkg_validate_size(size_t size, size_t max_size, const char* name) {
    if (size > max_size) {
        runepkg_util_error("Size %s too large: %lu > %lu\n",
                          name ? name : "unknown", (unsigned long)size, (unsigned long)max_size);
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
    
    /* Check for path traversal attacks */
    if (strstr(path, "..")) {
        if (strcmp(path, "..") == 0 || strncmp(path, "../", 3) == 0 ||
            strstr(path, "/../") != NULL) {
            runepkg_util_security_blocked("path traversal attempt: %s\n", path);
            return RUNEPKG_ERROR_INVALID_INPUT;
        }

        {
            size_t plen = strlen(path);
            if (plen >= 3 && strcmp(path + plen - 3, "/..") == 0) {
                runepkg_util_security_blocked("path traversal attempt: %s\n", path);
                return RUNEPKG_ERROR_INVALID_INPUT;
            }
        }
    }
    
    if (strstr(path, "//")) {
        runepkg_util_security_blocked("multiple slashes in path: %s\n", path);
        return RUNEPKG_ERROR_INVALID_INPUT;
    }

    return RUNEPKG_SUCCESS;
}

/* --- Secure File Operations --- */

char* runepkg_secure_read_file(const char* filepath, size_t max_size, size_t* out_size) {
    runepkg_error_t err = runepkg_validate_path(filepath);
    FILE* file;
    long file_size;
    char* buffer;
    size_t bytes_read;

    if (err != RUNEPKG_SUCCESS) {
        return NULL;
    }
    
    if (max_size > RUNEPKG_MAX_ALLOC_SIZE) {
        runepkg_util_error("Max file size %lu exceeds limit %d\n", (unsigned long)max_size, RUNEPKG_MAX_ALLOC_SIZE);
        return NULL;
    }
    
    file = fopen(filepath, "rb");
    if (!file) {
        runepkg_util_error("Failed to open file: %s (errno: %d)\n", filepath, errno);
        return NULL;
    }
    
    if (fseek(file, 0, SEEK_END) != 0) {
        runepkg_util_error("Failed to seek to end of file: %s\n", filepath);
        fclose(file);
        return NULL;
    }
    
    file_size = ftell(file);
    if (file_size < 0) {
        runepkg_util_error("Failed to get file size: %s\n", filepath);
        fclose(file);
        return NULL;
    }
    
    if ((size_t)file_size > max_size) {
        runepkg_util_error("File too large: %ld > %lu\n", file_size, (unsigned long)max_size);
        fclose(file);
        return NULL;
    }
    
    rewind(file);
    
    buffer = runepkg_secure_malloc(file_size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    
    bytes_read = fread(buffer, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != (size_t)file_size) {
        runepkg_util_error("Failed to read complete file: %lu != %ld\n", (unsigned long)bytes_read, file_size);
        runepkg_secure_free((void**)&buffer, file_size + 1);
        return NULL;
    }
    
    buffer[file_size] = '\0';
    
    if (out_size) {
        *out_size = bytes_read;
    }
    
    return buffer;
}

/* --- Error Messages --- */

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

/* --- Memory Debugging --- */

#ifdef RUNEPKG_DEBUG_MEMORY
void runepkg_memory_stats(void) {
    printf("=== Memory Statistics ===\n");
    printf("Total allocated: %lu bytes\n", (unsigned long)total_allocated);
    printf("Active allocations: %lu\n", (unsigned long)allocation_count);
    printf("========================\n");
}

size_t runepkg_memory_usage(void) {
    return total_allocated;
}
#endif
