/*******************************************************************    // Check if Rust FFI is available
    if (!runepkg_rust_highlighting_available()) {
        runepkg_util_log_debug("Rust FFI not available, using basic fallback highlighting\n");
        return runepkg_highlight_script_fallback(script_content, script_len, scheme);*******
 * Filename:    runepkg_highlight_rust.c
 * Author:      <michkochris@gmail.com>
 * Date:        started 01-03-2025
 * Description: C wrapper for Rust-based highlighting functionality in runepkg
 *
 * Copyright (c) 2025 runepkg (ulinux) All rights reserved.
 * GPLV3
 ******************************************************************************/

#include "runepkg_highlight_rust.h"
#include "runepkg_highlight_ffi.h"
#include "runepkg_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global flag to track FFI availability
static int ffi_available = -1; // -1 = not tested, 0 = unavailable, 1 = available
static int ffi_initialized = 0; // Track if FFI is initialized

/**
 * @brief Check if Rust FFI highlighting is available
 * @return 1 if available, 0 if not
 */
int runepkg_rust_highlighting_available(void) {
    if (ffi_available == -1) {
        // Test FFI availability
        ffi_available = (rust_test_ffi() == 42) ? 1 : 0;
        if (ffi_available) {
            runepkg_util_log_verbose("Rust FFI highlighting is available\n");
            // Initialize highlighting system
            if (!ffi_initialized) {
                rust_init_highlighting();
                ffi_initialized = 1;
                runepkg_util_log_verbose("Rust highlighting system initialized\n");
            }
        } else {
            runepkg_util_log_verbose("Rust FFI highlighting is not available, using fallback\n");
        }
    }
    return ffi_available;
}

/**
 * @brief Convert C highlight scheme to Rust scheme
 */
static RustHighlightScheme convert_scheme_to_rust(runepkg_highlight_scheme_t scheme) {
    switch (scheme) {
        case RUNEPKG_HIGHLIGHT_NANO:
            return RUST_HIGHLIGHT_SCHEME_NANO;
        case RUNEPKG_HIGHLIGHT_VIM:
            return RUST_HIGHLIGHT_SCHEME_VIM;
        case RUNEPKG_HIGHLIGHT_DEFAULT:
        default:
            return RUST_HIGHLIGHT_SCHEME_DEFAULT;
    }
}

/**
 * @brief Highlight script using Rust FFI with fallback
 */
char* runepkg_highlight_script_rust(const char* script_content, int script_len, runepkg_highlight_scheme_t scheme) {
    if (!script_content || script_len <= 0) {
        runepkg_util_log_debug("Invalid script content provided to runepkg_highlight_script_rust\n");
        return NULL;
    }

    // Check if Rust FFI is available
    if (!runepkg_rust_highlighting_available()) {
        runepkg_util_log_debug("Rust FFI not available, using basic fallback highlighting\n");
        return runepkg_highlight_script_basic_fallback(script_content, script_len, scheme);
    }

    // Use Rust highlighting
    RustHighlightScheme rust_scheme = convert_scheme_to_rust(scheme);
    char* result = rust_highlight_shell_script(script_content, script_len, rust_scheme);
    
    if (!result) {
        runepkg_util_log_debug("Rust highlighting failed, using fallback\n");
        return runepkg_highlight_script_basic_fallback(script_content, script_len, scheme);
    }

    runepkg_util_log_verbose("Successfully highlighted script using Rust FFI (%d chars -> %d chars)\n", 
                        script_len, (int)strlen(result));
    return result;
}

/**
 * @brief Free Rust-highlighted string
 */
void runepkg_free_rust_highlighted_string(char* ptr) {
    if (ptr && runepkg_rust_highlighting_available()) {
        rust_free_highlighted_string(ptr);
    } else if (ptr) {
        // Fallback string, use regular free
        free(ptr);
    }
}

/**
 * @brief Basic fallback highlighting when Rust FFI is not available
 * Enhanced version with better color support
 */
