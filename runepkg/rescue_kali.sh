#!/usr/bin/env bash
set -euo pipefail

MOUNT_POINT="/mnt/kali"

echo "[*] Scanning block devices for unmounted Kali rootfs..."

# Ensure mount point exists
sudo mkdir -p "$MOUNT_POINT"

# Check if already mounted
if mountpoint -q "$MOUNT_POINT"; then
    echo "[!] $MOUNT_POINT is already mounted. Proceeding with repair..."
    ALREADY_MOUNTED=true
else
    ALREADY_MOUNTED=false
    # Find unmounted ext4 block devices attached to WSL (usually /dev/sdX)
    CANDIDATES=$(lsblk -rn -o NAME,FSTYPE,MOUNTPOINT | awk '$2 == "ext4" && $3 == "" {print "/dev/"$1}')

    if [ -z "$CANDIDATES" ]; then
        echo "[ERROR] No unmounted ext4 block devices found."
        echo "        Make sure Kali is shut down or its VHDX is attached to WSL."
        exit 1
    fi

    TARGET_DEV=""
    for dev in $CANDIDATES; do
        echo "[*] Checking $dev..."
        if sudo mount "$dev" "$MOUNT_POINT" 2>/dev/null; then
            if grep -qi "kali" "$MOUNT_POINT/etc/os-release" 2>/dev/null; then
                TARGET_DEV="$dev"
                echo "[+] Found Kali rootfs on $TARGET_DEV"
                break
            else
                sudo umount "$MOUNT_POINT"
            fi
        fi
    done

    if [ -z "$TARGET_DEV" ]; then
        echo "[ERROR] Could not identify a Kali rootfs among unmounted ext4 devices: $CANDIDATES"
        exit 1
    fi
fi

# Cleanup trap to ensure we unmount cleanly if we mounted it
cleanup() {
    if [ "$ALREADY_MOUNTED" = false ] && mountpoint -q "$MOUNT_POINT"; then
        # Step out of MOUNT_POINT if current shell CWD is inside it
        if [[ "$PWD" == "$MOUNT_POINT"* ]]; then
            cd /
        fi
        echo "[*] Syncing and unmounting $MOUNT_POINT..."
        sync
        sudo umount "$MOUNT_POINT"
        echo "[+] Unmounted cleanly."
    fi
}
trap cleanup EXIT

echo "[*] Inspecting architecture and dynamic linker in $MOUNT_POINT..."

# Ensure top-level usrmerge symlinks (/bin, /sbin, /lib) aren't broken real directories
for dir in bin sbin lib; do
    if [ -d "$MOUNT_POINT/$dir" ] && [ ! -L "$MOUNT_POINT/$dir" ]; then
        echo "[!] Restoring usrmerge symlink for /$dir..."
        sudo mkdir -p "$MOUNT_POINT/usr/$dir"
        sudo cp -a "$MOUNT_POINT/$dir/." "$MOUNT_POINT/usr/$dir/" 2>/dev/null || true
        sudo rm -rf "$MOUNT_POINT/$dir"
    fi
    sudo ln -sfn "usr/$dir" "$MOUNT_POINT/$dir"
done

if [ -d "$MOUNT_POINT/usr/lib/x86_64-linux-gnu" ]; then
    ARCH="x86_64"
    LD_SRC="ld-linux-x86-64.so.2"

    if [ ! -f "$MOUNT_POINT/usr/lib/x86_64-linux-gnu/$LD_SRC" ]; then
        echo "[ERROR] Could not find $LD_SRC in /usr/lib/x86_64-linux-gnu!"
        exit 1
    fi

    echo "[*] Repairing usrmerge and dynamic linker symlinks for $ARCH..."
    if [ -d "$MOUNT_POINT/lib64" ] && [ ! -L "$MOUNT_POINT/lib64" ]; then
        echo "[!] /lib64 was replaced with a real directory. Migrating contents and restoring symlink..."
        sudo mkdir -p "$MOUNT_POINT/usr/lib64"
        sudo cp -a "$MOUNT_POINT/lib64/." "$MOUNT_POINT/usr/lib64/" 2>/dev/null || true
        sudo rm -rf "$MOUNT_POINT/lib64"
    fi

    sudo mkdir -p "$MOUNT_POINT/usr/lib64"
    sudo ln -sfn usr/lib64 "$MOUNT_POINT/lib64"
    sudo ln -sfn "../lib/x86_64-linux-gnu/$LD_SRC" "$MOUNT_POINT/usr/lib64/$LD_SRC"
    sudo ln -sfn "x86_64-linux-gnu/$LD_SRC" "$MOUNT_POINT/usr/lib/$LD_SRC"

elif [ -d "$MOUNT_POINT/usr/lib/aarch64-linux-gnu" ]; then
    ARCH="aarch64"
    LD_SRC="ld-linux-aarch64.so.1"

    if [ ! -f "$MOUNT_POINT/usr/lib/aarch64-linux-gnu/$LD_SRC" ]; then
        echo "[ERROR] Could not find $LD_SRC in /usr/lib/aarch64-linux-gnu!"
        exit 1
    fi

    echo "[*] Repairing usrmerge and dynamic linker symlinks for $ARCH..."
    sudo ln -sfn "aarch64-linux-gnu/$LD_SRC" "$MOUNT_POINT/usr/lib/$LD_SRC"
else
    echo "[ERROR] Unknown or severely damaged /usr/lib layout in $MOUNT_POINT."
    exit 1
fi

echo "[*] Verifying dynamic linker execution inside chroot..."
if sudo chroot "$MOUNT_POINT" /bin/sh -c "true" 2>/dev/null; then
    echo "[SUCCESS] Kali dynamic linker repaired! /bin/sh executes cleanly."
else
    echo "[WARNING] Symlinks were restored, but chroot /bin/sh test failed. Check /usr/bin/sh or libc6."
    exit 1
fi
