#!/usr/bin/env bash
# debian-depends.sh - Modular dependency installer for runepkg
# Supports Core, Musl, and Extended build requirements.

BOLD='\033[1m'
RESET='\033[0m'
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
CYAN='\033[1;36m'
PURPLE='\033[1;35m'
BLUE='\033[1;34m'

set -e

# Default flags
INSTALL_CORE=0
INSTALL_MUSL=0
INSTALL_EXTENDED=0

show_help() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --core      Install basic C build tools (gcc, make, etc.)"
    echo "  --musl      Install musl-libc toolchain (musl-tools, musl-dev)"
    echo "  --extended  Install C++ FFI & Networking (g++, libcurl, zlib)"
    echo "  --all       Install everything (default if no options provided)"
    echo "  --help      Show this help message"
}

# Parse arguments
if [[ $# -eq 0 ]]; then
    INSTALL_CORE=1
    INSTALL_MUSL=1
    INSTALL_EXTENDED=1
else
    for arg in "$@"; do
        case $arg in
            --core)     INSTALL_CORE=1 ;;
            --musl)     INSTALL_MUSL=1 ;;
            --extended) INSTALL_EXTENDED=1 ;;
            --all)      INSTALL_CORE=1; INSTALL_MUSL=1; INSTALL_EXTENDED=1 ;;
            --help)     show_help; exit 0 ;;
            *)          echo -e "${RED}Unknown option: $arg${RESET}"; show_help; exit 1 ;;
        esac
    done
fi

echo -e "${BOLD}${CYAN}--------------------------------------------------------${RESET}"
echo -e "${BOLD}${CYAN}   runepkg: Preparing the Ritual of Dependencies        ${RESET}"
echo -e "${BOLD}${CYAN}--------------------------------------------------------${RESET}"

if [[ $INSTALL_CORE -eq 1 ]]; then
    echo -e "${BOLD}${BLUE}[shell]${RESET} Ensuring /bin/sh points to /bin/bash..."
    if [ "$(readlink /bin/sh)" != "bash" ] && [ "$(readlink /bin/sh)" != "/bin/bash" ]; then
        echo -e "${YELLOW}Switching /bin/sh from $(readlink /bin/sh) to /bin/bash...${RESET}"
        sudo rm -f /bin/sh
        sudo ln -s /bin/bash /bin/sh
    fi

    echo -e "${BOLD}${BLUE}[forge]${RESET} Gathering Core Runes (C Build Tools)..."
    sudo apt-get update
    sudo apt-get install -y binutils tar gzip xz-utils gcc make libc6-dev bison flex gawk texinfo libtool-bin libncurses-dev quilt libgmp-dev libmpfr-dev libmpc-dev time rsync
    # Add help for manual permission fixes
    sudo mkdir -p /mnt/runepkg
    sudo chown -R $(id -u):$(id -g) /mnt/runepkg || true
fi

if [[ $INSTALL_MUSL -eq 1 ]]; then
    echo -e "${BOLD}${YELLOW}[musl]${RESET} Invoking the Lightweight Spirit (musl-libc)..."
    sudo apt-get install -y musl musl-dev musl-tools
fi

if [[ $INSTALL_EXTENDED -eq 1 ]]; then
    echo -e "${BOLD}${PURPLE}[ritual]${RESET} Invoking Extended Magic (C++ & Networking)..."
    sudo apt-get install -y g++ libcurl4-openssl-dev libssl-dev zlib1g-dev
fi

echo -e "${BOLD}${GREEN}--------------------------------------------------------${RESET}"
echo -e "${BOLD}${GREEN}✅ The ritual is complete. The forge is ready.${RESET}"
echo ""
echo -e "Next steps:"
[[ $INSTALL_CORE -eq 1 ]] && echo -e "  - Core Build:     ${CYAN}make runepkg${RESET}"
[[ $INSTALL_MUSL -eq 1 ]] && echo -e "  - Musl Build:     ${CYAN}make MUSL=1 runepkg${RESET}"
[[ $INSTALL_EXTENDED -eq 1 ]] && echo -e "  - Extended Build: ${CYAN}make all${RESET}"
echo -e "${BOLD}${GREEN}--------------------------------------------------------${RESET}"