char* runepkg_highlight_script_basic_fallback(const char* script_content, int script_len, runepkg_highlight_scheme_t scheme) {
    if (!script_content || script_len <= 0) {
        return NULL;
    }

    // Color schemes based on the scheme parameter
    const char* color_comment;
    const char* color_string;
    const char* color_shebang;
    const char* color_reset = "\x1b[0m";

    switch (scheme) {
        case RUNEPKG_HIGHLIGHT_NANO:
            color_comment = "\x1b[92m";    // Bright Green
            color_string = "\x1b[33m";     // Yellow
            color_shebang = "\x1b[91m";    // Bright Red
            break;
        case RUNEPKG_HIGHLIGHT_VIM:
            color_comment = "\x1b[32m";    // Green
            color_string = "\x1b[33m";     // Yellow
            color_shebang = "\x1b[95m";    // Bright Magenta
            break;
        case RUNEPKG_HIGHLIGHT_DEFAULT:
        default:
            color_comment = "\x1b[32m";    // Green
            color_string = "\x1b[33m";     // Yellow
            color_shebang = "\x1b[91m";    // Bright Red
            break;
    }

    // Allocate buffer (generous size for ANSI codes)
    size_t output_size = script_len * 3 + 200; // More space for colors
    char* output = malloc(output_size);
    if (!output) {
        runepkg_util_log_debug("Memory allocation failed in fallback highlighting\n");
        return NULL;
    }

    size_t output_pos = 0;
    int in_comment = 0;
    int in_string = 0;
    int is_shebang_line = 1; // First line might be shebang
    char string_char = 0;

    for (int i = 0; i < script_len && output_pos < output_size - 50; ++i) {
        char c = script_content[i];

        // Check for shebang at start of first line
        if (is_shebang_line && i == 0 && c == '#' && i + 1 < script_len && script_content[i + 1] == '!') {
            // Start shebang highlighting
            strcpy(output + output_pos, color_shebang);
            output_pos += strlen(color_shebang);
            // Continue until end of line
            while (i < script_len && script_content[i] != '\n') {
                output[output_pos++] = script_content[i++];
            }
            if (i < script_len && script_content[i] == '\n') {
                output[output_pos++] = script_content[i];
            }
            strcpy(output + output_pos, color_reset);
            output_pos += strlen(color_reset);
            is_shebang_line = 0;
            continue;
        }

        // State transitions
        if (!in_string && c == '#' && !is_shebang_line) {
            // Start comment
            if (!in_comment) {
                strcpy(output + output_pos, color_comment);
                output_pos += strlen(color_comment);
                in_comment = 1;
            }
        } else if (!in_comment && !in_string && (c == '"' || c == '\'')) {
            // Start string
            strcpy(output + output_pos, color_string);
            output_pos += strlen(color_string);
            in_string = 1;
            string_char = c;
        } else if (in_string && c == string_char) {
            // End string
            output[output_pos++] = c;
            strcpy(output + output_pos, color_reset);
            output_pos += strlen(color_reset);
            in_string = 0;
            continue;
        } else if (in_comment && c == '\n') {
            // End comment
            output[output_pos++] = c;
            strcpy(output + output_pos, color_reset);
            output_pos += strlen(color_reset);
            in_comment = 0;
            is_shebang_line = 0;
            continue;
        }

        // Handle newlines
        if (c == '\n') {
            is_shebang_line = 0;
        }

        // Add the character
        output[output_pos++] = c;
    }

    // Ensure reset at end
    if (in_comment || in_string) {
        strcpy(output + output_pos, color_reset);
        output_pos += strlen(color_reset);
    }

    output[output_pos] = '\0';
    
    runepkg_util_log_verbose("Fallback highlighting completed (%d chars -> %d chars)\n", 
                        script_len, (int)output_pos);
    return output;
}

/**
 * @brief Execute script using Rust FFI
 */
int runepkg_execute_script_rust(const char* script_content, int script_len) {
    if (!script_content || script_len <= 0) {
        runepkg_util_log_debug("Invalid script content provided to runepkg_execute_script_rust\n");
        return -1;
    }

    if (!runepkg_rust_highlighting_available()) {
        runepkg_util_log_debug("Rust FFI not available for script execution\n");
        return -1;
    }

    runepkg_util_log_verbose("Executing script via Rust FFI (%d chars)\n", script_len);
    return rust_execute_script_from_memory(script_content, script_len, 0);
}

/**
 * @brief Parse shebang using Rust FFI
 */
char* runepkg_parse_shebang_rust(const char* script_content, int script_len) {
    if (!script_content || script_len <= 0) {
        return NULL;
    }

    if (!runepkg_rust_highlighting_available()) {
        // Basic fallback shebang parsing
        return runepkg_parse_shebang_fallback(script_content, script_len);
    }

    return rust_parse_shebang(script_content, script_len);
}

/**
 * @brief Fallback shebang parsing
 */
char* runepkg_parse_shebang_fallback(const char* script_content, int script_len) {
    if (!script_content || script_len <= 0) {
        return NULL;
    }

    // Find first line
    const char* line_end = strchr(script_content, '\n');
    int line_len = line_end ? (line_end - script_content) : script_len;
    
    if (line_len < 3 || script_content[0] != '#' || script_content[1] != '!') {
        return NULL; // No shebang
    }

    // Extract interpreter (first word after #!)
    const char* start = script_content + 2; // Skip "#!"
    while (start < script_content + line_len && (*start == ' ' || *start == '\t')) {
        start++; // Skip whitespace
    }

    const char* end = start;
    while (end < script_content + line_len && *end != ' ' && *end != '\t' && *end != '\n') {
        end++; // Find end of interpreter
    }

    if (end <= start) {
        return NULL; // No interpreter found
    }

    // Allocate and copy interpreter
    int interp_len = end - start;
    char* interpreter = malloc(interp_len + 1);
    if (!interpreter) {
        return NULL;
    }

    memcpy(interpreter, start, interp_len);
    interpreter[interp_len] = '\0';

    return interpreter;
}

