#ifndef UPKG_HIGHLIGHT_H
#define UPKG_HIGHLIGHT_H

// --- ANSI Escape Codes for Text Coloring ---
// Basic colors
#define ANSI_COLOR_RESET    "\x1b[0m"
#define ANSI_COLOR_BLACK    "\x1b[30m"
#define ANSI_COLOR_RED      "\x1b[31m"
#define ANSI_COLOR_GREEN    "\x1b[32m"
#define ANSI_COLOR_YELLOW   "\x1b[33m"
#define ANSI_COLOR_BLUE     "\x1b[34m"
#define ANSI_COLOR_MAGENTA  "\x1b[35m"
#define ANSI_COLOR_CYAN     "\x1b[36m"
#define ANSI_COLOR_WHITE    "\x1b[37m"

// Bright/Light colors (often preferred for visibility)
#define ANSI_COLOR_BRIGHT_BLACK   "\x1b[90m" // Dark gray
#define ANSI_COLOR_BRIGHT_RED     "\x1b[91m"
#define ANSI_COLOR_BRIGHT_GREEN   "\x1b[92m"
#define ANSI_COLOR_BRIGHT_YELLOW  "\x1b[93m"
#define ANSI_COLOR_BRIGHT_BLUE    "\x1b[94m"
#define ANSI_COLOR_BRIGHT_MAGENTA "\x1b[95m"
#define ANSI_COLOR_BRIGHT_CYAN    "\x1b[96m"
#define ANSI_COLOR_BRIGHT_WHITE   "\x1b[97m"

// Basic styling (can be combined with colors, e.g., "\x1b[1;34m" for bold blue)
#define ANSI_STYLE_BOLD     "\x1b[1m"
#define ANSI_STYLE_ITALIC   "\x1b[3m"
#define ANSI_STYLE_UNDERLINE "\x1b[4m"

// --- Enumerations for Scheme and Token Types ---

// Defines the available highlighting schemes
typedef enum {
    HIGHLIGHT_SCHEME_NANO,
    HIGHLIGHT_SCHEME_VIM,
    HIGHLIGHT_SCHEME_DEFAULT // Can be an alias or just another distinct scheme
} HighlightSchemeType;

// Defines different types of tokens that can be highlighted
typedef enum {
    TOKEN_TYPE_DEFAULT,      // Unhighlighted text
    TOKEN_TYPE_COMMENT,      // # ...
    TOKEN_TYPE_STRING,       // "..." or '...'
    TOKEN_TYPE_KEYWORD,      // if, for, while, echo, ls, etc.
    TOKEN_TYPE_VARIABLE,     // $VAR, ${VAR}
    TOKEN_TYPE_NUMBER,       // 123, 0xFF
    TOKEN_TYPE_OPERATOR,     // =, ==, &&, ||, ;, ( ), [ ]
    TOKEN_TYPE_SHEBANG       // #! line
    // Add more as we identify them
} HighlightTokenType;

// --- Structure to define a single highlighting scheme ---
// Each member holds the ANSI escape code string for a specific token type.
typedef struct {
    const char *default_color;
    const char *comment_color;
    const char *string_color;
    const char *keyword_color;
    const char *variable_color;
    const char *number_color;
    const char *operator_color;
    const char *shebang_color;
    // Add more color members as TOKEN_TYPEs are added
} HighlightScheme;

// --- Global Scheme Definitions (declared in .h, defined in .c) ---
extern const HighlightScheme NANO_HIGHLIGHT_SCHEME;
extern const HighlightScheme VIM_HIGHLIGHT_SCHEME;

// --- Function to highlight a given script content string ---
// Now takes a scheme type as an argument.
// Allocates and returns a new string with ANSI escape codes.
// Caller is responsible for freeing the returned string.
char *highlight_shell_script(const char *script_content, int script_len, HighlightSchemeType scheme_type);

#endif // UPKG_HIGHLIGHT_H
