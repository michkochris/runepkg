/******************************************************************************
 * Filename:    runepkg_highlight_rust.h
 * Author:      <michkochris@gmail.com>
 * Date:        started 01-03-2025
 * Description: Header for C wrapper of Rust-based highlighting functionality in runepkg
 *
 * Copyright (c) 2025 runepkg (ulinux) All rights reserved.
 * GPLV3
 ******************************************************************************/

#ifndef RUNEPKG_HIGHLIGHT_RUST_H
#define RUNEPKG_HIGHLIGHT_RUST_H

#ifdef __cplusplus
extern "C" {
#endif

// --- Highlight Scheme Types for C Integration ---
typedef enum {
    RUNEPKG_HIGHLIGHT_NANO = 0,
    RUNEPKG_HIGHLIGHT_VIM = 1,
    RUNEPKG_HIGHLIGHT_DEFAULT = 2
} runepkg_highlight_scheme_t;

// --- Script Type Detection ---
typedef enum {
    RUNEPKG_SCRIPT_TYPE_SHELL = 0,
    RUNEPKG_SCRIPT_TYPE_PYTHON = 1,
    RUNEPKG_SCRIPT_TYPE_PERL = 2,
    RUNEPKG_SCRIPT_TYPE_RUBY = 3,
    RUNEPKG_SCRIPT_TYPE_UNKNOWN = 4
} runepkg_script_type_t;

// --- Core Functions ---

/**
 * @brief Check if Rust FFI highlighting is available
 * @return 1 if available, 0 if not
 */
int runepkg_rust_highlighting_available(void);

/**
 * @brief Highlight script using Rust FFI with automatic fallback
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @param scheme Highlighting scheme to use
 * @return Highlighted string or NULL on error
 * @note Must be freed with runepkg_free_rust_highlighted_string()
 */
char* runepkg_highlight_script_rust(const char* script_content, int script_len, runepkg_highlight_scheme_t scheme);

/**
 * @brief Free highlighted string returned by runepkg_highlight_script_rust
 * @param ptr Pointer to highlighted string
 */
void runepkg_free_rust_highlighted_string(char* ptr);

/**
 * @brief Basic fallback highlighting when Rust FFI is not available
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @param scheme Highlighting scheme (limited in fallback mode)
 * @return Basic highlighted string or NULL on error
 */
char* runepkg_highlight_script_basic_fallback(const char* script_content, int script_len, runepkg_highlight_scheme_t scheme);

/**
 * @brief Execute script using Rust FFI
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @return 0 on success, non-zero on error
 */
int runepkg_execute_script_rust(const char* script_content, int script_len);

/**
 * @brief Parse shebang using Rust FFI
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @return Interpreter path string (must be freed) or NULL
 */
char* runepkg_parse_shebang_rust(const char* script_content, int script_len);

/**
 * @brief Fallback shebang parsing (when Rust unavailable)
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @return Interpreter path string (must be freed) or NULL
 */
char* runepkg_parse_shebang_fallback(const char* script_content, int script_len);

/**
 * @brief Detect script type using Rust FFI
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @return Script type
 */
runepkg_script_type_t runepkg_detect_script_type_rust(const char* script_content, int script_len);

/**
 * @brief Validate script syntax using Rust FFI
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @return 1 if valid, 0 if invalid
 */
int runepkg_validate_script_syntax_rust(const char* script_content, int script_len);

/**
 * @brief Extract metadata from script using Rust FFI
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @return Metadata string (must be freed) or NULL
 */
char* runepkg_extract_script_metadata_rust(const char* script_content, int script_len);

/**
 * @brief Get script statistics using Rust FFI
 * @param script_content Pointer to script content
 * @param script_len Length of script content
 * @return Statistics string (must be freed) or NULL
 */
char* runepkg_get_script_stats_rust(const char* script_content, int script_len);

/**
 * @brief Get number of available themes
 * @return Theme count
 */
int runepkg_get_rust_theme_count(void);

/**
 * @brief Get theme name by index
 * @param index Theme index
 * @return Theme name or NULL
 */
const char* runepkg_get_rust_theme_name(int index);

/**
 * @brief Get Rust FFI version information
 * @return Version string (do not free)
 */
const char* runepkg_get_rust_version(void);

#ifdef __cplusplus
}
#endif

#endif // RUNEPKG_HIGHLIGHT_RUST_H
