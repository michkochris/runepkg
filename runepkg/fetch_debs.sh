#!/usr/bin/env bash
set -euo pipefail

# Fetch a package group (and dependencies) into a local folder.
# Usage:
#   ./fetch_debs.sh build-essential
#   ./fetch_debs.sh --out ./debs build-essential
#
# Requires: apt-get (Debian/Ubuntu/WSL). Uses apt cache for downloads.

OUT_DIR="./debs"
PKGS=()
COUNT=100
NO_RECOMMENDS=1
BUILD_DEPS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out)
      OUT_DIR="$2"
      shift 2
      ;;
    --count)
      COUNT="$2"
      shift 2
      ;;
    --with-recommends)
      NO_RECOMMENDS=0
      shift
      ;;
    --build-deps)
      BUILD_DEPS+=("$2")
      shift 2
      ;;
    *)
      PKGS+=("$1")
      shift
      ;;
  esac
done

if [[ ${#PKGS[@]} -eq 0 && ${#BUILD_DEPS[@]} -eq 0 ]]; then
  echo "Usage: $0 [--out <dir>] [--count N] [--build-deps <pkg>] <pkg1> [pkg2 ...]" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

# Update metadata
sudo apt-get update -y

# Optional: fetch build-deps into apt cache
if [[ ${#BUILD_DEPS[@]} -gt 0 ]]; then
  sudo apt-get build-dep --download-only -y "${BUILD_DEPS[@]}"
fi

# Expand dependencies to reach COUNT packages
DEP_FLAGS=(--recurse --no-suggests --no-conflicts --no-breaks --no-replaces --no-enhances)
if [[ "$NO_RECOMMENDS" -eq 1 ]]; then
  DEP_FLAGS+=(--no-recommends)
fi

mapfile -t ALL_DEPS < <(
  apt-cache depends "${DEP_FLAGS[@]}" "${PKGS[@]}" | \
    awk '/^  (Pre)?Depends:/ {print $2}' | \
    sed 's/<//;s/>//' | \
    sort -u
)

# Always include requested packages first, then fill to COUNT from deps
FINAL_PKGS=()
declare -A SEEN=()
for p in "${PKGS[@]}"; do
  if [[ -n "$p" && -z "${SEEN[$p]:-}" ]]; then
    FINAL_PKGS+=("$p")
    SEEN[$p]=1
  fi
done

for p in "${ALL_DEPS[@]}"; do
  if [[ ${#FINAL_PKGS[@]} -ge $COUNT ]]; then
    break
  fi
  if [[ -n "$p" && -z "${SEEN[$p]:-}" ]]; then
    FINAL_PKGS+=("$p")
    SEEN[$p]=1
  fi
done

if [[ ${#FINAL_PKGS[@]} -lt $COUNT ]]; then
  echo "Warning: only found ${#FINAL_PKGS[@]} packages (requested $COUNT)."
fi

# Download packages into apt cache
if [[ ${#FINAL_PKGS[@]} -gt 0 ]]; then
  sudo apt-get install --download-only -y "${FINAL_PKGS[@]}"
fi

# Copy all downloaded .deb files to OUT_DIR
shopt -s nullglob
for deb in /var/cache/apt/archives/*.deb; do
  cp -f "$deb" "$OUT_DIR/"
done

# Write list file for piping into runepkg
LIST_FILE="$OUT_DIR/deb.list"
ls -1 "$OUT_DIR"/*.deb > "$LIST_FILE"

echo "[fetch_debs] Downloaded .deb files to: $OUT_DIR"
echo "[fetch_debs] List file: $LIST_FILE"
echo "[fetch_debs] Install with: runepkg -i @\"$LIST_FILE\" or cat \"$LIST_FILE\" | runepkg -i -"
