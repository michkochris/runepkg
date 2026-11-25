#!/usr/bin/env bash
# forge-musl-all.sh - The "Holy Grail" Ritual
# Automates a full static musl build with C++ features on glibc systems.

BOLD='\033[1m'
RESET='\033[0m'
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
CYAN='\033[1;36m'
PURPLE='\033[1;35m'

set -e
set -o pipefail

START_DIR="$(pwd)"
FORGE_DIR="$START_DIR/forge_musl"
TOOLCHAIN_DIR="$FORGE_DIR/toolchain"
DEPS_DIR="$FORGE_DIR/deps"
BUILD_DIR="$FORGE_DIR/build"
LOG_FILE="$FORGE_DIR/forge.log"

# Versions
ZLIB_VER="1.3.1"
CURL_VER="8.9.1"
TOOLCHAIN_URL="https://musl.cc/x86_64-linux-musl-cross.tgz"

mkdir -p "$TOOLCHAIN_DIR" "$DEPS_DIR" "$BUILD_DIR"
: > "$LOG_FILE"

echo -e "${BOLD}${CYAN}--------------------------------------------------------${RESET}"
echo -e "${BOLD}${CYAN}   runepkg: The Grand Ritual of the Static Binary      ${RESET}"
echo -e "${BOLD}${CYAN}--------------------------------------------------------${RESET}"

if [ -n "${RUNEPKG_C_SOURCES:-}" ]; then
    echo -e "  ${CYAN}[sync]${RESET}    Paired with Makefile sources (${RUNEPKG_C_SOURCES})"
fi

# Helper for cleaner downloads
fetch_rune() {
    local url=$1
    local dest=$2
    local msg=$3
    printf "  ${YELLOW}[fetch]${RESET}   %s... " "$msg"
    if curl -fsSL "$url" -o "$dest" >> "$LOG_FILE" 2>&1; then
        echo -e "${GREEN}Done.${RESET}"
    else
        echo -e "${RED}Failed!${RESET}"
        echo "Check $LOG_FILE for details."
        exit 1
    fi
}

# 1. Toolchain Acquisition
if [ ! -f "$TOOLCHAIN_DIR/bin/x86_64-linux-musl-gcc" ]; then
    fetch_rune "$TOOLCHAIN_URL" "$FORGE_DIR/toolchain.tgz" "Ancient Toolchain (musl.cc)"
    printf "  ${BLUE}[forge]${RESET}   Extracting toolchain... "
    tar xzf "$FORGE_DIR/toolchain.tgz" -C "$TOOLCHAIN_DIR" --strip-components=1 >> "$LOG_FILE" 2>&1
    rm -f "$FORGE_DIR/toolchain.tgz"
    echo -e "${GREEN}Done.${RESET}"
fi

export PATH="$TOOLCHAIN_DIR/bin:$PATH"
export CC="x86_64-linux-musl-gcc"
export CXX="x86_64-linux-musl-g++"
export AR="x86_64-linux-musl-ar"
export RANLIB="x86_64-linux-musl-ranlib"
export PKG_CONFIG_PATH="$DEPS_DIR/lib/pkgconfig"

NPROC="$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 2)"

# 2. Forge zlib
if [ ! -f "$DEPS_DIR/lib/libz.a" ]; then
    fetch_rune "https://github.com/madler/zlib/archive/refs/tags/v$ZLIB_VER.tar.gz" "$FORGE_DIR/zlib.tgz" "zlib v$ZLIB_VER source"
    printf "  ${BLUE}[forge]${RESET}   Building static zlib (musl)... "
    (
        set -e
        set -o pipefail
        rm -rf "$BUILD_DIR/zlib-$ZLIB_VER"
        cd "$BUILD_DIR"
        tar xzf "$FORGE_DIR/zlib.tgz"
        cd "zlib-$ZLIB_VER"
        ./configure --static --prefix="$DEPS_DIR"
        make -j"$NPROC" install
    ) >> "$LOG_FILE" 2>&1 || { echo -e "${RED}Failed!${RESET}"; echo "Check $LOG_FILE for details."; exit 1; }
    rm -f "$FORGE_DIR/zlib.tgz"
    echo -e "${GREEN}Done.${RESET}"
fi

# 3. Forge libcurl
if [ ! -f "$DEPS_DIR/lib/libcurl.a" ]; then
    fetch_rune "https://curl.se/download/curl-$CURL_VER.tar.gz" "$FORGE_DIR/curl.tgz" "libcurl v$CURL_VER source"
    printf "  ${PURPLE}[ritual]${RESET}  Building static libcurl (musl)... "
    (
        set -e
        set -o pipefail
        rm -rf "$BUILD_DIR/curl-$CURL_VER"
        cd "$BUILD_DIR"
        tar xzf "$FORGE_DIR/curl.tgz"
        cd "curl-$CURL_VER"
        ./configure --host=x86_64-linux-musl \
                    --prefix="$DEPS_DIR" \
                    --enable-static \
                    --disable-shared \
                    --disable-ldap --disable-ldaps \
                    --with-zlib="$DEPS_DIR" \
                    --without-ssl --without-libpsl
        make -j"$NPROC" install
    ) >> "$LOG_FILE" 2>&1 || { echo -e "${RED}Failed!${RESET}"; echo "Check $LOG_FILE for details."; exit 1; }
    rm -f "$FORGE_DIR/curl.tgz"
    echo -e "${GREEN}Done.${RESET}"
fi

# 4. Final Forge: runepkg & runas
echo -e "${GREEN}[final]${RESET}  Forging the static runepkg & runas binaries..."
cd "$START_DIR"

make clean > /dev/null 2>&1 || true

MUSL_LDFLAGS="-L$DEPS_DIR/lib -static -Wl,-z,relro,-z,now -Wl,-z,noexecstack"

if make MUSL=1 WITH_CPP=1 V="${V:-0}" \
     CC="$CC" CXX="$CXX" \
     CFLAGS="-I$DEPS_DIR/include -DENABLE_CPP_FFI" \
     CXXFLAGS="-I$DEPS_DIR/include -DENABLE_CPP_FFI" \
     LDFLAGS="$MUSL_LDFLAGS" \
     LIBS="-lcurl -lz" \
     build; then
    echo -e "${BOLD}${GREEN}--------------------------------------------------------${RESET}"
    echo -e "${BOLD}${GREEN}✅ The Grand Ritual is Complete.${RESET}"
    echo -e "Your static runepkg & runas binaries are ready: ${CYAN}runepkg runas${RESET}"
    if command -v file >/dev/null 2>&1; then
        file "$START_DIR/runepkg" "$START_DIR/runas" || true
    fi
    echo -e "Check linkage with: ${YELLOW}ldd runepkg && ldd runas${RESET}"
    echo -e "${BOLD}${GREEN}--------------------------------------------------------${RESET}"
else
    echo -e "${RED}Failed!${RESET}"
    echo "Static forging failed. Check the errors above."
    exit 1
fi
