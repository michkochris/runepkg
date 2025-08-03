#include "upkg_highlight.h"
#include "upkg_hash.h" // For new logging function prototypes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h> // For isspace
#include <errno.h> // For strerror

// --- Global Scheme Definitions ---

// Nano-like scheme (common default colors for sh.nanorc)
const HighlightScheme NANO_HIGHLIGHT_SCHEME = {
    .default_color  = ANSI_COLOR_RESET,
    .comment_color  = ANSI_COLOR_BRIGHT_GREEN,
    .string_color   = ANSI_COLOR_YELLOW,
    .keyword_color  = ANSI_COLOR_BRIGHT_BLUE, // Will implement later
    .variable_color = ANSI_COLOR_CYAN,        // Will implement later
    .number_color   = ANSI_COLOR_MAGENTA,     // Will implement later
    .operator_color = ANSI_COLOR_WHITE,       // Will implement later
    .shebang_color  = ANSI_COLOR_BRIGHT_RED   // Distinct color for shebang
};

// Vim-like scheme (a common "dark theme" feel for Vim shell syntax)
const HighlightScheme VIM_HIGHLIGHT_SCHEME = {
    .default_color  = ANSI_COLOR_RESET,
    .comment_color  = ANSI_COLOR_GREEN,       // Slightly less bright than Nano's
    .string_color   = ANSI_COLOR_YELLOW,      // Often similar, or can be a darker yellow/orange
    .keyword_color  = ANSI_COLOR_BLUE,        // Darker blue
    .variable_color = ANSI_COLOR_MAGENTA,     // Often magenta or red
    .number_color   = ANSI_COLOR_CYAN,        // Often cyan or blue
    .operator_color = ANSI_COLOR_BRIGHT_WHITE, // White for operators
    .shebang_color  = ANSI_COLOR_BRIGHT_MAGENTA // A different distinct color
};


// Internal helper for appending to a dynamic buffer
// Reallocates if needed. Returns new buffer pointer (or NULL on failure).
// updates current_len and allocated_size.
static char *append_to_buffer(char *buffer, size_t *current_len, size_t *allocated_size,
                              const char *str_to_add, size_t str_len_to_add) {
    if (buffer == NULL) { // Handle initial allocation
        *allocated_size = str_len_to_add + 512; // Start with some extra space
        buffer = (char *)malloc(*allocated_size);
        if (buffer == NULL) {
            upkg_log_debug("Error: Initial memory allocation failed in append_to_buffer: %s\n", strerror(errno));
            return NULL;
        }
        *current_len = 0;
    }

    // Ensure enough space including the null terminator
    if (*current_len + str_len_to_add + 1 > *allocated_size) {
        // Double the size, but ensure it's at least enough for current + new_text + NULL
        size_t required_size = *current_len + str_len_to_add + 1;
        *allocated_size = (*allocated_size * 2 > required_size) ? (*allocated_size * 2) : required_size;
        char *new_buffer = (char *)realloc(buffer, *allocated_size);
        if (new_buffer == NULL) {
            upkg_log_debug("Error: Memory reallocation failed in append_to_buffer: %s\n", strerror(errno));
            free(buffer); // Free the old buffer on realloc failure
            return NULL;
        }
        buffer = new_buffer;
    }

    memcpy(buffer + *current_len, str_to_add, str_len_to_add);
    *current_len += str_len_to_add;
    buffer[*current_len] = '\0'; // Always keep null-terminated
    return buffer;
}


// New helper function to apply color based on token type
static char *append_colored_text(char *buffer, size_t *current_len, size_t *allocated_size,
                                 const char *text, size_t text_len, const char *color_code) {
    // Only apply color code if it's not NULL, not empty, and not the reset code itself (unless explicitly passed to reset)
    if (color_code != NULL && strlen(color_code) > 0 && strcmp(color_code, ANSI_COLOR_RESET) != 0) {
        buffer = append_to_buffer(buffer, current_len, allocated_size, color_code, strlen(color_code));
        if (buffer == NULL) return NULL;
    } else if (color_code != NULL && strcmp(color_code, ANSI_COLOR_RESET) == 0) {
        // Explicitly applying reset color
        // FIX: Removed the '&' from &allocated_size.
        // `append_to_buffer` expects a `size_t *`, and `allocated_size` is already that.
        buffer = append_to_buffer(buffer, current_len, allocated_size, ANSI_COLOR_RESET, strlen(ANSI_COLOR_RESET));
        if (buffer == NULL) return NULL;
    }


    if (text != NULL && text_len > 0) {
        // FIX: Corrected the typo `allocated_output_size` to `allocated_size`.
        buffer = append_to_buffer(buffer, current_len, allocated_size, text, text_len);
        if (buffer == NULL) return NULL;
    }

    // IMPORTANT: Do NOT apply reset here if `apply_reset` was removed.
    // The highlighting logic depends on the color state persisting until explicitly changed.
    return buffer;
}


