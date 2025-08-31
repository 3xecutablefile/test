#!/usr/bin/env bash
set -euo pipefail

# Build a Debian rootfs raw image using debootstrap.
# Run this on a Linux host (WSL Ubuntu is fine) with sudo and these tools:
#   apt-get install debootstrap qemu-utils e2fsprogs gdisk dosfstools

DIST=${DIST:-bookworm}
SIZE_GB=${SIZE_GB:-8}
OUT=${OUT:-$PWD/debian-rootfs.img}
HOSTNAME=${HOSTNAME:-colinux}

echo "[+] Creating raw image $OUT (${SIZE_GB} GiB)"
qemu-img create -f raw "$OUT" ${SIZE_GB}G

loopdev=$(losetup --find --show "$OUT")
trap 'set +e; sync; sleep 1; losetup -d "$loopdev" >/dev/null 2>&1 || true' EXIT

echo "[+] Partitioning (GPT): 1 x Linux root"
sgdisk --zap-all "$loopdev"
sgdisk -n1:0:0 -t1:8300 -c1:rootfs "$loopdev"
partprobe "$loopdev"

part=${loopdev}p1
mkfs.ext4 -F -L rootfs "$part"

mnt=$(mktemp -d)
trap 'set +e; sync; umount -R "$mnt" >/dev/null 2>&1 || true; losetup -d "$loopdev" >/dev/null 2>&1 || true; rmdir "$mnt" || true' EXIT

echo "[+] Mounting $part at $mnt"
mount "$part" "$mnt"

echo "[+] Bootstrapping Debian ($DIST)"
debootstrap --include="sudo,ca-certificates,net-tools,iproute2,openssh-client" "$DIST" "$mnt" http://deb.debian.org/debian

echo "[+] Configuring fstab, hostname, resolv.conf"
cat >/"$mnt"/etc/fstab <<EOF
LABEL=rootfs / ext4 defaults 0 1
EOF
echo "$HOSTNAME" >"$mnt"/etc/hostname
cp /etc/resolv.conf "$mnt"/etc/resolv.conf || true

echo "[+] Creating first-boot marker"
date -u >"$mnt"/root/IMAGE_CREATED_UTC

echo "[+] Syncing and unmounting"
sync
umount -R "$mnt"
losetup -d "$loopdev"
trap - EXIT
echo "[+] Done: $OUT"

echo "Place the image on Windows and set config.vblk_backing to its path, e.g.:"
echo "  C:\\KaliSync\\debian-rootfs.img"

