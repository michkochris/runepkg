#!/usr/bin/env bash
#/******************************************************************************
# * Filename:    make_runepkg_deb.sh
# * Author:      <michkochris@gmail.com>
# * Date:        2025-05-12
# * Description: Script to build a runepkg .deb package using runepkg itself
# ******************************************************************************/

set -e # Exit immediately on failure

# Color Definitions
BOLD='\033[1m'
RESET='\033[0m'
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
CYAN='\033[1;36m'
PURPLE='\033[1;35m'

# Package Variables
Package="runepkg"
Version="1.0.4"
Maintainer="michkochris <michkochris@gmail.com>"
Description="Lightning-fast, high-performance .deb package manager for power users and LFS."
Architecture="amd64"
Homepage="https://github.com/michkochris/runepkg"
Depends="binutils, tar, gzip, xz-utils, libcurl4, libssl3, zlib1g"

# Build Environment
STAGING_DIR="/tmp/runepkg_deb_staging"
CONTROL_DIR="$STAGING_DIR/control"
DATA_DIR="$STAGING_DIR/data"


# --- Helper Rituals ---

# Helper function to create the .deb structure
setup_staging() {
    rm -rf "$STAGING_DIR"
    mkdir -p "$CONTROL_DIR" "$DATA_DIR/usr/bin" "$DATA_DIR/etc/runepkg"
}

# Helper function to generate the control file
generate_control() {
    cat << EOF > "$CONTROL_DIR/control"
Package: $Package
Version: $Version
Architecture: $Architecture
Maintainer: $Maintainer
Homepage: $Homepage
Depends: $Depends
Description: $Description
EOF
}


# --- Main Build Process ---

echo -e "${BOLD}${CYAN}--------------------------------------------------------${RESET}"
echo -e "${BOLD}${CYAN}  The Forge of Runes: Building ${Package} v${Version}${RESET}"
echo -e "${BOLD}${CYAN}--------------------------------------------------------${RESET}"


# 1. Compile the latest runepkg binary
echo -e "  ${BLUE}[forge]${RESET} Preparing the workspace..."
make clean > /dev/null

echo -e "  ${BLUE}[forge]${RESET} Compiling binary runes (make all)..."
# Note: Minimal embedded users can change 'make all' to 'make runepkg' below
make all > /dev/null

if [[ ! -f "./runepkg" ]]; then
    echo -e "  ${RED}[error]${RESET} Compilation failed! The forge is cold."
    exit 1
fi


# 2. Setup the staging area
echo -e "  ${PURPLE}[ritual]${RESET} Initializing staging area at ${STAGING_DIR}..."
setup_staging


# 3. Populate the data directory
echo -e "  ${PURPLE}[ritual]${RESET} Gathering binary runes and configuration..."
cp ./runepkg "$DATA_DIR/usr/bin/"
cp ./runepkgconfig "$DATA_DIR/etc/runepkg/"


# 4. Generate metadata
echo -e "  ${PURPLE}[ritual]${RESET} Inscribing the control rune..."
generate_control


# 5. Build the .deb using runepkg!
DEB_FILENAME="${Package}_${Version}_${Architecture}.deb"
echo -e "  ${CYAN}[runes]${RESET} Forging final package: ${DEB_FILENAME}"

./runepkg -b "$STAGING_DIR" "$DEB_FILENAME" > /dev/null


# 6. Success and Cleanup
if [[ -f "$DEB_FILENAME" ]]; then
    echo -e "${BOLD}${GREEN}--------------------------------------------------------${RESET}"
    echo -e "  ${GREEN}⚡ SUCCESS:${RESET} ${DEB_FILENAME} has been unearthed!"
    echo -e "  ${YELLOW}🧹 Cleaning up artifacts...${RESET}"
    rm -rf "$STAGING_DIR"
    echo -e "${BOLD}${GREEN}--------------------------------------------------------${RESET}"
else
    echo -e "  ${RED}[error]${RESET} .deb assembly failed! The ritual is incomplete."
    exit 1
fi

# end of file...
