//! Minimal FFI library linked as `librunepkg_ffi` (matches Cargo.toml `[lib]` name).

#[no_mangle]
pub extern "C" fn runepkg_rust_ffi_available() -> i32 {
    1
}

#[no_mangle]
pub extern "C" fn runepkg_highlight_stub() -> i32 {
    0
}
