#!/usr/bin/env bash
# fetchdeb.sh - Fetch ancient Debian Binary Runes using the runepkg engine
set -euo pipefail

TARGET_DIR="debs"
mkdir -p "$TARGET_DIR"

BOLD='\033[1m'
RESET='\033[0m'
GREEN='\033[1;32m'
CYAN='\033[1;36m'
BLUE='\033[1;34m'
YELLOW='\033[1;33m'

if [ $# -eq 0 ]; then
    echo -e "${BOLD}${CYAN}Usage:${RESET} $0 <package_name>"
    exit 1
fi

echo -e "${BOLD}${BLUE}--------------------------------------------------------${RESET}"
echo -e "${BOLD}${BLUE}   runepkg: Gathering Binary Runes (fetchdeb)           ${RESET}"
echo -e "${BOLD}${BLUE}--------------------------------------------------------${RESET}"

# Use the system-installed runepkg
RUNEPKG_BIN=$(command -v runepkg || echo "./runepkg")

echo -e "${CYAN}--> Invoking the internal download engine for: ${BOLD}$1${RESET}"

# Run the command
$RUNEPKG_BIN download-depends "$1"

# Move the resulting debs into our local folder
DOWNLOAD_DIR=$($RUNEPKG_BIN --print-config | grep "download_dir" | cut -d'=' -f2 | xargs)

if [ -d "$DOWNLOAD_DIR" ]; then
    echo -e "${YELLOW}Gathering fragments from $DOWNLOAD_DIR...${RESET}"
    mv "$DOWNLOAD_DIR"/*.deb "$TARGET_DIR/" 2>/dev/null || true
fi

echo -e "${BOLD}${GREEN}--------------------------------------------------------${RESET}"
echo -e "${BOLD}${GREEN}✅ Gathering complete! Binary fragments in: $(pwd)/$TARGET_DIR${RESET}"
echo -e "You can now use: ${CYAN}runepkg -i $TARGET_DIR/*.deb${RESET}"
echo -e "${BOLD}${GREEN}--------------------------------------------------------${RESET}"
