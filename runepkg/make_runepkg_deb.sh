#!/usr/bin/env bash
#/******************************************************************************
# * Filename:    make_runepkg_deb.sh
# * Author:      <michkochris@gmail.com>
# * Date:        2025-05-12
# * Description: Script to build a runepkg .deb package using runepkg itself
# ******************************************************************************/

set -e # Exit immediately on failure

# Load configuration and helpers
if [[ ! -f "./make_runepkg_config" ]]; then
    echo "Error: make_runepkg_config not found!"
    exit 1
fi
source ./make_runepkg_config

echo "--- Starting runepkg .deb Build Process ---"

# 1. Compile the latest runepkg binary
echo "Compiling runepkg..."
make clean
# Note: Minimal embedded users can change 'make all' to 'make runepkg' below
make all

if [[ ! -f "./runepkg" ]]; then
    echo "Error: Compilation failed!"
    exit 1
fi

# 2. Setup the staging area
setup_staging

# 3. Populate the data directory
echo "Populating data directory..."
cp ./runepkg "$DATA_DIR/usr/bin/"
cp ./runepkgconfig "$DATA_DIR/etc/runepkg/"

# 4. Generate metadata
generate_control

# 5. Build the .deb using runepkg!
# We use the local ./runepkg to build the package of itself.
DEB_FILENAME="${PACKAGE_NAME}_${VERSION}_${ARCHITECTURE}.deb"
echo "Building final .deb: $DEB_FILENAME"

./runepkg -b "$STAGING_DIR" "$DEB_FILENAME"

if [[ -f "$DEB_FILENAME" ]]; then
    echo "-----------------------------------------------------------------------"
    echo "SUCCESS: $DEB_FILENAME built successfully!"
    echo "Staging area cleaned."
    rm -rf "$STAGING_DIR"
    echo "-----------------------------------------------------------------------"
else
    echo "Error: .deb assembly failed!"
    exit 1
fi
