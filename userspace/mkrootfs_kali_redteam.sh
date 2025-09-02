#!/usr/bin/env bash
set -euo pipefail

# Build a Kali-based raw image with preinstalled red-team tooling.
# Requires: sudo, debootstrap, qemu-utils, e2fsprogs, gdisk, dosfstools, chroot networking.

IMG=${IMG:-$PWD/kali-redteam.img}
SIZE_GB=${SIZE_GB:-16}
MIRROR=${MIRROR:-http://http.kali.org/kali}
HOSTNAME=${HOSTNAME:-colx}

echo "[+] Creating raw image: $IMG (${SIZE_GB} GiB)"
qemu-img create -f raw "$IMG" ${SIZE_GB}G

loopdev=$(sudo losetup --find --show "$IMG")
trap 'set +e; sync; sleep 1; sudo losetup -d "$loopdev" >/dev/null 2>&1 || true' EXIT

echo "[+] Partitioning (GPT)"
sudo sgdisk --zap-all "$loopdev"
sudo sgdisk -n1:0:0 -t1:8300 -c1:rootfs "$loopdev"
sudo partprobe "$loopdev"
part=${loopdev}p1

echo "[+] Formatting ext4"
sudo mkfs.ext4 -F -L rootfs "$part"

mnt=$(mktemp -d)
trap 'set +e; sync; sudo umount -R "$mnt" >/dev/null 2>&1 || true; sudo losetup -d "$loopdev" >/dev/null 2>&1 || true; rmdir "$mnt" || true' EXIT
sudo mount "$part" "$mnt"

echo "[+] Bootstrap Kali (amd64)"
sudo debootstrap --arch=amd64 kali-rolling "$mnt" "$MIRROR"

echo "[+] Configure base"
echo "$HOSTNAME" | sudo tee "$mnt/etc/hostname" >/dev/null
cat | sudo tee "$mnt/etc/fstab" >/dev/null <<EOF
LABEL=rootfs / ext4 defaults 0 1
EOF
sudo cp /etc/resolv.conf "$mnt/etc/resolv.conf" || true

echo "[+] Enable non-free + update"
sudo chroot "$mnt" bash -c 'sed -i "s/^deb /deb [arch=amd64] /" /etc/apt/sources.list || true; apt-get update -y'

echo "[+] Install core tooling"
sudo chroot "$mnt" bash -c 'DEBIAN_FRONTEND=noninteractive apt-get install -y \
  linux-image-amd64 systemd-sysv sudo ca-certificates net-tools iproute2 iputils-ping ethtool pciutils usbutils \
  openssh-client openssh-server curl wget git python3 python3-pip golang-go build-essential jq'

echo "[+] Install red-team tooling (Kali)"
sudo chroot "$mnt" bash -c 'DEBIAN_FRONTEND=noninteractive apt-get install -y \
  nmap arp-scan masscan netcat-traditional socat proxychains4 \
  metasploit-framework sqlmap hydra crackmapexec seclists \
  impacket-scripts smbclient \
  chisel bettercap aircrack-ng hcxdumptool \
  dnsutils whois tcpdump telnet rlwrap'

echo "[+] Add convenience users and SSH"
sudo chroot "$mnt" bash -c 'echo root:toor | chpasswd; systemctl enable ssh || true'

echo "[+] Mark image and clean"
date -u | sudo tee "$mnt/root/IMAGE_CREATED_UTC" >/dev/null
sudo chroot "$mnt" bash -c 'apt-get clean'

echo "[+] Unmount and detach"
sync
sudo umount -R "$mnt"
sudo losetup -d "$loopdev"
trap - EXIT
echo "[+] Done: $IMG"

