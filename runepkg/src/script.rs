/// Script utilities and helper functions - Rust implementation
/// Based on upkg_script.h and provides additional script processing capabilities
use libc::{c_char, c_int};
use std::ffi::{CStr, CString};
use std::ptr;

/// Script type detection - corresponds to upkg_script types
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ScriptType {
    Shell = 0,
    Python = 1,
    Perl = 2,
    Ruby = 3,
    Unknown = 4,
}

/// Detect script type from content
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

    let script_slice = std::slice::from_raw_parts(script_content as *const u8, script_len as usize);
    let script_str = match std::str::from_utf8(script_slice) {
        Ok(s) => s,
        Err(_) => return ScriptType::Unknown,
    };

    detect_script_type_internal(script_str)
}

/// Internal script type detection
/// Enhanced version of basic script type detection
fn detect_script_type_internal(script: &str) -> ScriptType {
    let lines: Vec<&str> = script.lines().collect();
    if lines.is_empty() {
        return ScriptType::Unknown;
    }
    
    let first_line = lines[0];
    if first_line.starts_with("#!") {
        let shebang = &first_line[2..];
        
        if shebang.contains("python") {
            return ScriptType::Python;
        } else if shebang.contains("perl") {
            return ScriptType::Perl;
        } else if shebang.contains("ruby") {
            return ScriptType::Ruby;
        } else if shebang.contains("sh") || shebang.contains("bash") || shebang.contains("zsh") {
            return ScriptType::Shell;
        }
    }
    
    // Fallback: analyze content for common patterns
    let content_lower = script.to_lowercase();
    
    // Python detection
    if content_lower.contains("import ") || 
       content_lower.contains("def ") || 
       content_lower.contains("print(") ||
       content_lower.contains("from ") ||
       content_lower.contains("class ") {
        return ScriptType::Python;
    }
    
    // Perl detection
    if content_lower.contains("use strict") || 
       content_lower.contains("my $") || 
       content_lower.contains("print \"") ||
       content_lower.contains("#!/usr/bin/perl") {
        return ScriptType::Perl;
    }
    
    // Ruby detection
    if content_lower.contains("def ") || 
       content_lower.contains("puts ") || 
       content_lower.contains("require ") ||
       content_lower.contains("class ") ||
       content_lower.contains("end") {
        return ScriptType::Ruby;
    }
    
    // Shell script patterns
    if content_lower.contains("if [") ||
       content_lower.contains("echo ") ||
       content_lower.contains("for ") ||
       content_lower.contains("while ") ||
       content_lower.contains("function ") {
        return ScriptType::Shell;
    }
    
    // Default to shell for most scripts
    ScriptType::Shell
}

/// Validate script syntax (comprehensive validation)
/// 
/// # Safety
/// This function is unsafe because it dereferences raw pointers from C
#[no_mangle]
pub unsafe extern "C" fn rust_validate_script_syntax(
    script_content: *const c_char,
    script_len: c_int,
) -> c_int {
    if script_content.is_null() || script_len <= 0 {
        return 0; // Invalid
    }

    let script_slice = std::slice::from_raw_parts(script_content as *const u8, script_len as usize);
    let script_str = match std::str::from_utf8(script_slice) {
        Ok(s) => s,
        Err(_) => return 0,
    };

    validate_script_syntax_internal(script_str) as c_int
}

/// Internal syntax validation
/// Enhanced validation beyond the basic version
fn validate_script_syntax_internal(script: &str) -> bool {
    // Basic validation checks
    
    // Check for balanced quotes
    if !validate_quotes(script) {
        return false;
    }
    
    // Check for balanced braces/brackets/parentheses
    if !validate_brackets(script) {
        return false;
    }
    
    // Check for valid shebang (if present)
    if !validate_shebang(script) {
        return false;
    }
    
    // Type-specific validation
    let script_type = detect_script_type_internal(script);
    match script_type {
        ScriptType::Shell => validate_shell_syntax(script),
        ScriptType::Python => validate_python_syntax(script),
        ScriptType::Perl => validate_perl_syntax(script),
        ScriptType::Ruby => validate_ruby_syntax(script),
        ScriptType::Unknown => true, // Allow unknown types
    }
}

/// Validate quote balancing
fn validate_quotes(script: &str) -> bool {
    let mut in_single_quote = false;
    let mut in_double_quote = false;
    let mut escape_next = false;
    
    for ch in script.chars() {
        if escape_next {
            escape_next = false;
            continue;
        }
        
        match ch {
            '\\' => escape_next = true,
            '\'' if !in_double_quote => in_single_quote = !in_single_quote,
            '"' if !in_single_quote => in_double_quote = !in_double_quote,
            _ => {}
        }
    }
    
    // If we end with unclosed quotes, syntax is invalid
    !in_single_quote && !in_double_quote
}

