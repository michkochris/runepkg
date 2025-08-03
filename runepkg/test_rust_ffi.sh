#!/bin/bash
# Test script for runepkg Rust FFI highlighting functionality

echo "=== runepkg Rust FFI Testing Script ==="
echo "Testing runepkg Rust FFI highlighting..."

# Test 1: Check if Rust toolchain is available
echo ""
echo "=== Test 1: Rust Toolchain Check ==="
if command -v rustc >/dev/null 2>&1; then
    echo "✓ Rust compiler found: $(rustc --version)"
else
    echo "✗ Rust compiler not found"
    echo "Install with: curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh"
fi

if command -v cargo >/dev/null 2>&1; then
    echo "✓ Cargo found: $(cargo --version)"
else
    echo "✗ Cargo not found"
fi

# Test 2: Check Cargo.toml
echo ""
echo "=== Test 2: Rust Project Check ==="
if [ -f "Cargo.toml" ]; then
    echo "✓ Cargo.toml found"
    echo "Project configuration:"
    grep -E "(name|version|edition)" Cargo.toml | head -3
else
    echo "✗ Cargo.toml not found"
fi

# Test 3: Build Rust library
echo ""
echo "=== Test 3: Rust Library Build ==="
if make rust-lib 2>/dev/null; then
    echo "✓ Rust library built successfully"
    
    # Check for library files
    if [ -f "target/release/librunepkg_highlight.a" ]; then
        echo "✓ Static library found: target/release/librunepkg_highlight.a"
        ls -lh target/release/librunepkg_highlight.a
    elif [ -f "target/release/librunepkg_highlight.so" ]; then
        echo "✓ Dynamic library found: target/release/librunepkg_highlight.so"
        ls -lh target/release/librunepkg_highlight.so
    else
        echo "✗ No library output found"
    fi
else
    echo "✗ Rust library build failed"
fi

# Test 4: Check if C wrapper files exist
echo ""
echo "=== Test 4: C Wrapper Files Check ==="
for file in runepkg_highlight_rust.c runepkg_highlight_rust.h runepkg_highlight_ffi.h; do
    if [ -f "$file" ]; then
        echo "✓ $file found"
    else
        echo "✗ $file missing"
    fi
done

# Test 5: Build runepkg with Rust FFI
echo ""
echo "=== Test 5: runepkg with Rust FFI Build ==="
if make with-rust 2>/dev/null; then
    echo "✓ runepkg with Rust FFI built successfully"
    if [ -f "runepkg" ]; then
        echo "✓ runepkg executable created"
        ls -lh runepkg
    else
        echo "✗ runepkg executable not found"
    fi
else
    echo "✗ runepkg with Rust FFI build failed"
    echo "Trying fallback build without Rust..."
    if make clean && make WITHOUT_RUST=1; then
        echo "✓ Fallback build successful"
    else
        echo "✗ Even fallback build failed"
    fi
fi

# Test 6: Runtime FFI test
echo ""
echo "=== Test 6: Runtime FFI Test ==="
if make test-rust 2>/dev/null; then
    echo "✓ Runtime FFI test completed"
else
    echo "✗ Runtime FFI test failed"
fi

# Test 7: Create test highlighting samples
echo ""
echo "=== Test 7: Sample Script Highlighting ==="
cat > sample_scripts/test_bash.sh << 'EOF'
#!/bin/bash
# Sample bash script for highlighting test
# Author: Test User
# Version: 1.0

echo "Hello, World!"

if [ "$1" = "test" ]; then
    echo "Running in test mode"
    for file in *.txt; do
        echo "Processing: $file"
    done
fi

# Function definition
function greet() {
    local name="$1"
    echo "Hello, $name!"
}

greet "runepkg"
EOF

cat > sample_scripts/test_python.py << 'EOF'
#!/usr/bin/env python3
# Sample Python script for highlighting test
# Author: Test User

def main():
    """Main function"""
    print("Hello from Python!")
    
    # List comprehension
    numbers = [x**2 for x in range(10)]
    print(f"Squares: {numbers}")

if __name__ == "__main__":
    main()
EOF

mkdir -p sample_scripts
echo "✓ Created sample scripts for testing:"
echo "  - sample_scripts/test_bash.sh"
echo "  - sample_scripts/test_python.py"

# Test 8: Show build information
echo ""
echo "=== Test 8: Build Information ==="
if make rust-info 2>/dev/null; then
    echo "✓ Build information displayed"
else
    echo "⚠ Could not display build information (make rust-info failed)"
fi

# Summary
echo ""
echo "=== Test Summary ==="
echo "runepkg Rust FFI test completed."

if [ -f "runepkg" ] && [ -f "target/release/librunepkg_highlight.a" ]; then
    echo "🎉 SUCCESS: runepkg with Rust FFI is ready!"
    echo ""
    echo "Next steps:"
    echo "  - Test with: ./runepkg --help"
    echo "  - Run highlighting tests: make test-rust"
    echo "  - Install: make install"
    echo ""
    echo "To test highlighting manually:"
    echo "  echo '#!/bin/bash\necho \"test\"' | ./runepkg --highlight"
    echo "  (Note: CLI integration may still need to be implemented)"
elif [ -f "runepkg" ]; then
    echo "✓ runepkg built successfully (without Rust FFI)"
    echo "⚠ Rust FFI not available - install Rust for highlighting features"
else
    echo "✗ Build failed - check error messages above"
fi

echo ""
echo "For more information: make info"
