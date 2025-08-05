#!/bin/bash
# Memory Management Validation Script for runepkg
# This script analyzes the source code for memory management issues

echo "=== runepkg Memory Management Analysis ==="
echo "Checking unified PkgInfo memory model..."

echo ""
echo "1. Checking PkgInfo structure fields..."
echo "   - Fields in hash.h:"
grep -n "char \*" runepkg_hash.h | head -20

echo ""
echo "2. Checking runepkg_pack_free_package_info()..."
echo "   - Fields being freed:"
grep -A 20 "runepkg_pack_free_package_info" runepkg_pack.c | grep "runepkg_util_free_and_null"

echo ""
echo "3. Checking runepkg_hash_free_package_info()..."
echo "   - Fields being freed:"
grep -A 25 "runepkg_hash_free_package_info" runepkg_hash.c | grep "runepkg_util_free_and_null"

echo ""
echo "4. Checking hash table add operations..."
echo "   - strdup calls for new nodes:"
grep -A 30 "new_node->data\." runepkg_hash.c | grep "strdup"

echo ""
echo "5. Checking hash table update operations..."
echo "   - strdup calls for existing nodes:"
grep -A 25 "existing->" runepkg_hash.c | grep "strdup"

echo ""
echo "6. Potential memory issues to verify:"
echo "   - ✓ All PkgInfo fields must be freed in both pack and hash modules"
echo "   - ✓ All PkgInfo fields must be allocated in hash table operations"
echo "   - ✓ File list array must be properly freed"
echo "   - ✓ control_dir_path and data_dir_path must be handled consistently"

echo ""
echo "=== Analysis Complete ==="
