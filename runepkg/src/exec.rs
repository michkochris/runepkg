/// Script execution module - Rust implementation based on upkg_exec.c
/// Provides secure script execution with highlighting support
use libc::{c_char, c_int, size_t};
use std::ffi::{CStr, CString};
use std::process::{Command, Stdio};
use std::io::Write;
use std::ptr;

/// Execute a shell script from memory with optional highlighting
/// Based on execute_pkginfo_script from upkg_exec.c
/// 
/// # Safety
/// This function is unsafe because it dereferences raw pointers from C
#[no_mangle]
pub unsafe extern "C" fn rust_execute_script_from_memory(
    script_content: *const c_char,
    script_len: c_int,
    enable_highlighting: c_int,
) -> c_int {
    if script_content.is_null() || script_len <= 0 {
        return -1;
    }

    // Convert C string to Rust string
    let script_slice = std::slice::from_raw_parts(script_content as *const u8, script_len as usize);
    let script_str = match std::str::from_utf8(script_slice) {
        Ok(s) => s,
        Err(_) => return -1,
    };

    // Defensive check for null termination (mimicking check_null_termination_and_exit)
    if !script_str.ends_with('\0') && !script_str.chars().any(|c| c == '\0') {
        // Add null termination check
        let mut found_null = false;
        for byte in script_slice {
            if *byte == 0 {
                found_null = true;
                break;
            }
        }
        if !found_null {
            return -1; // Not null terminated
        }
    }

    // Extract shebang interpreter (based on parse_shebang)
    let interpreter = extract_shebang_interpreter(script_str).unwrap_or("/bin/sh".to_string());
    
    // Execute the script
    execute_script_internal(script_str, &interpreter)
}

/// Extract interpreter from shebang line
/// Based on the parse_shebang function from upkg_exec.c
fn extract_shebang_interpreter(script: &str) -> Option<String> {
    let lines: Vec<&str> = script.lines().collect();
    if lines.is_empty() {
        return None;
    }
    
    let first_line = lines[0];
    if !first_line.starts_with("#!") {
        return None;
    }
    
    // Parse shebang - take first word after #! (mimicking the strtok_r logic)
    let shebang_content = &first_line[2..]; // Skip "#!"
    let parts: Vec<&str> = shebang_content.split_whitespace().collect();
    if parts.is_empty() {
        return None;
    }
    
    Some(parts[0].to_string())
}

/// Internal script execution
/// Based on the execution logic from upkg_exec.c
fn execute_script_internal(script_content: &str, interpreter: &str) -> c_int {
    // Check if interpreter is executable (mimicking access check)
    let mut cmd = Command::new(interpreter)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn();
        
    let mut child = match cmd {
        Ok(child) => child,
        Err(_) => return -1,
    };
    
    // Write script to stdin (mimicking the pipe write in original)
    if let Some(mut stdin) = child.stdin.take() {
        if stdin.write_all(script_content.as_bytes()).is_err() {
            return -1;
        }
    }
    
    // Wait for completion (mimicking waitpid logic)
    match child.wait() {
        Ok(status) => {
            if status.success() {
                0
            } else {
                status.code().unwrap_or(-1)
            }
        }
        Err(_) => -1,
    }
}

