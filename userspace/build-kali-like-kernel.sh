#!/usr/bin/env bash
set -euo pipefail

# Build a Debian/Kali-like kernel with the coLinux front-end drivers added under drivers/colinux.
# Run on a Debian/Kali host with build deps installed (kernel-package toolchain):
#   sudo apt-get install fakeroot build-essential libncurses-dev bison flex libssl-dev libelf-dev bc

WORK=${WORK:-$PWD/.kernel}
KVER=${KVER:-"linux-6.6"}
SRC=${SRC:-$WORK/$KVER}

echo "[+] Preparing workdir: $WORK"; mkdir -p "$WORK"
echo "[+] Fetch Linux source (manually if no network): $SRC"
echo "    e.g., apt source linux or wget from kernel.org then extract to $SRC"

echo "[+] Overlay coLinux drivers"
echo "    cp -r linux/drivers/colinux "$SRC"/drivers/"
echo "    cp -r linux/include/uapi/linux/colinux_ring.h "$SRC"/include/uapi/linux/"

cat <<'EOF'
[+] Enable configs (menuconfig or scripts/config):
  - CONFIG_COLINUX=m

Commands (example):
  cd $SRC
  make olddefconfig
  scripts/config --module CONFIG_COLINUX
  make olddefconfig
  make -j$(nproc) bindeb-pkg

Artifacts:
  ../linux-image-*.deb  (install with dpkg -i)
EOF

