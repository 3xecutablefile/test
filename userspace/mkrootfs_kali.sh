#!/usr/bin/env bash
# mkrootfs_kali.sh: build a raw ext4 Kali rootfs (amd64 only)
set -euo pipefail

IMG="${1:-kali-rootfs-amd64.img}"
SIZE_MB="${2:-6144}"
MNT="$(mktemp -d)"
MIRROR="${MIRROR:-http://http.kali.org/kali}"

echo "[*] Target: amd64"

if ! command -v debootstrap >/dev/null; then
  echo "install debootstrap first (sudo apt-get install debootstrap)" >&2; exit 1
fi

sudo dd if=/dev/zero of="$IMG" bs=1M count="$SIZE_MB" status=progress
sudo mkfs.ext4 -F "$IMG"
sudo mount -o loop "$IMG" "$MNT"

sudo debootstrap --arch=amd64 kali-rolling "$MNT" "$MIRROR"

echo "kali-colx" | sudo tee "$MNT/etc/hostname" >/dev/null
echo "root:toor" | sudo chroot "$MNT" chpasswd
echo "proc /proc proc defaults 0 0" | sudo tee -a "$MNT/etc/fstab" >/dev/null

sudo umount "$MNT"
rmdir "$MNT"
echo "[✓] Rootfs ready: $IMG"
