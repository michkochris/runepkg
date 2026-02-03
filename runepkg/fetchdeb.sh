#!/bin/bash

# Define the target directory
TARGET_DIR="debs"

# Create the directory if it doesn't exist
mkdir -p "$TARGET_DIR"

# Check if an argument was provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 <package_name> or <list_file.txt>"
    exit 1
fi

# Function to download package and its dependencies
fetch_pkg() {
    local pkg=$1
    echo "--> Processing: $pkg"
    # Use apt-get to download the package and all dependencies into the target dir
    # --reinstall ensures you get the file even if it's already installed on your system
    cd "$TARGET_DIR" && apt-get download $(apt-cache depends --recurse --no-recommends --no-suggests --no-conflicts --no-breaks --no-replaces --no-enhances "$pkg" | grep "^\w" | sort -u)
    cd ..
}

# Check if the input is a file or a single package name
if [ -f "$1" ]; then
    echo "Reading package list from $1..."
    while IFS= read -r line; do
        [[ -z "$line" || "$line" =~ ^# ]] && continue
        fetch_pkg "$line"
    done < "$1"
else
    fetch_pkg "$1"
fi

echo "---"
echo "Done! Check the '$(pwd)/$TARGET_DIR' directory."
