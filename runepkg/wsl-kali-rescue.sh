#!/usr/bin/env bash
set -euo pipefail

MOUNT_DIR="/mnt/kali"

# Optional unmount mode: ./wsl-kali-rescue.sh -u
if [[ "${1:-}" == "-u" || "${1:-}" == "--umount" ]]; then
    if mountpoint -q "$MOUNT_DIR"; then
        umount -l "$MOUNT_DIR"
        echo "[+] Lazily unmounted $MOUNT_DIR"
    else
        echo "[*] $MOUNT_DIR is not currently mounted."
    fi
    exit 0
fi

mkdir -p "$MOUNT_DIR"

if mountpoint -q "$MOUNT_DIR"; then
    umount -l "$MOUNT_DIR"
fi

FOUND_DEV=""
for dev in $(lsblk -ln -o NAME,FSTYPE,MOUNTPOINTS | awk '$2=="ext4" && $3=="" && $1 !~ /^sd[ab]$/ {print "/dev/"$1}'); do
    RO_STATE=$(blockdev --getro "$dev" 2>/dev/null || echo "0")
    if [[ "$RO_STATE" == "1" ]]; then
        MOUNT_OPTS="-o ro,noload"
    else
        MOUNT_OPTS="-o rw"
    fi

    echo "[*] Probing $dev ($MOUNT_OPTS)..."
    if mount $MOUNT_OPTS "$dev" "$MOUNT_DIR"; then
        if grep -q '^ID=kali' "$MOUNT_DIR/etc/os-release" 2>/dev/null; then
            FOUND_DEV="$dev"
            break
        fi
        umount -l "$MOUNT_DIR"
    fi
done

if [[ -z "$FOUND_DEV" ]]; then
    echo "[!] Kali Linux not found."
    exit 1
fi

echo "[+] Found and mounted Kali ($FOUND_DEV) at $MOUNT_DIR"
echo "    (Tip: Run '$0 -u' when done to lazily unmount)"
echo ""
ls -la "$MOUNT_DIR/root" "$MOUNT_DIR/home"
