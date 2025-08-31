#!/usr/bin/env bash
set -euo pipefail

# Prepare a Kali rootfs VHDX (run from WSL/Linux)
# Requirements: qemu-img, debootstrap (or mmdebstrap), sudo

if ! command -v qemu-img >/dev/null; then
  echo "qemu-img not found" >&2; exit 1
fi

SIZE_GB=${SIZE_GB:-16}
OUT=${OUT:-$PWD/kali-rootfs.vhdx}

echo "Creating $OUT ($SIZE_GB GiB)"
qemu-img create -f vhdx "$OUT" ${SIZE_GB}G
echo "Now partition/format and debootstrap Kali into it (manual step)."
echo "Place final VHDX at C:\\KaliSync\\kali-rootfs.vhdx on Windows."

