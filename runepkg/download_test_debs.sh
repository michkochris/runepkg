#!/bin/bash

# A helper script to download .deb files for testing runepkg.
# This script uses a function to quickly check and download files
# by simply providing the URL.

# Create debs directory if it doesn't exist
if [ ! -d "debs" ]; then
    mkdir debs
fi

cd debs

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

# --- BusyBox (arm64) ---
download_if_not_exist "http://ftp.us.debian.org/debian/pool/main/b/busybox/busybox-static_1.37.0-6+b2_arm64.deb"

# --- File Utility (arm64) ---  
download_if_not_exist "http://ftp.us.debian.org/debian/pool/main/f/file/file_5.39-3+deb11u1_arm64.deb"

# --- Nano (arm64) ---
download_if_not_exist "http://ftp.us.debian.org/debian/pool/main/n/nano/nano_7.2-1_arm64.deb"

# --- Vim (arm64) ---
download_if_not_exist "http://ftp.us.debian.org/debian/pool/main/v/vim/vim_9.0.1378-2_arm64.deb"

# --- Curl (arm64) ---
download_if_not_exist "http://ftp.us.debian.org/debian/pool/main/c/curl/curl_7.88.1-10+deb12u7_arm64.deb"

echo "Download process complete."
echo "Files downloaded to debs/ directory"
