#!/bin/bash

# A helper script to download .deb files for testing runepkg.
# This script uses a function to quickly check and download files
# by simply providing the URL.

# Function to check and download a file if it doesn't exist.
download_if_not_exist() {
    local url="$1"
    # Use basename to extract the filename from the URL
    local filename=$(basename "$url")

    # Check if the file already exists
    if [ ! -f "$filename" ]; then
        echo "Downloading $filename..."
        wget -q "$url" # -q for quiet output
    else
        echo "$filename already exists. Skipping download."
    fi
}

echo "Starting download process..."

# --- BusyBox (amd64) ---
download_if_not_exist "http://ftp.us.debian.org/debian/pool/main/b/busybox/busybox-static_1.37.0-9_amd64.deb"

# --- File Utility (amd64) ---
download_if_not_exist "http://ftp.us.debian.org/debian/pool/main/f/file/file_5.46-5_amd64.deb"

echo "Download process complete."