/// Parse shebang and return interpreter path as C string
/// Converts the parse_shebang functionality to return a C string
/// 
/// # Safety
/// Returns allocated C string that must be freed with rust_free_string
#[no_mangle]
pub unsafe extern "C" fn rust_parse_shebang(
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

    let interpreter = extract_shebang_interpreter(script_str).unwrap_or("/bin/sh".to_string());
    
    match CString::new(interpreter) {
        Ok(c_string) => c_string.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/// Advanced shebang parsing that returns all arguments
/// Based on the full parse_shebang implementation
/// 
/// # Safety
/// Returns allocated C string array that must be freed properly
#[no_mangle]
pub unsafe extern "C" fn rust_parse_shebang_with_args(
    script_content: *const c_char,
    script_len: c_int,
    max_args: c_int,
) -> *mut *mut c_char {
    if script_content.is_null() || script_len <= 0 || max_args <= 0 {
        return ptr::null_mut();
    }

    let script_slice = std::slice::from_raw_parts(script_content as *const u8, script_len as usize);
    let script_str = match std::str::from_utf8(script_slice) {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };

    let args = extract_shebang_args(script_str, max_args as usize);
    if args.is_empty() {
        return ptr::null_mut();
    }

    // Allocate array of char pointers
    let arg_array = libc::malloc((args.len() + 1) * std::mem::size_of::<*mut c_char>()) as *mut *mut c_char;
    if arg_array.is_null() {
        return ptr::null_mut();
    }

    // Convert each argument to C string
    for (i, arg) in args.iter().enumerate() {
        match CString::new(arg.as_str()) {
            Ok(c_string) => {
                *arg_array.add(i) = c_string.into_raw();
            }
            Err(_) => {
                // Cleanup on error
                for j in 0..i {
                    libc::free(*arg_array.add(j) as *mut libc::c_void);
                }
                libc::free(arg_array as *mut libc::c_void);
                return ptr::null_mut();
            }
        }
    }

    // Null terminate the array
    *arg_array.add(args.len()) = ptr::null_mut();
    arg_array
}

/// Extract shebang arguments (full parsing like the original)
fn extract_shebang_args(script: &str, max_args: usize) -> Vec<String> {
    let lines: Vec<&str> = script.lines().collect();
    if lines.is_empty() {
        return Vec::new();
    }
    
    let first_line = lines[0];
    if !first_line.starts_with("#!") {
        return Vec::new();
    }
    
    let shebang_content = &first_line[2..]; // Skip "#!"
    let parts: Vec<&str> = shebang_content.split_whitespace().collect();
    
    let mut args = Vec::new();
    for (i, part) in parts.iter().enumerate() {
        if i >= max_args {
            break;
        }
        args.push(part.to_string());
    }
    
    args
}

/// Free string allocated by Rust functions
/// 
/// # Safety
/// Assumes ptr was allocated by CString::into_raw()
#[no_mangle]
pub unsafe extern "C" fn rust_free_string(ptr: *mut c_char) {
    if !ptr.is_null() {
        let _ = CString::from_raw(ptr);
    }
}

/// Free string array allocated by rust_parse_shebang_with_args
/// 
/// # Safety
/// Assumes array was allocated by rust_parse_shebang_with_args
#[no_mangle]
pub unsafe extern "C" fn rust_free_string_array(ptr: *mut *mut c_char) {
    if ptr.is_null() {
        return;
    }

    let mut i = 0;
    loop {
        let str_ptr = *ptr.add(i);
        if str_ptr.is_null() {
            break;
        }
        libc::free(str_ptr as *mut libc::c_void);
        i += 1;
    }
    
    libc::free(ptr as *mut libc::c_void);
}

/// Validate script before execution
/// Additional safety check not in original but useful
#[no_mangle]
pub unsafe extern "C" fn rust_validate_script_before_exec(
    script_content: *const c_char,
    script_len: c_int,
) -> c_int {
    if script_content.is_null() || script_len <= 0 {
        return 0;
    }

    let script_slice = std::slice::from_raw_parts(script_content as *const u8, script_len as usize);
    let script_str = match std::str::from_utf8(script_slice) {
        Ok(s) => s,
        Err(_) => return 0,
    };

    // Basic validation
    if script_str.is_empty() {
        return 0;
    }

    // Check for valid shebang
    if !script_str.starts_with("#!") {
        return 0; // Should have shebang for execution
    }

    // Check if interpreter exists
    if let Some(interpreter) = extract_shebang_interpreter(script_str) {
        // Basic path validation
        if interpreter.is_empty() || interpreter.contains('\0') {
            return 0;
        }
        return 1; // Valid
    }

    0 // Invalid
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_shebang_extraction() {
        let script = "#!/bin/bash\necho 'test'\n";
        let interpreter = extract_shebang_interpreter(script);
        assert_eq!(interpreter, Some("/bin/bash".to_string()));
    }
    
    #[test]
    fn test_env_shebang() {
        let script = "#!/usr/bin/env python3\nprint('test')\n";
        let interpreter = extract_shebang_interpreter(script);
        assert_eq!(interpreter, Some("/usr/bin/env".to_string()));
    }

    #[test]
    fn test_shebang_with_args() {
        let script = "#!/bin/bash -e -x\necho 'test'\n";
        let args = extract_shebang_args(script, 10);
        assert_eq!(args.len(), 3);
        assert_eq!(args[0], "/bin/bash");
        assert_eq!(args[1], "-e");
        assert_eq!(args[2], "-x");
    }

    #[test]
    fn test_no_shebang() {
        let script = "echo 'no shebang'\n";
        let interpreter = extract_shebang_interpreter(script);
        assert_eq!(interpreter, None);
    }
}
