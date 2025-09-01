# Meta Repo: Kernels + Host + Packaging

This optional “meta” repository gathers:

- `kernels/linux`: Upstream Linux kernel (torvalds/linux or linux-stable)
- `host/colinux2`: This repository (Windows host driver + daemon + guest drivers)
- `packaging/*` (optional): Distro packaging, e.g., Debian/Kali kernel packaging

It provides helper scripts to overlay our guest drivers into the kernel tree and build.

## Quick Start (macOS/Linux)

1) Bootstrap a new meta repo (adjust path as needed):

   ```bash
   ./scripts/bootstrap-meta-repo.sh ~/coLinux-meta
   ```

   Defaults:
   - Kernel: `https://github.com/stable-rt/linux-stable.git` (can change to `https://github.com/torvalds/linux.git`)
   - Host repo: `https://github.com/3xecutablefile/test.git` (this repo)

   You can override URLs:

   ```bash
   ./scripts/bootstrap-meta-repo.sh \
     ~/coLinux-meta \
     https://github.com/torvalds/linux.git \
     https://github.com/3xecutablefile/test.git
   ```

2) Overlay drivers into the kernel tree:

   ```bash
   ~/coLinux-meta/scripts/overlay-drivers-into-kernel.sh \
     ~/coLinux-meta/kernels/linux \
     ~/coLinux-meta/host/colinux2
   ```

3) Build the kernel (example minimal):

   ```bash
   cd ~/coLinux-meta/kernels/linux
   make olddefconfig
   make -j$(sysctl -n hw.ncpu || nproc)
   ```

   For Debian/Kali packaging, see `docs/kali-kernel-build.md` and adapt paths to the meta repo locations.

## Windows (PowerShell)

Use the PowerShell variant to bootstrap the meta repo:

```powershell
./scripts/bootstrap-meta-repo.ps1 -Path "$env:USERPROFILE\\coLinux-meta" `
  -KernelRepo "https://github.com/stable-rt/linux-stable.git" `
  -HostRepo   "https://github.com/3xecutablefile/test.git"
```

Then copy the drivers with the POSIX script from WSL or manually copy
`host/colinux2/linux/drivers/colinux` into
`kernels/linux/drivers/colinux` and merge the Kconfig/Makefile entries as needed.

## Layout

```
coLinux-meta/
  kernels/
    linux/             # upstream kernel (submodule)
  host/
    colinux2/          # this repo (submodule)
  scripts/
    overlay-drivers-into-kernel.sh
```

## Notes

- Submodules are pinned; run `git submodule update --remote` in the meta repo to track latest upstreams.
- The overlay script copies guest-side drivers from `host/colinux2/linux/drivers/colinux` into the kernel tree.
- Driver Kconfig/Makefile under `drivers/colinux/` may need enabling in your kernel `.config`.