/**
 * @brief Detect script type using Rust FFI
 */
runepkg_script_type_t runepkg_detect_script_type_rust(const char* script_content, int script_len) {
    if (!script_content || script_len <= 0) {
        return RUNEPKG_SCRIPT_TYPE_UNKNOWN;
    }

    if (!runepkg_rust_highlighting_available()) {
        // Basic fallback detection
        if (script_len >= 11 && strncmp(script_content, "#!/bin/bash", 11) == 0) {
            return RUNEPKG_SCRIPT_TYPE_SHELL;
        }
        if (script_len >= 9 && strncmp(script_content, "#!/bin/sh", 9) == 0) {
            return RUNEPKG_SCRIPT_TYPE_SHELL;
        }
        if (strstr(script_content, "python") != NULL) {
            return RUNEPKG_SCRIPT_TYPE_PYTHON;
        }
        return RUNEPKG_SCRIPT_TYPE_UNKNOWN;
    }

    RustScriptType rust_type = rust_detect_script_type(script_content, script_len);
    switch (rust_type) {
        case RUST_SCRIPT_TYPE_SHELL:
            return RUNEPKG_SCRIPT_TYPE_SHELL;
        case RUST_SCRIPT_TYPE_PYTHON:
            return RUNEPKG_SCRIPT_TYPE_PYTHON;
        case RUST_SCRIPT_TYPE_PERL:
            return RUNEPKG_SCRIPT_TYPE_PERL;
        case RUST_SCRIPT_TYPE_RUBY:
            return RUNEPKG_SCRIPT_TYPE_RUBY;
        default:
            return RUNEPKG_SCRIPT_TYPE_UNKNOWN;
    }
}

/**
 * @brief Validate script syntax using Rust FFI
 */
int runepkg_validate_script_syntax_rust(const char* script_content, int script_len) {
    if (!script_content || script_len <= 0) {
        return 0;
    }

    if (!runepkg_rust_highlighting_available()) {
        // Basic fallback validation - just check if it's not empty and has reasonable content
        return (script_len > 0 && script_content[0] != '\0') ? 1 : 0;
    }

    return rust_validate_script_syntax(script_content, script_len);
}

/**
 * @brief Extract script metadata using Rust FFI
 */
char* runepkg_extract_script_metadata_rust(const char* script_content, int script_len) {
    if (!script_content || script_len <= 0) {
        return NULL;
    }

    if (!runepkg_rust_highlighting_available()) {
        // Basic fallback - just return script type info
        char* metadata = malloc(100);
        if (metadata) {
            snprintf(metadata, 100, "Script type: %s\nLength: %d chars", 
                    "Unknown (Rust FFI unavailable)", script_len);
        }
        return metadata;
    }

    return rust_extract_script_metadata(script_content, script_len);
}

/**
 * @brief Get script statistics using Rust FFI
 */
char* runepkg_get_script_stats_rust(const char* script_content, int script_len) {
    if (!script_content || script_len <= 0) {
        return NULL;
    }

    if (!runepkg_rust_highlighting_available()) {
        // Basic fallback statistics
        char* stats = malloc(200);
        if (stats) {
            int lines = 0;
            for (int i = 0; i < script_len; i++) {
                if (script_content[i] == '\n') lines++;
            }
            snprintf(stats, 200, "Basic Statistics:\nTotal characters: %d\nTotal lines: %d\n(Rust FFI unavailable)", 
                    script_len, lines);
        }
        return stats;
    }

    return rust_get_script_stats(script_content, script_len);
}

/**
 * @brief Get available themes count
 */
int runepkg_get_rust_theme_count(void) {
    if (!runepkg_rust_highlighting_available()) {
        return 3; // Fallback: nano, vim, default
    }
    return rust_get_theme_count();
}

/**
 * @brief Get theme name by index
 */
const char* runepkg_get_rust_theme_name(int index) {
    if (!runepkg_rust_highlighting_available()) {
        // Fallback theme names
        switch (index) {
            case 0: return "nano";
            case 1: return "vim";
            case 2: return "default";
            default: return NULL;
        }
    }
    return rust_get_theme_name(index);
}

/**
 * @brief Get Rust FFI version information
 */
const char* runepkg_get_rust_version(void) {
    if (!runepkg_rust_highlighting_available()) {
        return "Rust FFI not available";
    }
    return rust_get_version();
}