// Main highlighting function, now scheme-aware
char *highlight_shell_script(const char *script_content, int script_len, HighlightSchemeType scheme_type) {
    if (script_content == NULL || script_len <= 0) {
        // Initialize an empty string, allocate space for at least a null terminator
        char *empty_str = (char *)malloc(1);
        if (empty_str == NULL) {
            upkg_log_debug("Error: Memory allocation failed for empty string in highlight_shell_script: %s\n", strerror(errno));
        } else {
            *empty_str = '\0';
        }
        return empty_str; // Return an empty string or NULL on failure
    }

    // Select the appropriate scheme
    const HighlightScheme *current_scheme;
    switch (scheme_type) {
        case HIGHLIGHT_SCHEME_NANO:
            current_scheme = &NANO_HIGHLIGHT_SCHEME;
            break;
        case HIGHLIGHT_SCHEME_VIM:
            current_scheme = &VIM_HIGHLIGHT_SCHEME;
            break;
        case HIGHLIGHT_SCHEME_DEFAULT: // Fallback if needed, or point to one
        default:
            current_scheme = &NANO_HIGHLIGHT_SCHEME; // Default to Nano if unknown
            break;
    }

    char *highlighted_output = NULL;
    size_t current_output_len = 0;
    size_t allocated_output_size = 0;

    bool in_single_quote = false;
    bool in_double_quote = false;
    bool in_comment = false; // For line comments
    // bool is_shebang_line = true; // REMOVED: This variable was unused.

    // Initialize with default color just in case the script is empty or starts with non-special char
    highlighted_output = append_colored_text(highlighted_output, &current_output_len, &allocated_output_size,
                                              "", 0, current_scheme->default_color);
    if (highlighted_output == NULL) {
        upkg_log_debug("Error: Initial color application failed in highlight_shell_script.\n");
        return NULL;
    }


    for (int i = 0; i < script_len; ++i) {
        char current_char = script_content[i];

        // --- Handle state transitions ---
        if (in_comment) {
            // Stay in comment until newline
            if (current_char == '\n') {
                // Append the char (newline), then reset color
                highlighted_output = append_colored_text(highlighted_output, &current_output_len, &allocated_output_size,
                                                          &current_char, 1, NULL); // No color applied to newline itself
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while appending newline.\n"); return NULL; }
                highlighted_output = append_colored_text(highlighted_output, &current_output_len, &allocated_output_size,
                                                          "", 0, current_scheme->default_color); // Apply reset
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while resetting color after newline.\n"); return NULL; }
                in_comment = false;
            } else {
                 // Append character (already in comment color from when `in_comment` was set)
                highlighted_output = append_to_buffer(highlighted_output, &current_output_len, &allocated_output_size,
                                                      &current_char, 1);
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while appending comment character.\n"); return NULL; }
            }
        } else if (in_single_quote) {
            if (current_char == '\'') {
                // Append the quote, then reset color
                highlighted_output = append_colored_text(highlighted_output, &current_output_len, &allocated_output_size,
                                                          &current_char, 1, NULL); // No color applied to quote itself
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while appending single quote.\n"); return NULL; }
                highlighted_output = append_colored_text(highlighted_output, &current_output_len, &allocated_output_size,
                                                          "", 0, current_scheme->default_color); // Apply reset
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while resetting color after single quote.\n"); return NULL; }
                in_single_quote = false;
            } else {
                // Append character within string (already in string color from when `in_single_quote` was set)
                highlighted_output = append_to_buffer(highlighted_output, &current_output_len, &allocated_output_size,
                                                      &current_char, 1);
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while appending single-quote string character.\n"); return NULL; }
            }
        } else if (in_double_quote) {
            if (current_char == '"') {
                // Append the quote, then reset color
                highlighted_output = append_colored_text(highlighted_output, &current_output_len, &allocated_output_size,
                                                          &current_char, 1, NULL); // No color applied to quote itself
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while appending double quote.\n"); return NULL; }
                highlighted_output = append_colored_text(highlighted_output, &current_output_len, &allocated_output_size,
                                                          "", 0, current_scheme->default_color); // Apply reset
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while resetting color after double quote.\n"); return NULL; }
                in_double_quote = false;
            } else if (current_char == '\\' && i + 1 < script_len) {
                // Handle escaped characters within double quotes (e.g., \", \\)
                // Append current char (\), then increment i and append escaped char.
                // These are still within the string context, so no color change.
                highlighted_output = append_to_buffer(highlighted_output, &current_output_len, &allocated_output_size,
                                                      &current_char, 1); // The backslash
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while appending backslash in double-quote string.\n"); return NULL; }
                i++; // Move to the escaped character
                current_char = script_content[i]; // Get the escaped char
                highlighted_output = append_to_buffer(highlighted_output, &current_output_len, &allocated_output_size,
                                                      &current_char, 1); // The escaped char
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while appending escaped character in double-quote string.\n"); return NULL; }
            } else {
                // Append character within string (already in string color)
                highlighted_output = append_to_buffer(highlighted_output, &current_output_len, &allocated_output_size,
                                                      &current_char, 1);
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while appending double-quote string character.\n"); return NULL; }
            }
        } else { // Normal code state, check for new state beginnings
            if (current_char == '#') {
                // Check for shebang: #! at the very start of the script
                // (Only applies to the very first two characters of the entire script)
                if (i == 0 && script_len > 1 && script_content[1] == '!') {
                    // Start of shebang line: apply shebang color
                    highlighted_output = append_colored_text(highlighted_output, &current_output_len, &allocated_output_size,
                                                              &current_char, 1, current_scheme->shebang_color);
                    if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while applying shebang color.\n"); return NULL; }
                } else {
                    // Start of a regular line comment: apply comment color
                    in_comment = true;
                    highlighted_output = append_colored_text(highlighted_output, &current_output_len, &allocated_output_size,
                                                              &current_char, 1, current_scheme->comment_color);
                    if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while applying comment color.\n"); return NULL; }
                }
            } else if (current_char == '\'') {
                in_single_quote = true;
                highlighted_output = append_colored_text(highlighted_output, &current_output_len, &allocated_output_size,
                                                          &current_char, 1, current_scheme->string_color);
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while applying single-quote color.\n"); return NULL; }
            } else if (current_char == '"') {
                in_double_quote = true;
                highlighted_output = append_colored_text(highlighted_output, &current_output_len, &allocated_output_size,
                                                          &current_char, 1, current_scheme->string_color);
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while applying double-quote color.\n"); return NULL; }
            } else if (current_char == '\n') {
                // Always append newline and reset color after it in normal state
                highlighted_output = append_colored_text(highlighted_output, &current_output_len, &allocated_output_size,
                                                          &current_char, 1, NULL); // Append newline char
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while appending newline in normal state.\n"); return NULL; }
                highlighted_output = append_colored_text(highlighted_output, &current_output_len, &allocated_output_size,
                                                          "", 0, current_scheme->default_color); // Reset color
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while resetting color in normal state.\n"); return NULL; }
                continue; // Skip the default append below as we already added newline and reset
            } else {
                // Normal character (not a special state marker)
                // Append character with default color
                highlighted_output = append_colored_text(highlighted_output, &current_output_len, &allocated_output_size,
                                                          &current_char, 1, current_scheme->default_color);
                if (highlighted_output == NULL) { upkg_log_debug("Error: Memory allocation failed while appending default character.\n"); return NULL; }
            }
        }
        // NOTE: The `append_colored_text` calls now handle both color and character appending.
        // The original `append_to_buffer` call for `current_char` at the end of the loop
        // is no longer needed here, as the character is already processed by the above `if/else if` chain.
    }

    // Ensure reset at the very end in case a state was left open (e.g., unterminated string/comment)
    if (in_single_quote || in_double_quote || in_comment) {
        highlighted_output = append_colored_text(highlighted_output, &current_output_len, &allocated_output_size,
                                                  "", 0, current_scheme->default_color);
        // Do not return NULL here, as we are cleaning up. Just let it finish.
    }

    return highlighted_output;
}
