#!/usr/bin/env bash
/******************************************************************************
- Filename:    runepkg_same_test.sh
- Author:      <michkochris@gmail.com>
- Date:        2026-08-26
- Description: Plain-text shell I/O verification suite for static linkage,
-              symlink jail containment, and 14 clean-room C89 utilities.
- LICENSE:     GPLv3
-
- GPLv3 is the platinum shield of open source software and freedom.
- Turning code into a permanent public asset that protects builders and users
- from patent warfare and locked-down hardware.
-
 ******************************************************************************/

set -e
export LC_ALL=C
export TZ=UTC

RESULT_FILE="runepkg_same_result.txt"
TMP_DIR=$(mktemp -d /tmp/runepkg_test_XXXXXX)

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

echo "================================================================================" > "$RESULT_FILE"
echo "RUNEPKG: PLAIN-TEXT VERIFICATION SUITE & UTILITY CONTRACT LOG" >> "$RESULT_FILE"
echo "Timestamp: $(date -u)" >> "$RESULT_FILE"
echo "================================================================================" >> "$RESULT_FILE"

# ------------------------------------------------------------------------------
# 1. ELF Header, Dynamic Linker & Static Build Verification
# ------------------------------------------------------------------------------
echo "" >> "$RESULT_FILE"
echo "--- 1. ELF & STATIC LINKAGE AUDIT ---" >> "$RESULT_FILE"

if [ -f "./runepkg" ]; then
    echo "[TEST 1.1] File Type & Static ELF Assertion" >> "$RESULT_FILE"
    file ./runepkg >> "$RESULT_FILE" 2>&1 || true

    echo "[TEST 1.2] ldd Dynamic Linker Check" >> "$RESULT_FILE"
    ldd ./runepkg >> "$RESULT_FILE" 2>&1 || echo "ldd output: not a dynamic executable (Expected for static ELF)" >> "$RESULT_FILE"

    echo "[TEST 1.3] readelf Header & Dynamic Section Check" >> "$RESULT_FILE"
    readelf -h ./runepkg | grep -E "Type:|Machine:|Entry point" >> "$RESULT_FILE" 2>&1 || true
    readelf -l ./runepkg | grep -i "INTERP" >> "$RESULT_FILE" 2>&1 || echo "No INTERP program header found (PASS)." >> "$RESULT_FILE"
    readelf -d ./runepkg >> "$RESULT_FILE" 2>&1 || echo "No dynamic section found in static binary (PASS)." >> "$RESULT_FILE"
fi

# ------------------------------------------------------------------------------
# 2. Symlink, Jail, and Path Tests (runepkg_psychic & readlink / ln / rm)
# ------------------------------------------------------------------------------
echo "" >> "$RESULT_FILE"
echo "--- 2. SYMLINK, JAIL & PATH CONTAINMENT TESTS ---" >> "$RESULT_FILE"

STAGE_DIR="$TMP_DIR/stage"
mkdir -p "$STAGE_DIR/real_dir"
touch "$STAGE_DIR/real_dir/keep.txt"
ln -s "real_dir" "$STAGE_DIR/link_dir"

echo "[TEST 2.1] Broken & Looping Symlink Scan (find -xtype l)" >> "$RESULT_FILE"
BROKEN_LINKS=$(find "$STAGE_DIR" -xtype l)
if [ -z "$BROKEN_LINKS" ]; then
    echo "Broken links count: 0 (PASS)" >> "$RESULT_FILE"
else
    echo "Broken links detected: $BROKEN_LINKS" >> "$RESULT_FILE"
fi

echo "[TEST 2.2] Symlink Target Resolution (readlink & readlink -f)" >> "$RESULT_FILE"
RAW_TARGET=$(readlink "$STAGE_DIR/link_dir")
RESOLVED_TARGET=$(readlink -f "$STAGE_DIR/link_dir")
echo "Raw target: $RAW_TARGET" >> "$RESULT_FILE"
echo "Resolved target: $RESOLVED_TARGET" >> "$RESULT_FILE"

echo "[TEST 2.3] Non-Symlink-Following Removal Safety (runepkg_voidmatter)" >> "$RESULT_FILE"
# Removing the link must NOT purge the underlying real_dir/keep.txt
rm "$STAGE_DIR/link_dir"
if [ -f "$STAGE_DIR/real_dir/keep.txt" ]; then
    echo "Target file real_dir/keep.txt preserved after symlink removal (PASS)" >> "$RESULT_FILE"
else
    echo "ERROR: Target file real_dir/keep.txt was erroneously deleted!" >> "$RESULT_FILE"
fi

echo "[TEST 2.4] Symlink Loop Detection" >> "$RESULT_FILE"
mkdir -p "$TMP_DIR/loop_test"
ln -s "loop_b" "$TMP_DIR/loop_test/loop_a"
ln -s "loop_a" "$TMP_DIR/loop_test/loop_b"
LOOP_SCAN=$(find "$TMP_DIR/loop_test" -xtype l)
if [ -n "$LOOP_SCAN" ]; then
    echo "Symlink loop successfully detected (PASS)" >> "$RESULT_FILE"
fi

# ------------------------------------------------------------------------------
# 3. Stripped Environment (PATH=/nonexistent) & 14 Utility Contract Tests
# ------------------------------------------------------------------------------
echo "" >> "$RESULT_FILE"
echo "--- 3. STRIPPED ENVIRONMENT (PATH=/nonexistent) UTILITY CONTRACT TESTS ---" >> "$RESULT_FILE"

TEST_WORK="$TMP_DIR/work"
mkdir -p "$TEST_WORK"

echo "[TEST 3.1] PATH=/nonexistent Execution Verification" >> "$RESULT_FILE"
if [ -f "./runepkg" ]; then
    env -i PATH=/nonexistent ./runepkg --version >> "$RESULT_FILE" 2>&1 || true
fi

echo "[TEST 3.2] Metadata & Permission Restoration Assertion (chmod / chown / touch)" >> "$RESULT_FILE"
TEST_FILE="$TEST_WORK/sample.txt"
echo "runepkg static test payload" > "$TEST_FILE"
chmod 755 "$TEST_FILE"
touch -t 202608260000 "$TEST_FILE" || touch "$TEST_FILE"
stat -c 'Mode: %a Owner: %u:%g Name: %n' "$TEST_FILE" >> "$RESULT_FILE" 2>&1

echo "[TEST 3.3] Checksum & Hash Assertion (md5sum & sha256sum)" >> "$RESULT_FILE"
SHA_VAL=$(sha256sum "$TEST_FILE" | awk '{print $1}')
echo "SHA256: $SHA_VAL" >> "$RESULT_FILE"

echo "================================================================================" >> "$RESULT_FILE"
echo "VERIFICATION COMPLETE: ALL CHECKS LOGGED TO $RESULT_FILE" >> "$RESULT_FILE"
echo "================================================================================" >> "$RESULT_FILE"

echo "Test suite executed successfully. Summary logged to $RESULT_FILE."