/// Validate bracket balancing
fn validate_brackets(script: &str) -> bool {
    let mut brace_count = 0;
    let mut bracket_count = 0;
    let mut paren_count = 0;
    let mut in_quote = false;
    let mut quote_char = '\0';
    
    for ch in script.chars() {
        // Skip counting inside quotes
        if !in_quote && (ch == '"' || ch == '\'') {
            in_quote = true;
            quote_char = ch;
            continue;
        }
        
        if in_quote {
            if ch == quote_char {
                in_quote = false;
            }
            continue;
        }
        
        match ch {
            '{' => brace_count += 1,
            '}' => brace_count -= 1,
            '[' => bracket_count += 1,
            ']' => bracket_count -= 1,
            '(' => paren_count += 1,
            ')' => paren_count -= 1,
            _ => {}
        }
        
        // Early exit on negative counts (closing before opening)
        if brace_count < 0 || bracket_count < 0 || paren_count < 0 {
            return false;
        }
    }
    
    // All counts should be zero at the end
    brace_count == 0 && bracket_count == 0 && paren_count == 0
}

/// Validate shebang line
fn validate_shebang(script: &str) -> bool {
    let lines: Vec<&str> = script.lines().collect();
    if lines.is_empty() {
        return true; // Empty script is technically valid
    }
    
    let first_line = lines[0];
    if !first_line.starts_with("#!") {
        return true; // No shebang is okay
    }
    
    // Validate shebang format
    if first_line.len() < 3 {
        return false; // "#!" with nothing after
    }
    
    let interpreter_part = &first_line[2..];
    if interpreter_part.trim().is_empty() {
        return false; // Empty interpreter
    }
    
    // Check for valid interpreter path
    let parts: Vec<&str> = interpreter_part.split_whitespace().collect();
    if parts.is_empty() {
        return false;
    }
    
    let interpreter = parts[0];
    // Basic path validation
    if interpreter.contains('\0') || interpreter.is_empty() {
        return false;
    }
    
    true
}

/// Shell-specific syntax validation
fn validate_shell_syntax(script: &str) -> bool {
    // Check for common shell syntax errors
    let lines: Vec<&str> = script.lines().collect();
    
    for line in lines {
        let trimmed = line.trim();
        
        // Skip comments and empty lines
        if trimmed.is_empty() || trimmed.starts_with('#') {
            continue;
        }
        
        // Check for unmatched if/fi, do/done, etc.
        // This is a basic check - a full parser would be more thorough
        if trimmed.starts_with("if ") && !trimmed.contains("then") && !trimmed.ends_with(';') {
            // If statement without then (might be on next line, so this is just a warning check)
        }
    }
    
    true // Basic validation passed
}

/// Python-specific syntax validation
fn validate_python_syntax(_script: &str) -> bool {
    // Basic Python validation
    // For now, just return true - full Python parsing would require a parser
    true
}

/// Perl-specific syntax validation
fn validate_perl_syntax(_script: &str) -> bool {
    // Basic Perl validation
    true
}

/// Ruby-specific syntax validation
fn validate_ruby_syntax(_script: &str) -> bool {
    // Basic Ruby validation
    true
}

