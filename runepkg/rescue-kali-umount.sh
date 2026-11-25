#!/usr/bin/env bash
set -euo pipefail

MOUNT_DIR="/mnt/kali"

if mountpoint -q "$MOUNT_DIR"; then
    sync
    umount -l "$MOUNT_DIR"
    echo "[+] Synced and unmounted $MOUNT_DIR"
else
    echo "[*] $MOUNT_DIR is not mounted."
fi
