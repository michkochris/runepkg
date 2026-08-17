#!/usr/bin/env bash
# wget_deb.sh - Quickly summon specific known test artifacts

BOLD='\033[1m'
RESET='\033[0m'
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
CYAN='\033[1;36m'
BLUE='\033[1;34m'

echo -e "${BOLD}${BLUE}--------------------------------------------------------${RESET}"
echo -e "${BOLD}${BLUE}   runepkg: Summoning Test Artifacts (wget_deb)        ${RESET}"
echo -e "${BOLD}${BLUE}--------------------------------------------------------${RESET}"

if [ $# -gt 0 ]; then
    echo -e "${YELLOW}Notice: wget_deb is for hardcoded core artifacts only.${RESET}"
    echo -e "To fetch ${BOLD}$1${RESET} and its dependencies, use: ${CYAN}./fetchdeb.sh $1${RESET}"
    echo ""
fi

download_if_not_exist() {
    local url="$1"
    local filename=$(basename "$url")

    if [ ! -f "$filename" ]; then
        echo -e "${CYAN}--> Summoning artifact:${RESET} ${BOLD}$filename${RESET}"
        wget -q "$url" || echo -e "${RED}Failed to download $filename${RESET}"
    else
        echo -e "${GREEN}--> Artifact already present:${RESET} $filename"
    fi
}

# --- Standalone Samples (amd64) ---
download_if_not_exist "http://ftp.us.debian.org/debian/pool/main/b/busybox/busybox-static_1.37.0-9_amd64.deb"
download_if_not_exist "http://ftp.us.debian.org/debian/pool/main/f/file/file_5.46-5_amd64.deb"

echo -e "${BOLD}${GREEN}--------------------------------------------------------${RESET}"
echo -e "${BOLD}${GREEN}✅ Summoning complete. Samples ready for runepkg -i${RESET}"
echo -e "${BOLD}${GREEN}--------------------------------------------------------${RESET}"
