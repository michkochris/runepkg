#!/bin/bash

# runepkg Simple Package Downloader - Debian 12 (Bookworm)
# Downloads working .deb files for testing

set -e

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

print_status() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Simple download function
download_package() {
    local url="$1"
    local filename=$(basename "$url")
    
    if [ ! -f "$filename" ]; then
        print_status "Downloading $filename..."
        if wget "$url"; then
            print_success "Downloaded $filename"
        else
            print_error "Failed to download $filename from $url"
            return 1
        fi
    else
        print_warning "$filename already exists"
    fi
}

# Create debs directory
mkdir -p debs
cd debs

print_status "🚀 Downloading test packages from Debian 12..."

# Use stable, known packages from Debian 12 (bookworm)
BASE_URL="https://deb.debian.org/debian/pool/main"

# BusyBox - very stable, always available
download_package "$BASE_URL/b/busybox/busybox-static_1.36.1-9+deb12u1_amd64.deb"

# Coreutils - essential
download_package "$BASE_URL/c/coreutils/coreutils_9.1-1_amd64.deb"

# Wget itself - meta!
download_package "$BASE_URL/w/wget/wget_1.21.3-1+b2_amd64.deb"

# Tiny packages for quick testing
download_package "$BASE_URL/h/hello/hello_2.12-1_amd64.deb"

# File utility 
download_package "$BASE_URL/f/file/file_5.44-3_amd64.deb"

echo ""
print_success "✅ Download complete! Files in debs/ directory:"
ls -la *.deb 2>/dev/null || echo "No .deb files found"

echo ""
print_status "🔧 Test with: ./runepkg install debs/hello_2.12-1_amd64.deb"
