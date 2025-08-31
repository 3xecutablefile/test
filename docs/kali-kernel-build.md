# Building a Kali-like Kernel with coLinux Front-Ends

Goal: produce a Debian/Kali kernel package with experimental coLinux front-end drivers under `drivers/colinux`.

Prereqs (on Debian/Kali host):
- `sudo apt-get install fakeroot build-essential libncurses-dev bison flex libssl-dev libelf-dev bc`

Steps:
1) Fetch kernel sources
   - Debian: `apt source linux`
   - Upstream: download `linux-<ver>.tar.xz` from kernel.org and extract

2) Overlay this repo’s driver tree:
```
cp -r <this-repo>/linux/drivers/colinux  <linux-src>/drivers/
cp -r <this-repo>/linux/include/uapi/linux/colinux_ring.h <linux-src>/include/uapi/linux/
```

3) Enable config and build
```
cd <linux-src>
make olddefconfig
scripts/config --module CONFIG_COLINUX
make olddefconfig
make -j$(nproc) bindeb-pkg
```

4) Install and test
```
sudo dpkg -i ../linux-image-*.deb
sudo modprobe colx_core colx_base=0xDEADBEEF colx_size=0x1000
cat /dev/colx0 | hexdump -C | head
```

Notes
- The module maps a provided base/size and exposes `/dev/colx0` for inspection.
- A full cooperative kernel requires deeper paravirtual integration; this is the first building block.

