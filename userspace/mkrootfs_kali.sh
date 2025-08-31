#!/usr/bin/env bash
# mkrootfs_kali.sh: build a raw ext4 Kali rootfs image for amd64 or arm64, from ANY Linux host.
set -euo pipefail

ARCH="${1:-amd64}"                  # amd64 | arm64
IMG="${2:-kali-rootfs-${ARCH}.img}" # output filename
SIZE_MB="${3:-6144}"                # image size in MB
MNT="$(mktemp -d)"
MIRROR="${MIRROR:-http://http.kali.org/kali}"

echo "[*] Target arch: $ARCH"

if ! command -v debootstrap >/dev/null; then
  echo "install debootstrap first (sudo apt-get install debootstrap)" >&2; exit 1
fi

sudo dd if=/dev/zero of="$IMG" bs=1M count="$SIZE_MB" status=progress
sudo mkfs.ext4 -F "$IMG"
sudo mount -o loop "$IMG" "$MNT"

# cross-arch support: qemu-user-static makes chroot second stage work
CROSS=""
HOST_ARCH="$(dpkg --print-architecture 2>/dev/null || echo unknown)"
if [ "$HOST_ARCH" != "$ARCH" ]; then
  echo "[*] Host arch ($HOST_ARCH) != target arch ($ARCH); using qemu-user-static"
  sudo apt-get update && sudo apt-get install -y qemu-user-static binfmt-support
  # Best-effort copy; ignore if not found (some distros install multiple qemu-*-static names)
  sudo cp /usr/bin/qemu-${ARCH/-/}*-static "$MNT/usr/bin/" 2>/dev/null || true
  CROSS="--foreign"
fi

# stage 1
sudo debootstrap --arch="$ARCH" $CROSS kali-rolling "$MNT" "$MIRROR"
# stage 2 (inside chroot if foreign)
if [ -n "$CROSS" ]; then
  sudo chroot "$MNT" /debootstrap/debootstrap --second-stage
fi

# minimal config
echo "kali-colx" | sudo tee "$MNT/etc/hostname" >/dev/null
echo "root:toor" | sudo chroot "$MNT" chpasswd
echo "proc /proc proc defaults 0 0" | sudo tee -a "$MNT/etc/fstab" >/dev/null

# niceties
sudo mkdir -p "$MNT/root/.colx"
echo "Built: $(date -Iseconds) Arch: $ARCH Size: ${SIZE_MB}MB" | sudo tee "$MNT/root/.colx/BUILDINFO" >/dev/null

sudo umount "$MNT"
rmdir "$MNT"
echo "[✓] Rootfs ready: $IMG"
