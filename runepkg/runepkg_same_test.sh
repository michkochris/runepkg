#!/usr/bin/env bash
# ==============================================================================
# - Filename:    runepkg_same_text.sh
# - Author:      <michkochris@gmail.com>
# - Date:        2026-08-26
# - Description: Automated Multi-Tier Verification Suite for runepkg
# - LICENSE:     GPLv3
# ==============================================================================

set -e
export PATH=".:$PATH"
export LC_ALL=C
export TZ=UTC

RUNEPKG_SAME_RESULT="runepkg_same_result.txt"
CONFIG_FILE="/etc/runepkg/runepkgconfig"

log_wait() {
  echo ""
  echo "--------------------------------------------------------------------------------"
  echo "  ⚡ [EXECUTING] $1 . . . . . . (Please wait while processing)"
  echo "--------------------------------------------------------------------------------"
  echo ""
}

echo "Active execution PATH: $PATH"
log_wait "Building and installing runepkg binaries"
rm -rf /srv/lib/runepkg_dir /mnt/runepkg
clear && make clean && make uninstall
make all && make install && make install-runas

: > "$RUNEPKG_SAME_RESULT"

{
  echo "================================================================================"
  echo "RUNEPKG: MULTI-TIER EXECUTION VERIFICATION MATRIX LOG"
  echo "Timestamp:   $(date -u)"
  echo "Active PATH: $PATH"
  echo "Config File: $CONFIG_FILE"
  echo "================================================================================"
  echo ""

  # ----------------------------------------------------------------------------
  # 0. ELF Header, Dynamic Linker & Static Build Audit
  # ----------------------------------------------------------------------------
  log_wait "0. ELF Header & Static Linkage Audit"
  echo "=== ELF & LINKAGE AUDIT: RUNEPKG BINARY ==="
  file ./runepkg 2>&1 || true
  ldd ./runepkg 2>&1 || echo "ldd output: not a dynamic executable (Static ELF confirmed)"
  readelf -h ./runepkg | grep -E "Type:|Machine:|Entry point" 2>&1 || true
  readelf -l ./runepkg | grep -i "INTERP" 2>&1 || echo "No INTERP program header found (PASS)."
  readelf -d ./runepkg 2>&1 || echo "No dynamic section found (PASS)."
  echo ""

  # ----------------------------------------------------------------------------
  # TIER 1: Alternate Install Location Mode
  # ----------------------------------------------------------------------------
  log_wait "TIER 1: Alternate Install Location Verification (/srv/lib/runepkg_dir/install_dir)"
  sed -i 's|^install_dir=.*|install_dir=/srv/lib/runepkg_dir/install_dir|' "$CONFIG_FILE"

  echo "=== TIER 1: CONFIG ==="
  runepkg --print-config

  echo "=== TIER 1: UPDATE ==="
  runepkg update

  echo "=== TIER 1: INSTALL FILE ==="
  runepkg -i file

  echo "=== TIER 1: LD SYMLINK ==="
  file /srv/lib/runepkg_dir/install_dir/lib/ld-linux-x86-64.so.2 2>&1 || true
  ls -la /srv/lib/runepkg_dir/install_dir/lib/ld-linux-x86-64.so.2 2>&1 || true
  echo ""

  # ----------------------------------------------------------------------------
  # TIER 2: Alternate Target Architecture Mode (ARM64)
  # ----------------------------------------------------------------------------
  log_wait "TIER 2: Alternate Target Architecture Verification (arm64:aarch64-linux-gnu)"
  echo "=== TIER 2: TARGET UPDATE (ARM64) ==="
  runepkg --target=arm64:aarch64-linux-gnu update

  echo "=== TIER 2: TARGET INSTALL FILE (ARM64) ==="
  runepkg --target=arm64:aarch64-linux-gnu -i file

  echo "=== TIER 2: TARGET LD SYMLINK (ARM64) ==="
  file /mnt/runepkg/aarch64-linux-gnu/lib/ld-linux-aarch64.so.1 2>&1 || true
  ls -la /mnt/runepkg/aarch64-linux-gnu/lib/ld-linux-aarch64.so.1 2>&1 || true
  echo ""

  # ----------------------------------------------------------------------------
  # TIER 3: Live Host Root Rescue Mode
  # ----------------------------------------------------------------------------
  log_wait "TIER 3: Live Host Root Rescue Mode Verification (install_dir=/)"
  sed -i 's|^install_dir=.*|install_dir=/|' "$CONFIG_FILE"

  echo "=== TIER 3: HOST ROOT CONFIG ==="
  runepkg --print-config

  echo "=== TIER 3: HOST ROOT UPDATE ==="
  runepkg update

  echo "=== TIER 3: HOST ROOT INSTALL FILE ==="
  runepkg -i file

  echo "=== TIER 3: HOST ROOT LD SYMLINK ==="
  file /lib/ld-linux-x86-64.so.2 2>&1 || true
  ls -la /lib/ld-linux-x86-64.so.2 2>&1 || true

  echo "=== TIER 3: LDD /usr/bin/file ==="
  ldd /usr/bin/file 2>&1 || true
  echo ""

  log_wait "TIER 3: Host System Integrity Audit (bash, ls, grep, openssl)"
  echo "=== TIER 3: HOST SYSTEM INTEGRITY AUDIT ==="
  echo "--- Testing Core Shell (bash) ---"
  bash --version | head -n 1 2>&1 || true
  ldd /bin/bash 2>&1 || true

  echo "--- Testing Core Utility (ls) ---"
  ls -la /usr/bin/ls 2>&1 || true
  ldd /usr/bin/ls 2>&1 || true

  echo "--- Testing Text Utility (grep) ---"
  grep --version | head -n 1 2>&1 || true

  echo "--- Testing System Crypto / SSL Libraries (openssl) ---"
  openssl version 2>&1 || echo "openssl utility not present"
  echo ""

  echo "================================================================================"
  echo "VERIFICATION COMPLETE: ALL CHECKS LOGGED TO $RUNEPKG_SAME_RESULT"
  echo "================================================================================"
} 2>&1 | tee "$RUNEPKG_SAME_RESULT"

echo "Verification suite executed successfully. Output logged to $RUNEPKG_SAME_RESULT."
