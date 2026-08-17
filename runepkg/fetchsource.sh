#!/usr/bin/env bash
# fetchsource.sh - Fetch ancient Debian Source Runes using the runepkg engine
set -euo pipefail

TARGET_DIR="sources"
mkdir -p "$TARGET_DIR"

BOLD='\033[1m'
RESET='\033[0m'
GREEN='\033[1;32m'
CYAN='\033[1;36m'
PURPLE='\033[1;35m'
YELLOW='\033[1;33m'

if [ $# -eq 0 ]; then
    echo -e "${BOLD}${CYAN}Usage:${RESET} $0 <package_name>"
    exit 1
fi

echo -e "${BOLD}${PURPLE}--------------------------------------------------------${RESET}"
echo -e "${BOLD}${PURPLE}   runepkg: Unearthing Source Runes (fetchsource)       ${RESET}"
echo -e "${BOLD}${PURPLE}--------------------------------------------------------${RESET}"

# Use the system-installed runepkg
RUNEPKG_BIN=$(command -v runepkg || echo "./runepkg")

echo -e "${CYAN}--> Invoking the internal source unearthing engine for: ${BOLD}$1${RESET}"

# Use the built-in 'source' command
$RUNEPKG_BIN source "$1"

# Move the resulting source files into our local folder
BUILD_DIR=$($RUNEPKG_BIN --print-config | grep "build_dir" | cut -d'=' -f2 | xargs)

if [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Gathering source runes from $BUILD_DIR...${RESET}"
    mv "$BUILD_DIR"/"$1"* "$TARGET_DIR/" 2>/dev/null || true
fi

echo -e "${BOLD}${GREEN}--------------------------------------------------------${RESET}"
echo -e "${BOLD}${GREEN}✅ Extraction complete! Sources available in: $(pwd)/$TARGET_DIR${RESET}"
echo -e "You can now use: ${CYAN}runepkg buildpkg-split $TARGET_DIR/${RESET}"
echo -e "${BOLD}${GREEN}--------------------------------------------------------${RESET}"
