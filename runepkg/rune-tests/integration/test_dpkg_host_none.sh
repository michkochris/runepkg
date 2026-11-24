#!/usr/bin/env bash
# ==============================================================================
# Filename:    test_dpkg_host_none.sh
# Description: Integration test verifying dpkg_host=none skips host dpkg synchronization
# ==============================================================================

set -e

RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
RESET='\033[0m'

echo -e "${YELLOW}=== Running dpkg_host=none Isolation Test ===${RESET}"

TARGET_BIN="../../runepkg"
if [ ! -f "$TARGET_BIN" ]; then
    echo -e "${RED}Error: runepkg binary not found at $TARGET_BIN${RESET}"
    exit 1
fi

TEST_DIR="/tmp/runepkg_test_dpkg_none_$$"
mkdir -p "$TEST_DIR/runepkg_dir/runepkg_db"
mkdir -p "$TEST_DIR/runepkg_dir/control_dir"
mkdir -p "$TEST_DIR/runepkg_dir/install_dir"

CONF_FILE="$TEST_DIR/test_runepkg.conf"
cat << EOF > "$CONF_FILE"
runepkg_dir=$TEST_DIR/runepkg_dir
control_dir=$TEST_DIR/runepkg_dir/control_dir
runepkg_db=$TEST_DIR/runepkg_dir/runepkg_db
install_dir=$TEST_DIR/runepkg_dir/install_dir
dpkg_host=none
EOF

export HOME="$TEST_DIR"
export RUNEPKG_CONFIG_PATH="$CONF_FILE"

echo -e "${YELLOW}Executing runepkg -v sync with dpkg_host=none...${RESET}"
SYNC_OUTPUT=$("$TARGET_BIN" -v sync 2>&1 || true)
echo "$SYNC_OUTPUT"

if echo "$SYNC_OUTPUT" | grep -q "dpkg_host is set to 'none'"; then
    echo -e "${GREEN}Verified: dpkg_host=none correctly skipped host dpkg synchronization.${RESET}"
else
    echo -e "${RED}Error: dpkg_host=none was not respected or logged during sync.${RESET}"
    rm -rf "$TEST_DIR"
    exit 1
fi

rm -rf "$TEST_DIR"
echo -e "${GREEN}=== dpkg_host=none Isolation Test Passed ===${RESET}"
