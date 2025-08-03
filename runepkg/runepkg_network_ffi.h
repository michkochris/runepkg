#ifndef RUNEPKG_NETWORK_FFI_H
#define RUNEPKG_NETWORK_FFI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

// Feature detection
#ifdef ENABLE_CPP_FFI
#define CPP_FFI_AVAILABLE 1
#else
#define CPP_FFI_AVAILABLE 0
#endif

// Error codes
typedef enum {
    CPP_SUCCESS = 0,
    CPP_ERROR_NETWORK = -1,
    CPP_ERROR_DEPENDENCY = -2,
    CPP_ERROR_CACHE = -3,
    CPP_ERROR_SIGNATURE = -4,
    CPP_ERROR_MEMORY = -5,
    CPP_ERROR_FILE_IO = -6,
    CPP_ERROR_JSON_PARSE = -7,
    CPP_ERROR_THREAD = -8,
    CPP_ERROR_NOT_AVAILABLE = -9
} cpp_ffi_error_t;

// Progress callback function pointer
typedef void (*progress_callback_t)(int64_t downloaded, int64_t total, void* user_data);

// Data structures
typedef struct {
    char* name;
    char* version;
    char* description;
    char* url;
    int64_t size;
} package_info_t;

typedef struct {
    char* name;
    char* version;
    char* constraint;  // ">=", "<=", "==", etc.
} dependency_t;

typedef struct {
    dependency_t* packages;
    int count;
    int capacity;
} dependency_list_t;

typedef struct {
    package_info_t* packages;
    int count;
    int capacity;
} search_result_t;

typedef struct {
    char** package_names;
    int count;
    int* priorities;  // Installation order priorities
} install_order_t;

typedef enum {
    CACHE_CLEANUP_LRU,
    CACHE_CLEANUP_SIZE,
    CACHE_CLEANUP_AGE,
    CACHE_CLEANUP_ALL
} cache_cleanup_policy_t;

// Network operations
int cpp_download_package(const char* url, const char* output_path, 
                        progress_callback_t callback, void* user_data);
int cpp_download_parallel(const char* urls[], int count, 
                         const char* output_dir, 
                         progress_callback_t callback, void* user_data);
int cpp_verify_signature(const char* package_path, const char* sig_path);
int cpp_check_network_availability(void);

// Repository management
int cpp_update_repository_metadata(const char* repo_url);
int cpp_search_packages(const char* query, search_result_t** results);
int cpp_get_package_info(const char* package_name, package_info_t* info);
int cpp_add_repository(const char* repo_url, const char* repo_name);
int cpp_remove_repository(const char* repo_name);
int cpp_list_repositories(char*** repo_list, int* count);

// Dependency resolution
int cpp_resolve_dependencies(const char* package_name, 
                            dependency_list_t** deps);
int cpp_check_conflicts(const char* packages[], int count);
int cpp_calculate_install_order(const char* packages[], int count,
                               install_order_t** order);
int cpp_find_circular_dependencies(const char* packages[], int count, 
                                  char*** circular_deps, int* circular_count);

// Cache management
int cpp_initialize_cache(const char* cache_dir, int64_t max_size_mb);
int cpp_cache_package(const char* package_path);
int cpp_get_cached_package(const char* package_name, char* output_path, size_t path_size);
int cpp_cleanup_cache(cache_cleanup_policy_t policy);
int cpp_get_cache_stats(int64_t* total_size, int* package_count, int* hit_rate);

// Memory management
void cpp_free_dependency_list(dependency_list_t* list);
void cpp_free_search_results(search_result_t* results);
void cpp_free_install_order(install_order_t* order);
void cpp_free_package_info(package_info_t* info);
void cpp_free_string_array(char** strings, int count);

// Utility functions
const char* cpp_get_error_string(cpp_ffi_error_t error);
int cpp_get_version_info(char* version_buffer, size_t buffer_size);
int cpp_test_cpp_ffi(void);

// Fallback functions (when C++ FFI not available)
int cpp_fallback_download(const char* url, const char* output_path);
int cpp_fallback_check_dependencies(const char* package_name);

#ifdef __cplusplus
}
#endif

#endif // RUNEPKG_NETWORK_FFI_H
