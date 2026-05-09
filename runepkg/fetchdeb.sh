#!/usr/bin/env bash
set -euo pipefail

# Target directory for downloaded .deb files
TARGET_DIR="debs"
mkdir -p "$TARGET_DIR"

usage() {
    echo "Usage: $0 [sources.list] <package_name> | <list_file.txt>"
    echo "If the first arg is a file containing 'deb' lines, it is treated as a sources.list."
    exit 1
}

if [ $# -eq 0 ]; then
    usage
fi

# If first argument is a file that contains 'deb' or 'deb-src', treat it as a sources.list
SRC_LIST=""
if [ -f "$1" ]; then
    if grep -qE '^[[:space:]]*(deb|deb-src)\b' "$1"; then
        SRC_LIST="$1"
        shift
        if [ $# -eq 0 ]; then
            echo "Error: sources.list provided but no package or package list specified."
            usage
        fi
    fi
fi

# Prepare temporary APT environment if a sources.list was provided
APT_OPTS=""
TMPDIR=""
if [ -n "$SRC_LIST" ]; then
    TMPDIR=$(mktemp -d)
    TMP_ETC="$TMPDIR/etc/apt"
    TMP_VAR="$TMPDIR/var"
    mkdir -p "$TMP_ETC" "$TMP_VAR/lib/apt/lists/partial" "$TMP_VAR/lib/dpkg" "$TMP_VAR/cache/apt/archives" "$TMP_VAR/cache/apt"
    cp "$SRC_LIST" "$TMP_ETC/sources.list"

    # Use an empty dpkg status in the temporary APT environment so the
    # temporary APT sees no packages installed. This forces apt to
    # download packages from the provided sources rather than treating
    # the system's installed packages as already satisfied.
    : > "$TMP_VAR/lib/dpkg/status"

    chmod -R a+rX "$TMPDIR"

    APT_OPTS="-o Dir::Etc::sourcelist=$TMP_ETC/sources.list -o Dir::Etc::sourceparts=- -o Dir::State::Lists=$TMP_VAR/lib/apt/lists -o Dir::State::status=$TMP_VAR/lib/dpkg/status -o Dir::Cache::Archives=$TMP_VAR/cache/apt/archives -o Dir::Cache::pkgcache=$TMP_VAR/cache/apt/pkgcache.bin -o Dir::Cache::srcpkgcache=$TMP_VAR/cache/apt/srcpkgcache.bin"

    echo "Using temporary apt sources from '$SRC_LIST' — updating index (may prompt for sudo)..."
    if [ "$EUID" -ne 0 ]; then
        sudo apt-get $APT_OPTS update
    else
        apt-get $APT_OPTS update
    fi
fi

# Download a package and its recursive dependencies using the active APT environment
fetch_pkg() {
    local pkg="$1"
    echo "--> Processing: $pkg"

    if [ -n "$APT_OPTS" ]; then
        deps=$(sudo apt-cache $APT_OPTS depends --recurse --no-recommends --no-suggests --no-conflicts --no-breaks --no-replaces --no-enhances "$pkg" 2>/dev/null | grep "^\\w" | sort -u || true)
    else
        deps=$(apt-cache depends --recurse --no-recommends --no-suggests --no-conflicts --no-breaks --no-replaces --no-enhances "$pkg" 2>/dev/null | grep "^\\w" | sort -u || true)
    fi

    if [ -z "$deps" ]; then
        deps="$pkg"
    fi

    echo "Downloading: $deps"
    pushd "$TARGET_DIR" >/dev/null
    if [ -n "$APT_OPTS" ]; then
        # Use apt-get install --download-only to populate the temporary archives cache,
        # then copy .deb files from the temporary cache into the target directory.
        if [ "$EUID" -ne 0 ]; then
            sudo apt-get $APT_OPTS -y -d install $deps || true
            if [ -d "$TMP_VAR/cache/apt/archives" ]; then
                for f in "$TMP_VAR/cache/apt/archives/"*.deb; do
                    [ -e "$f" ] || continue
                    sudo cp -a "$f" . || true
                done
                if [ -n "${SUDO_USER-}" ]; then
                    sudo chown -R "$SUDO_USER":"$SUDO_USER" . || true
                fi
            fi
        else
            apt-get $APT_OPTS -y -d install $deps || true
            if [ -d "$TMP_VAR/cache/apt/archives" ]; then
                for f in "$TMP_VAR/cache/apt/archives/"*.deb; do
                    [ -e "$f" ] || continue
                    cp -a "$f" . || true
                done
            fi
        fi
    else
        apt-get download $deps || true
    fi
    popd >/dev/null
}

# Accept either a package list file or a single package name (same as before)
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

# Cleanup temporary APT state
if [ -n "$TMPDIR" ] && [ -d "$TMPDIR" ]; then
    rm -rf "$TMPDIR"
fi
