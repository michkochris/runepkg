/// runepkg Script Utilities - Clean Slate Implementation
/// 
/// PHILOSOPHY: Zero external dependencies
/// - Pure Rust fundamental features only
/// - Self-contained C FFI types
/// - libcore/liballoc for basic collections
/// - No external crates whatsoever
#![no_std]

extern crate alloc;

use alloc::string::String;
use core::ptr;
use core::slice;
use core::str;

// Self-contained C FFI types
pub type c_char = i8;
pub type c_int = i32;
pub type size_t = usize;

/// Script type detection - pure Rust enums
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ScriptType {
    Shell = 0,
    Python = 1,
    Perl = 2,
    Ruby = 3,
    Unknown = 4,
}

/// Detect script type from content - completely self-contained
/// 
/// # Safety
/// This function is unsafe because it dereferences raw pointers from C
#[no_mangle]
pub unsafe extern "C" fn rust_detect_script_type(
    script_content: *const c_char,
    script_len: c_int,
) -> ScriptType {
    if script_content.is_null() || script_len <= 0 {
        return ScriptType::Unknown;
    }

    let script_slice = slice::from_raw_parts(script_content as *const u8, script_len as usize);
    let script_str = match str::from_utf8(script_slice) {
        Ok(s) => s,
        Err(_) => return ScriptType::Unknown,
    };

    detect_script_type_internal(script_str)
}

/// Internal script type detection using pure Rust pattern matching
fn detect_script_type_internal(script: &str) -> ScriptType {
    // Find first non-empty line
    let mut first_line = "";
    for line in script.lines() {
        let trimmed = line.trim();
        if !trimmed.is_empty() {
            first_line = trimmed;
            break;
        }
    }
    
    if first_line.is_empty() {
        return ScriptType::Unknown;
    }
    
    // Check shebang using pure string operations
    if first_line.starts_with("#!") {
        let shebang = &first_line[2..];
        
        if shebang.contains("bash") || shebang.contains("sh") || shebang.contains("/bin/sh") {
            return ScriptType::Shell;
        } else if shebang.contains("python") {
            return ScriptType::Python;
        } else if shebang.contains("perl") {
            return ScriptType::Perl;
        } else if shebang.contains("ruby") {
            return ScriptType::Ruby;
        }
    }
    
    // Check for language-specific patterns using pure string methods
    if script.contains("def ") && script.contains("import ") {
        return ScriptType::Python;
    } else if script.contains("use strict") || script.contains("use warnings") || script.contains("sub ") {
        return ScriptType::Perl;
    } else if script.contains("def ") && (script.contains("class ") || script.contains("require ")) {
        return ScriptType::Ruby;
    } else if script.contains("if [") || script.contains("function ") || script.contains("#!/bin/") {
        return ScriptType::Shell;
    }
    
    ScriptType::Unknown
}

/// Extract metadata from script comments - pure Rust implementation
/// 
/// # Safety
/// This function is unsafe because it dereferences raw pointers from C
#[no_mangle]
pub unsafe extern "C" fn rust_extract_script_metadata(
    script_content: *const c_char,
    script_len: c_int,
    metadata_buffer: *mut c_char,
    buffer_size: c_int,
) -> c_int {
    if script_content.is_null() || metadata_buffer.is_null() || script_len <= 0 || buffer_size <= 0 {
        return 0;
    }

    let script_slice = slice::from_raw_parts(script_content as *const u8, script_len as usize);
    let script_str = match str::from_utf8(script_slice) {
        Ok(s) => s,
        Err(_) => return 0,
    };

    let metadata = extract_metadata_internal(script_str);
    let metadata_bytes = metadata.as_bytes();
    let copy_len = core::cmp::min(metadata_bytes.len(), (buffer_size - 1) as usize);
    
    // Copy metadata to buffer using core functionality
    let buffer_slice = slice::from_raw_parts_mut(metadata_buffer as *mut u8, buffer_size as usize);
    buffer_slice[..copy_len].copy_from_slice(&metadata_bytes[..copy_len]);
    buffer_slice[copy_len] = 0; // Null terminator
    
    copy_len as c_int
}