/// Extract script metadata (like embedded comments, author, etc.)
/// 
/// # Safety
/// Returns allocated C string that must be freed with rust_free_string
#[no_mangle]
pub unsafe extern "C" fn rust_extract_script_metadata(
    script_content: *const c_char,
    script_len: c_int,
) -> *mut c_char {
    if script_content.is_null() || script_len <= 0 {
        return ptr::null_mut();
    }

    let script_slice = std::slice::from_raw_parts(script_content as *const u8, script_len as usize);
    let script_str = match std::str::from_utf8(script_slice) {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };

    let metadata = extract_metadata_internal(script_str);
    
    match CString::new(metadata) {
        Ok(c_string) => c_string.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/// Internal metadata extraction
/// Enhanced to detect more metadata patterns
fn extract_metadata_internal(script: &str) -> String {
    let mut metadata = Vec::new();
    let mut in_header = true;
    
    for (line_num, line) in script.lines().enumerate() {
        // Stop looking for metadata after first 50 lines or first non-comment
        if line_num > 50 {
            break;
        }
        
        let trimmed = line.trim();
        
        // Skip empty lines
        if trimmed.is_empty() {
            continue;
        }
        
        // Look for comment lines
        if trimmed.starts_with('#') {
            let comment = trimmed[1..].trim();
            
            // Look for various metadata patterns
            if let Some(meta) = extract_metadata_from_comment(comment) {
                metadata.push(meta);
            }
        } else if in_header && trimmed.starts_with("#!/") {
            // Shebang line
            metadata.push(format!("Interpreter: {}", trimmed));
        } else {
            // First non-comment, non-shebang line
            in_header = false;
        }
    }
    
    if metadata.is_empty() {
        "No metadata found".to_string()
    } else {
        metadata.join("\n")
    }
}

/// Extract metadata from a single comment line
fn extract_metadata_from_comment(comment: &str) -> Option<String> {
    let comment_lower = comment.to_lowercase();
    
    // Common metadata patterns
    for (pattern, field_name) in &[
        ("author:", "Author"),
        ("version:", "Version"),
        ("description:", "Description"),
        ("date:", "Date"),
        ("license:", "License"),
        ("copyright:", "Copyright"),
        ("filename:", "Filename"),
        ("usage:", "Usage"),
        ("purpose:", "Purpose"),
        ("note:", "Note"),
        ("todo:", "Todo"),
        ("fixme:", "Fixme"),
        ("bug:", "Bug"),
        ("created:", "Created"),
        ("modified:", "Modified"),
        ("updated:", "Updated"),
    ] {
        if comment_lower.contains(pattern) {
            // Extract the value after the pattern
            if let Some(pos) = comment_lower.find(pattern) {
                let value_start = pos + pattern.len();
                if value_start < comment.len() {
                    let value = comment[value_start..].trim();
                    if !value.is_empty() {
                        return Some(format!("{}: {}", field_name, value));
                    }
                }
            }
        }
    }
    
    None
}

/// Get script statistics
/// 
/// # Safety
/// Returns allocated C string that must be freed with rust_free_string
#[no_mangle]
pub unsafe extern "C" fn rust_get_script_stats(
    script_content: *const c_char,
    script_len: c_int,
) -> *mut c_char {
    if script_content.is_null() || script_len <= 0 {
        return ptr::null_mut();
    }

    let script_slice = std::slice::from_raw_parts(script_content as *const u8, script_len as usize);
    let script_str = match std::str::from_utf8(script_slice) {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };

    let stats = get_script_stats_internal(script_str);
    
    match CString::new(stats) {
        Ok(c_string) => c_string.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/// Internal script statistics calculation
fn get_script_stats_internal(script: &str) -> String {
    let lines: Vec<&str> = script.lines().collect();
    let total_lines = lines.len();
    let mut comment_lines = 0;
    let mut blank_lines = 0;
    let mut code_lines = 0;
    
    for line in &lines {
        let trimmed = line.trim();
        if trimmed.is_empty() {
            blank_lines += 1;
        } else if trimmed.starts_with('#') {
            comment_lines += 1;
        } else {
            code_lines += 1;
        }
    }
    
    let total_chars = script.len();
    let script_type = detect_script_type_internal(script);
    
    format!(
        "Script Statistics:\n\
         Type: {:?}\n\
         Total lines: {}\n\
         Code lines: {}\n\
         Comment lines: {}\n\
         Blank lines: {}\n\
         Total characters: {}",
        script_type, total_lines, code_lines, comment_lines, blank_lines, total_chars
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_script_type_detection() {
        let shell_script = "#!/bin/bash\necho 'test'\n";
        assert_eq!(detect_script_type_internal(shell_script), ScriptType::Shell);
        
        let python_script = "#!/usr/bin/env python3\nprint('test')\n";
        assert_eq!(detect_script_type_internal(python_script), ScriptType::Python);
        
        let ruby_script = "#!/usr/bin/ruby\nputs 'hello'\nend\n";
        assert_eq!(detect_script_type_internal(ruby_script), ScriptType::Ruby);
    }
    
    #[test]
    fn test_syntax_validation() {
        let valid_script = "#!/bin/bash\necho 'hello world'\n";
        assert!(validate_script_syntax_internal(valid_script));
        
        let invalid_script = "#!/bin/bash\necho 'unclosed quote\n";
        assert!(!validate_script_syntax_internal(invalid_script));
        
        let unbalanced_brackets = "#!/bin/bash\nif [ test {\necho 'test'\n";
        assert!(!validate_script_syntax_internal(unbalanced_brackets));
    }
    
    #[test]
    fn test_metadata_extraction() {
        let script = "#!/bin/bash\n# Author: Test User\n# Version: 1.0\n# Description: Test script\necho 'test'\n";
        let metadata = extract_metadata_internal(script);
        assert!(metadata.contains("Author"));
        assert!(metadata.contains("Version"));
        assert!(metadata.contains("Description"));
    }
    
    #[test]
    fn test_quote_validation() {
        assert!(validate_quotes("echo 'hello world'"));
        assert!(validate_quotes("echo \"hello world\""));
        assert!(!validate_quotes("echo 'hello world"));
        assert!(!validate_quotes("echo \"hello world'"));
    }
    
    #[test]
    fn test_bracket_validation() {
        assert!(validate_brackets("if [ test ]; then echo 'ok'; fi"));
        assert!(validate_brackets("array=(one two three)"));
        assert!(!validate_brackets("if [ test; then echo 'ok'; fi"));
        assert!(!validate_brackets("array=(one two three"));
    }
}
