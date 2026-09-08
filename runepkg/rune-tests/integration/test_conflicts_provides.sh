#!/usr/bin/env bash
# ==============================================================================
# Filename:    test_conflicts_provides.sh
# Description: Integration test for conflicts-replaces.bin and Provides dummy sync
# ==============================================================================

set -e

RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
RESET='\033[0m'

echo -e "${YELLOW}=== Running Conflicts & Provides Index Integration Test ===${RESET}"

TARGET_BIN="../../runepkg"
if [ ! -f "$TARGET_BIN" ]; then
    echo -e "${RED}Error: runepkg binary not found at $TARGET_BIN${RESET}"
    exit 1
fi

TEST_DIR="/tmp/runepkg_test_conflicts_$$"
mkdir -p "$TEST_DIR/runepkg_dir/runepkg_db"
mkdir -p "$TEST_DIR/runepkg_dir/control_dir"
mkdir -p "$TEST_DIR/runepkg_dir/install_dir"

CONF_FILE="$TEST_DIR/test_runepkg.conf"
cat << EOF > "$CONF_FILE"
runepkg_dir=$TEST_DIR/runepkg_dir
control_dir=$TEST_DIR/runepkg_dir/control_dir
runepkg_db=$TEST_DIR/runepkg_dir/runepkg_db
install_dir=$TEST_DIR/runepkg_dir/install_dir
dpkg_host=yes
EOF

export HOME="$TEST_DIR"
export RUNEPKG_CONFIG_PATH="$CONF_FILE"

echo -e "${YELLOW}Executing runepkg sync with custom config...${RESET}"
"$TARGET_BIN" sync || true

# Verify conflicts-replaces.bin and conflicts-replaces.txt creation
CONF_BIN="$TEST_DIR/runepkg_dir/runepkg_db/conflicts-replaces.bin"
CONF_TXT="$TEST_DIR/runepkg_dir/runepkg_db/conflicts-replaces.txt"

if [ -f "$CONF_BIN" ] && [ -f "$CONF_TXT" ]; then
    echo -e "${GREEN}Verified conflicts-replaces.bin and conflicts-replaces.txt successfully created in DB.${RESET}"
    echo -e "${YELLOW}Index Summary Contents:${RESET}"
    cat "$CONF_TXT"
else
    echo -e "${RED}Error: conflicts-replaces index files missing at $CONF_BIN${RESET}"
    rm -rf "$TEST_DIR"
    exit 1
fi

rm -rf "$TEST_DIR"
echo -e "${GREEN}=== Conflicts & Provides Integration Test Passed ===${RESET}"
