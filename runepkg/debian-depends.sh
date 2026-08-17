#!/usr/bin/env bash
# debian-depends.sh - Install all dependencies for runepkg (Extended/Full version)
# Built for Debian/Ubuntu-based systems.

BOLD='\033[1m'
RESET='\033[0m'
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
CYAN='\033[1;36m'

set -e

echo -e "${BOLD}${CYAN}--------------------------------------------------------${RESET}"
echo -e "${BOLD}${CYAN}   runepkg: Preparing the Ritual of Dependencies        ${RESET}"
echo -e "${BOLD}${CYAN}--------------------------------------------------------${RESET}"

echo -e "${YELLOW}Updating ancient scrolls (package lists)...${RESET}"
sudo apt-get update

echo -e "${YELLOW}Gathering Core Runes (Runtime & C Build)...${RESET}"
sudo apt-get install -y binutils tar gzip xz-utils gcc make libc6-dev

echo -e "${YELLOW}Invoking Extended Magic (C++ FFI & Networking)...${RESET}"
sudo apt-get install -y g++ libcurl4-openssl-dev libssl-dev zlib1g-dev

echo -e "${YELLOW}Acquiring supporting artifacts (Bison, Gawk, Debhelper, etc.)...${RESET}"
sudo apt-get install -y bison gawk texinfo libtool-bin debhelper

echo -e "${BOLD}${GREEN}--------------------------------------------------------${RESET}"
echo -e "${BOLD}${GREEN}✅ The ritual is complete. All dependencies are unearthed.${RESET}"
echo -e "You can now run: ${CYAN}make all${RESET} to compile runepkg."
echo -e "${BOLD}${GREEN}--------------------------------------------------------${RESET}"
