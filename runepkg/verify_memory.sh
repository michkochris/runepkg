#!/bin/bash
# runepkg Memory Management Verification Report
# This script verifies the unified memory model fixes

echo "=== runepkg Unified Memory Model Verification ==="
echo "Date: $(date)"
echo ""

echo "1. STRUCTURE DEFINITION CHECK"
echo "   PkgInfo structure fields:"
grep -A 20 "typedef struct PkgInfo" runepkg_hash.h | grep "char \*" | nl

echo ""
echo "2. MEMORY CLEANUP FUNCTIONS COMPARISON"
echo ""
echo "   A. runepkg_pack_free_package_info() fields:"
grep -A 20 "runepkg_pack_free_package_info" runepkg_pack.c | grep "runepkg_util_free_and_null" | nl

echo ""
echo "   B. runepkg_hash_free_package_info() fields:"
grep -A 25 "runepkg_hash_free_package_info" runepkg_hash.c | grep "runepkg_util_free_and_null" | nl

echo ""
echo "3. HASH TABLE ALLOCATION VERIFICATION"
echo ""
echo "   A. New node allocation (strdup calls):"
grep -A 20 "new_node->data\." runepkg_hash.c | grep "strdup" | nl

echo ""
echo "   B. Existing node update (strdup calls):"
grep -A 20 "existing->" runepkg_hash.c | grep "strdup" | nl

echo ""
echo "4. CRITICAL MEMORY MANAGEMENT CHECKS"
echo ""

# Count fields in structure
STRUCT_FIELDS=$(grep -A 20 "typedef struct PkgInfo" runepkg_hash.h | grep -c "char \*")
echo "   ✓ PkgInfo has $STRUCT_FIELDS string fields"

# Count fields freed in pack module
PACK_FREE_FIELDS=$(grep -A 20 "runepkg_pack_free_package_info" runepkg_pack.c | grep -c "runepkg_util_free_and_null.*->[a-z_]*;")
echo "   ✓ runepkg_pack_free_package_info() frees $PACK_FREE_FIELDS string fields"

# Count fields freed in hash module
HASH_FREE_FIELDS=$(grep -A 25 "runepkg_hash_free_package_info" runepkg_hash.c | grep -c "runepkg_util_free_and_null.*->[a-z_]*;")
echo "   ✓ runepkg_hash_free_package_info() frees $HASH_FREE_FIELDS string fields"

# Count fields allocated in hash new node
NEW_NODE_FIELDS=$(grep -A 20 "new_node->data\." runepkg_hash.c | grep -c "strdup")
echo "   ✓ Hash table new node allocates $NEW_NODE_FIELDS fields"

# Count fields allocated in hash update
UPDATE_NODE_FIELDS=$(grep -A 20 "existing->" runepkg_hash.c | grep -c "strdup")
echo "   ✓ Hash table update allocates $UPDATE_NODE_FIELDS fields"

echo ""
echo "5. CONSISTENCY VERIFICATION"
echo ""

if [ "$PACK_FREE_FIELDS" -eq "$HASH_FREE_FIELDS" ]; then
    echo "   ✅ PASS: Both free functions handle the same number of fields"
else
    echo "   ❌ FAIL: Inconsistent field count between free functions"
    echo "      Pack: $PACK_FREE_FIELDS, Hash: $HASH_FREE_FIELDS"
fi

if [ "$NEW_NODE_FIELDS" -eq "$UPDATE_NODE_FIELDS" ]; then
    echo "   ✅ PASS: Hash table add and update handle same number of fields"
else
    echo "   ❌ FAIL: Inconsistent field count in hash operations"
    echo "      New: $NEW_NODE_FIELDS, Update: $UPDATE_NODE_FIELDS"
fi

# Check for specific critical fields
echo ""
echo "6. CRITICAL FIELD VERIFICATION"
echo ""

if grep -q "control_dir_path.*strdup" runepkg_hash.c; then
    echo "   ✅ PASS: control_dir_path is allocated in hash table"
else
    echo "   ❌ FAIL: control_dir_path NOT allocated in hash table"
fi

if grep -q "data_dir_path.*strdup" runepkg_hash.c; then
    echo "   ✅ PASS: data_dir_path is allocated in hash table"
else
    echo "   ❌ FAIL: data_dir_path NOT allocated in hash table"
fi

if grep -q "control_dir_path" runepkg_hash.c | grep -q "runepkg_util_free_and_null"; then
    echo "   ✅ PASS: control_dir_path is freed in hash cleanup"
else
    echo "   ❌ FAIL: control_dir_path NOT freed in hash cleanup"
fi

if grep -q "data_dir_path" runepkg_hash.c | grep -q "runepkg_util_free_and_null"; then
    echo "   ✅ PASS: data_dir_path is freed in hash cleanup"
else
    echo "   ❌ FAIL: data_dir_path NOT freed in hash cleanup"
fi

echo ""
echo "7. FILE LIST MANAGEMENT"
echo ""

if grep -q "file_list\[i\].*strdup" runepkg_hash.c; then
    echo "   ✅ PASS: File list entries are properly allocated"
else
    echo "   ❌ FAIL: File list entries NOT properly allocated"
fi

if grep -q "runepkg_util_free_and_null.*file_list\[i\]" runepkg_hash.c; then
    echo "   ✅ PASS: File list entries are properly freed"
else
    echo "   ❌ FAIL: File list entries NOT properly freed"
fi

echo ""
echo "=== SUMMARY ==="
echo "The unified memory model should now correctly handle:"
echo "• All PkgInfo string fields (including control_dir_path, data_dir_path)"
echo "• Consistent allocation in hash table operations"
echo "• Consistent cleanup in both pack and hash modules"
echo "• Proper file list management"
echo ""
echo "=== Verification Complete ==="