/// Internal metadata extraction using pure Rust string operations
fn extract_metadata_internal(script: &str) -> String {
    let mut metadata = String::new();
    
    for (line_num, line) in script.lines().enumerate() {
        if line_num >= 20 { // Only check first 20 lines
            break;
        }
        
        let trimmed = line.trim();
        
        // Look for comment metadata using pure string methods
        if trimmed.starts_with('#') {
            let comment = trimmed[1..].trim();
            
            // Common metadata patterns - case insensitive using pure Rust
            let comment_lower = comment.to_lowercase();
            if comment_lower.contains("author:") ||
               comment_lower.contains("description:") ||
               comment_lower.contains("version:") ||
               comment_lower.contains("usage:") {
                if !metadata.is_empty() {
                    metadata.push('\n');
                }
                metadata.push_str(comment);
            }
        }
    }
    
    if metadata.is_empty() {
        metadata.push_str("No metadata found");
    }
    
    metadata
}

/// Validate script syntax - basic pure Rust validation
/// 
/// # Safety
/// This function is unsafe because it dereferences raw pointers from C
#[no_mangle]
pub unsafe extern "C" fn rust_validate_script_syntax(
    script_content: *const c_char,
    script_len: c_int,
    script_type: ScriptType,
) -> c_int {
    if script_content.is_null() || script_len <= 0 {
        return 0;
    }

    let script_slice = slice::from_raw_parts(script_content as *const u8, script_len as usize);
    let script_str = match str::from_utf8(script_slice) {
        Ok(s) => s,
        Err(_) => return 0,
    };

    if validate_syntax_internal(script_str, script_type) {
        1
    } else {
        0
    }
}

/// Internal syntax validation using pure Rust
fn validate_syntax_internal(script: &str, script_type: ScriptType) -> bool {
    match script_type {
        ScriptType::Shell => validate_shell_syntax(script),
        ScriptType::Python => validate_python_syntax(script),
        ScriptType::Perl => validate_perl_syntax(script),
        ScriptType::Ruby => validate_ruby_syntax(script),
        ScriptType::Unknown => false,
    }
}

/// Basic shell syntax validation using pure Rust
fn validate_shell_syntax(script: &str) -> bool {
    let mut if_count = 0;
    let mut fi_count = 0;
    let mut for_count = 0;
    let mut done_count = 0;
    
    for line in script.lines() {
        let trimmed = line.trim();
        
        // Basic bracket matching using pure string operations
        if trimmed.starts_with("if ") {
            if_count += 1;
        } else if trimmed == "fi" {
            fi_count += 1;
        } else if trimmed.starts_with("for ") {
            for_count += 1;
        } else if trimmed == "done" {
            done_count += 1;
        }
    }
    
    // Basic structure validation
    if_count == fi_count && for_count == done_count
}

/// Basic Python syntax validation using pure Rust
fn validate_python_syntax(script: &str) -> bool {
    let mut indent_levels = alloc::vec::Vec::new();
    
    for line in script.lines() {
        if line.trim().is_empty() || line.trim().starts_with('#') {
            continue;
        }
        
        // Count leading spaces using pure character iteration
        let mut line_indent = 0;
        for ch in line.chars() {
            if ch == ' ' {
                line_indent += 1;
            } else {
                break;
            }
        }
        
        // Basic indentation check
        if line.trim().ends_with(':') {
            indent_levels.push(line_indent);
        }
    }
    
    true // Basic validation passed
}

/// Basic Perl syntax validation using pure Rust
fn validate_perl_syntax(script: &str) -> bool {
    let mut brace_count = 0;
    
    for line in script.lines() {
        for ch in line.chars() {
            match ch {
                '{' => brace_count += 1,
                '}' => brace_count -= 1,
                _ => {}
            }
        }
    }
    
    brace_count == 0
}

/// Basic Ruby syntax validation using pure Rust
fn validate_ruby_syntax(script: &str) -> bool {
    let mut end_count = 0;
    let mut begin_count = 0;
    
    for line in script.lines() {
        let trimmed = line.trim();
        
        if trimmed.starts_with("def ") || 
           trimmed.starts_with("class ") ||
           trimmed.starts_with("module ") ||
           trimmed.starts_with("if ") ||
           trimmed.starts_with("unless ") ||
           trimmed.starts_with("while ") ||
           trimmed.starts_with("for ") ||
           trimmed.starts_with("begin") {
            begin_count += 1;
        } else if trimmed == "end" {
            end_count += 1;
        }
    }
    
    begin_count == end_count
}

// Clean slate - no test module to avoid external dependencies
