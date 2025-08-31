# Debian Rootfs for coLinux 2.0 (Raw Image)

This prepares a Debian (bookworm) raw disk image that coLinux’s vblk path can read/write.

Prereqs (run on Linux/WSL):
- sudo, debootstrap, qemu-utils, e2fsprogs, gdisk, dosfstools

Steps:
1. Create the image
   - `cd userspace && DIST=bookworm SIZE_GB=8 OUT=$PWD/debian-rootfs.img sudo ./mkrootfs_debian.sh`
2. Move the image to Windows (e.g., `C:\\KaliSync\\debian-rootfs.img`).
3. Update `config/colinux.yaml` or `config/profiles/debian-dev.yaml` to point `vblk_backing` at the image.
4. Build and run the daemon. The vblk path is real; Linux kernel boot is a separate milestone.

Notes:
- The image has a single ext4 partition labeled `rootfs`.
- We haven’t booted a kernel yet; this is the storage substrate for when the kernel path lands.
- If you need VHDX instead of raw, say the word and we’ll add a Virtual Disk API layer.

