use libc::{c_char, c_int, size_t};
use std::ffi::{CStr, CString};
use std::ptr;
use syntect::easy::HighlightLines;
use syntect::highlighting::{ThemeSet, Style};
use syntect::parsing::SyntaxSet;
use syntect::util::{as_24_bit_terminal_escaped, LinesWithEndings};
use once_cell::sync::Lazy;

// Module declarations
pub mod exec;
pub mod script;

// Global syntax and theme sets - initialized once
static SYNTAX_SET: Lazy<SyntaxSet> = Lazy::new(|| SyntaxSet::load_defaults_newlines());
static THEME_SET: Lazy<ThemeSet> = Lazy::new(|| ThemeSet::load_defaults());

/// Highlight scheme types matching the C enum from upkg_highlight.h
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub enum HighlightScheme {
    Nano = 0,
    Vim = 1,
    Default = 2,
}

/// Convert C highlight scheme to theme name
fn scheme_to_theme_name(scheme: HighlightScheme) -> &'static str {
    match scheme {
        HighlightScheme::Nano => "base16-ocean.dark",
        HighlightScheme::Vim => "base16-mocha.dark", 
        HighlightScheme::Default => "base16-ocean.dark",
    }
}

/// Main highlighting function exposed to C - based on highlight_shell_script from upkg_highlight.c
/// 
/// # Safety
/// This function is unsafe because it:
/// - Dereferences raw pointers from C
/// - Assumes script_content is a valid C string of length script_len
/// - Returns a malloc'd C string that must be freed by the caller
#[no_mangle]
pub unsafe extern "C" fn rust_highlight_shell_script(
    script_content: *const c_char,
    script_len: c_int,
    scheme: HighlightScheme,
) -> *mut c_char {
    // Null pointer check
    if script_content.is_null() || script_len <= 0 {
        return ptr::null_mut();
    }

    // Convert C string to Rust string slice
    let script_slice = std::slice::from_raw_parts(script_content as *const u8, script_len as usize);
    let script_str = match std::str::from_utf8(script_slice) {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(), // Invalid UTF-8
    };

    // Perform highlighting
    let highlighted = highlight_script_internal(script_str, scheme);
    
    // Convert result back to C string
    match CString::new(highlighted) {
        Ok(c_string) => {
            let ptr = c_string.into_raw();
            ptr
        }
        Err(_) => ptr::null_mut(),
    }
}

/// Internal highlighting implementation using syntect
/// This replaces the manual ANSI coloring from the original upkg_highlight.c
fn highlight_script_internal(script_content: &str, scheme: HighlightScheme) -> String {
    let theme_name = scheme_to_theme_name(scheme);
    
    // Get syntax reference for shell scripts
    let syntax = SYNTAX_SET
        .find_syntax_by_extension("sh")
        .or_else(|| SYNTAX_SET.find_syntax_by_name("Bourne Again Shell (bash)"))
        .or_else(|| SYNTAX_SET.find_syntax_by_name("Shell Script (Bash)"))
        .unwrap_or_else(|| SYNTAX_SET.find_syntax_plain_text());

    // Get theme
    let theme = THEME_SET
        .themes
        .get(theme_name)
        .unwrap_or_else(|| THEME_SET.themes.values().next().unwrap());

    // Create highlighter
    let mut highlighter = HighlightLines::new(syntax, theme);
    let mut result = String::new();

    // Process each line
    for line in LinesWithEndings::from(script_content) {
        let ranges: Vec<(Style, &str)> = highlighter
            .highlight_line(line, &SYNTAX_SET)
            .unwrap_or_else(|_| vec![(Style::default(), line)]);
        
        let escaped = as_24_bit_terminal_escaped(&ranges[..], false);
        result.push_str(&escaped);
    }

    result
}

/// Free memory allocated by rust_highlight_shell_script
/// 
/// # Safety
/// This function is unsafe because it:
/// - Dereferences a raw pointer that should have been allocated by CString::into_raw()
/// - Assumes the pointer is valid and was allocated by this library
#[no_mangle]
pub unsafe extern "C" fn rust_free_highlighted_string(ptr: *mut c_char) {
    if !ptr.is_null() {
        let _ = CString::from_raw(ptr);
        // CString::from_raw will automatically drop and free the memory
    }
}

/// Get available themes count - useful for C code to know available options
#[no_mangle]
pub extern "C" fn rust_get_theme_count() -> c_int {
    THEME_SET.themes.len() as c_int
}

/// Get theme name by index - useful for C code to enumerate themes
/// 
/// # Safety
/// This function is unsafe because it returns a raw pointer to a C string
/// The returned string should not be freed by the caller (it's static)
#[no_mangle]
pub unsafe extern "C" fn rust_get_theme_name(index: c_int) -> *const c_char {
    if index < 0 || index >= THEME_SET.themes.len() as c_int {
        return ptr::null();
    }
    
    let theme_names: Vec<&String> = THEME_SET.themes.keys().collect();
    let theme_name = theme_names[index as usize];
    
    // Convert to C string - this creates a static string that doesn't need freeing
    match CString::new(theme_name.as_str()) {
        Ok(c_string) => {
            // Note: This leaks memory intentionally for simplicity
            // In a production system, you'd want a better approach
            c_string.into_raw() as *const c_char
        }
        Err(_) => ptr::null(),
    }
}

/// Simple test function to verify FFI is working
#[no_mangle]
pub extern "C" fn rust_test_ffi() -> c_int {
    42
}

/// Initialize the highlighting system - call this once at startup
#[no_mangle]
pub extern "C" fn rust_init_highlighting() -> c_int {
    // Force initialization of lazy statics
    Lazy::force(&SYNTAX_SET);
    Lazy::force(&THEME_SET);
    1 // Success
}

/// Get version information
#[no_mangle]
pub unsafe extern "C" fn rust_get_version() -> *const c_char {
    let version = "runepkg-highlight 0.1.0 (syntect-powered)";
    match CString::new(version) {
        Ok(c_string) => c_string.into_raw() as *const c_char,
        Err(_) => ptr::null(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_highlight_simple_script() {
        let script = "#!/bin/bash\necho 'Hello World'\n# This is a comment\n";
        let result = highlight_script_internal(script, HighlightScheme::Nano);
        assert!(!result.is_empty());
        println!("Highlighted result:\n{}", result);
    }

    #[test]
    fn test_highlight_complex_script() {
        let script = r#"#!/bin/bash
# Complex shell script test
if [ "$1" = "test" ]; then
    echo "Running tests..."
    for file in *.txt; do
        echo "Processing: $file"
    done
fi
"#;
        let result = highlight_script_internal(script, HighlightScheme::Vim);
        assert!(!result.is_empty());
        assert!(result.contains("\x1b[")); // Should contain ANSI codes
    }

    #[test]
    fn test_ffi_function() {
        assert_eq!(rust_test_ffi(), 42);
    }

    #[test]
    fn test_theme_count() {
        let count = rust_get_theme_count();
        assert!(count > 0);
    }
}
