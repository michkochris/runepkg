/// runepkg Rust FFI - Clean Slate Implementation
/// 
/// PHILOSOPHY: Absolute minimal external dependencies approach
/// - No external crates whatsoever 
/// - Standard Rust library only for FFI compatibility
/// - Pure Rust fundamental language features
/// - Self-contained C FFI types and functions

use std::ffi::CString;
use std::os::raw::{c_char, c_int};
use std::ptr;

// Self-contained C FFI types using standard library
pub type CChar = c_char;
pub type CInt = c_int;

/// Highlight scheme types - completely self-contained
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub enum HighlightScheme {
    Nano = 0,
    Vim = 1,
    Default = 2,
}

/// Pure Rust ANSI color codes - no external dependencies
const COLOR_RESET: &str = "\x1b[0m";
const COLOR_COMMENT: &str = "\x1b[32m";      // Green
const COLOR_STRING: &str = "\x1b[33m";       // Yellow  
const COLOR_KEYWORD: &str = "\x1b[34m";      // Blue
const COLOR_VARIABLE: &str = "\x1b[36m";     // Cyan
const COLOR_OPERATOR: &str = "\x1b[35m";     // Magenta

/// Convert C highlight scheme to color intensity
fn scheme_to_intensity(scheme: HighlightScheme) -> bool {
    match scheme {
        HighlightScheme::Nano => false,    // Dim colors
        HighlightScheme::Vim => true,      // Bright colors  
        HighlightScheme::Default => false, // Dim colors
    }
}

