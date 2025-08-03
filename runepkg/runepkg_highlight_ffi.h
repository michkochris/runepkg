/******************************************************************************
 * Filename:    runepkg_highlight_ffi.h
 * Author:      <michkochris@gmail.com>
 * Date:        started 01-03-2025
 * Description: C header for Rust FFI highlighting and script utilities in runepkg
 *
 * Copyright (c) 2025 runepkg (ulinux) All rights reserved.
 * GPLV3
 ******************************************************************************/

#ifndef RUNEPKG_HIGHLIGHT_FFI_H
#define RUNEPKG_HIGHLIGHT_FFI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Highlight Scheme Types ---
typedef enum {
    RUST_HIGHLIGHT_SCHEME_NANO = 0,
    RUST_HIGHLIGHT_SCHEME_VIM = 1,
    RUST_HIGHLIGHT_SCHEME_DEFAULT = 2
} RustHighlightScheme;

// --- Script Type Detection ---
typedef enum {
    RUST_SCRIPT_TYPE_SHELL = 0,
    RUST_SCRIPT_TYPE_PYTHON = 1,
    RUST_SCRIPT_TYPE_PERL = 2,
    RUST_SCRIPT_TYPE_RUBY = 3,
    RUST_SCRIPT_TYPE_UNKNOWN = 4
} RustScriptType;

// --- Core Highlighting Functions ---

/**
 * @brief Highlight shell script content using Rust-based syntax highlighter
 * @param script_content Pointer to script content (null-terminated string)
 * @param script_len Length of script content in bytes
 * @param scheme Highlighting scheme to use
 * @return Pointer to highlighted string with ANSI codes, or NULL on error
 * @note Returned string must be freed with rust_free_highlighted_string()
 */
char* rust_highlight_shell_script(const char* script_content, int script_len, RustHighlightScheme scheme);

/**
 * @brief Free memory allocated by rust_highlight_shell_script
 * @param ptr Pointer returned by rust_highlight_shell_script
 */
void rust_free_highlighted_string(char* ptr);

/**
 * @brief Initialize the highlighting system
 * @return 1 on success, 0 on failure
 */
int rust_init_highlighting(void);

/**
 * @brief Get number of available themes
 * @return Number of themes available for highlighting
 */
int rust_get_theme_count(void);

/**
 * @brief Get theme name by index
 * @param index Theme index (0 to rust_get_theme_count()-1)
 * @return Pointer to theme name string (static, do not free)
 */
const char* rust_get_theme_name(int index);

/**
 * @brief Get version information
 * @return Pointer to version string (must be freed with rust_free_string)
 */
const char* rust_get_version(void);

/**
 * @brief Test FFI connection
 * @return 42 if FFI is working correctly
 */
int rust_test_ffi(void);

// --- Script Execution Functions ---

/**
 * @brief Execute script from memory with optional highlighting
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @param enable_highlighting 1 to enable highlighting, 0 to disable
 * @return 0 on success, non-zero on error
 */
int rust_execute_script_from_memory(const char* script_content, int script_len, int enable_highlighting);

/**
 * @brief Parse shebang line and extract interpreter path
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @return Pointer to interpreter path string, or NULL on error
 * @note Returned string must be freed with rust_free_string()
 */
char* rust_parse_shebang(const char* script_content, int script_len);

/**
 * @brief Parse shebang with all arguments
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @param max_args Maximum number of arguments to parse
 * @return Pointer to NULL-terminated array of strings, or NULL on error
 * @note Returned array must be freed with rust_free_string_array()
 */
char** rust_parse_shebang_with_args(const char* script_content, int script_len, int max_args);

/**
 * @brief Validate script before execution
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @return 1 if valid for execution, 0 if invalid
 */
int rust_validate_script_before_exec(const char* script_content, int script_len);

/**
 * @brief Free string allocated by Rust functions
 * @param ptr Pointer to string allocated by Rust
 */
void rust_free_string(char* ptr);

/**
 * @brief Free string array allocated by rust_parse_shebang_with_args
 * @param ptr Pointer to string array allocated by Rust
 */
void rust_free_string_array(char** ptr);

// --- Script Utility Functions ---

/**
 * @brief Detect script type from content
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @return Script type enumeration value
 */
RustScriptType rust_detect_script_type(const char* script_content, int script_len);

/**
 * @brief Validate script syntax (comprehensive validation)
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @return 1 if syntax appears valid, 0 if invalid
 */
int rust_validate_script_syntax(const char* script_content, int script_len);

/**
 * @brief Extract metadata from script comments
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @return Pointer to metadata string, or NULL on error
 * @note Returned string must be freed with rust_free_string()
 */
char* rust_extract_script_metadata(const char* script_content, int script_len);

/**
 * @brief Get script statistics
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @return Pointer to statistics string, or NULL on error
 * @note Returned string must be freed with rust_free_string()
 */
char* rust_get_script_stats(const char* script_content, int script_len);

// --- Convenience Macros ---

/**
 * @brief Check if FFI is available and working
 */
#define RUNEPKG_FFI_IS_AVAILABLE() (rust_test_ffi() == 42)

/**
 * @brief Safe highlighting with fallback
 */
#define RUNEPKG_HIGHLIGHT_SAFE(content, len, scheme) \
    (RUNEPKG_FFI_IS_AVAILABLE() ? rust_highlight_shell_script(content, len, scheme) : NULL)

/**
 * @brief Initialize highlighting if not already done
 */
#define RUNEPKG_INIT_HIGHLIGHTING() \
    do { \
        static int initialized = 0; \
        if (!initialized && RUNEPKG_FFI_IS_AVAILABLE()) { \
            rust_init_highlighting(); \
            initialized = 1; \
        } \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif // RUNEPKG_HIGHLIGHT_FFI_H