/// Main highlighting function - completely self-contained
/// 
/// # Safety
/// This function is unsafe because it dereferences raw pointers from C
/// Uses only core Rust features and libcore/liballoc
#[no_mangle]
pub unsafe extern "C" fn rust_highlight_shell_script(
    script_content: *const CChar,
    script_len: CInt,
    scheme: HighlightScheme,
) -> *mut CChar {
    // Null pointer check
    if script_content.is_null() || script_len <= 0 {
        return ptr::null_mut();
    }

    // Convert C string to Rust string slice
    let script_slice = std::slice::from_raw_parts(script_content as *const u8, script_len as usize);
    let script_str = match std::str::from_utf8(script_slice) {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };

    // Perform highlighting using pure Rust
    let highlighted = highlight_script_internal(script_str, scheme);
    
    // Convert to C string and return raw pointer
    match CString::new(highlighted) {
        Ok(c_string) => c_string.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/// Internal highlighting using pure Rust pattern matching
fn highlight_script_internal(script_content: &str, scheme: HighlightScheme) -> String {
    let intense = scheme_to_intensity(scheme);
    let mut result = String::new();
    
    for line in script_content.lines() {
        let highlighted_line = highlight_line_simple(line, intense);
        result.push_str(&highlighted_line);
        result.push('\n');
    }
    
    result
}

/// Simple line highlighting using fundamental Rust features only
fn highlight_line_simple(line: &str, _intense: bool) -> String {
    let mut result = String::new();
    let chars: Vec<char> = line.chars().collect();
    let mut i = 0;
    
    while i < chars.len() {
        let ch = chars[i];
        
        // Comments
        if ch == '#' {
            result.push_str(COLOR_COMMENT);
            result.push_str(&line[i..]);
            result.push_str(COLOR_RESET);
            break;
        }
        // String literals
        else if ch == '"' || ch == '\'' {
            let quote = ch;
            result.push_str(COLOR_STRING);
            result.push(ch);
            i += 1;
            
            // Find closing quote
            while i < chars.len() && chars[i] != quote {
                if chars[i] == '\\' && i + 1 < chars.len() {
                    result.push(chars[i]);
                    i += 1;
                    if i < chars.len() {
                        result.push(chars[i]);
                    }
                } else {
                    result.push(chars[i]);
                }
                i += 1;
            }
            
            if i < chars.len() {
                result.push(chars[i]); // Closing quote
            }
            result.push_str(COLOR_RESET);
        }
        // Variables
        else if ch == '$' {
            result.push_str(COLOR_VARIABLE);
            result.push(ch);
            i += 1;
            
            // Collect variable name
            while i < chars.len() && (chars[i].is_alphanumeric() || chars[i] == '_' || chars[i] == '{' || chars[i] == '}') {
                result.push(chars[i]);
                i += 1;
            }
            result.push_str(COLOR_RESET);
            continue;
        }
        // Keywords
        else if ch.is_alphabetic() {
            let start = i;
            while i < chars.len() && (chars[i].is_alphanumeric() || chars[i] == '_') {
                i += 1;
            }
            
            let word: String = chars[start..i].iter().collect();
            if is_shell_keyword(&word) {
                result.push_str(COLOR_KEYWORD);
                result.push_str(&word);
                result.push_str(COLOR_RESET);
            } else {
                result.push_str(&word);
            }
            continue;
        }
        // Operators
        else if "=<>!&|".contains(ch) {
            result.push_str(COLOR_OPERATOR);
            result.push(ch);
            result.push_str(COLOR_RESET);
        }
        // Normal characters
        else {
            result.push(ch);
        }
        
        i += 1;
    }
    
    result
}

/// Shell keyword detection using pure Rust
fn is_shell_keyword(word: &str) -> bool {
    matches!(word, 
        "if" | "then" | "else" | "elif" | "fi" |
        "for" | "while" | "until" | "do" | "done" |
        "case" | "esac" | "function" | "return" |
        "local" | "export" | "declare" | "readonly" |
        "echo" | "printf" | "read" | "test" | "true" | "false"
    )
}

/// Free memory - self-contained memory management
/// 
/// # Safety
/// This function is unsafe because it dereferences a raw pointer
#[no_mangle]
pub unsafe extern "C" fn rust_free_highlighted_string(ptr: *mut CChar) {
    if !ptr.is_null() {
        // Reconstruct the CString and let it drop automatically
        let _ = CString::from_raw(ptr);
    }
}

/// Get available themes count
#[no_mangle]
pub extern "C" fn rust_get_theme_count() -> CInt {
    3 // Nano, Vim, Default
}

/// Get theme name by index
/// 
/// # Safety
/// Returns pointer to static string - no allocation needed
#[no_mangle]
pub extern "C" fn rust_get_theme_name(index: CInt) -> *const CChar {
    let theme_name = match index {
        0 => "nano\0",
        1 => "vim\0", 
        2 => "default\0",
        _ => return ptr::null(),
    };
    
    theme_name.as_ptr() as *const CChar
}

/// Simple test function
#[no_mangle]
pub extern "C" fn rust_test_ffi() -> CInt {
    42
}

/// Initialize highlighting system - no-op for clean implementation
#[no_mangle]
pub extern "C" fn rust_init_highlighting() -> CInt {
    1 // Always success
}

/// Get version information
#[no_mangle]
pub extern "C" fn rust_get_version() -> *const CChar {
    b"runepkg-highlight 1.0.0 (clean-slate)\0".as_ptr() as *const CChar
}

/// Execute script from memory - minimal placeholder
#[no_mangle]
pub unsafe extern "C" fn rust_execute_script_from_memory(
    _script_content: *const CChar,
    _script_len: CInt,
) -> CInt {
    // Clean slate: minimal implementation
    0 // Not implemented in clean slate version
}

/// Parse shebang line - minimal placeholder
#[no_mangle]
pub unsafe extern "C" fn rust_parse_shebang(
    _script_content: *const CChar,
    _script_len: CInt,
) -> *mut CChar {
    // Clean slate: minimal implementation
    ptr::null_mut()
}

/// Detect script type - minimal placeholder
#[no_mangle]
pub unsafe extern "C" fn rust_detect_script_type(
    _script_content: *const CChar,
    _script_len: CInt,
) -> *mut CChar {
    // Clean slate: return "shell" as default
    match CString::new("shell") {
        Ok(c_string) => c_string.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/// Validate script syntax - minimal placeholder
#[no_mangle]
pub unsafe extern "C" fn rust_validate_script_syntax(
    _script_content: *const CChar,
    _script_len: CInt,
) -> CInt {
    // Clean slate: always return valid
    1
}

/// Extract script metadata - minimal placeholder
#[no_mangle]
pub unsafe extern "C" fn rust_extract_script_metadata(
    _script_content: *const CChar,
    _script_len: CInt,
) -> *mut CChar {
    // Clean slate: minimal implementation
    ptr::null_mut()
}

/// Get script statistics - minimal placeholder
#[no_mangle]
pub unsafe extern "C" fn rust_get_script_stats(
    _script_content: *const CChar,
    _script_len: CInt,
) -> *mut CChar {
    // Clean slate: return basic stats
    match CString::new("lines=0,chars=0,words=0") {
        Ok(c_string) => c_string.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

// Clean slate - no tests module to avoid any external dependencies
// Testing should be done through the C FFI interface
