# coLinux 2.0

Rust-first cooperative Linux on Windows: kernel driver (vblk/vtty), IOCP-based daemon, and experimental Linux front-ends. This repo is a work-in-progress toward booting a Kali rootfs via colx_vblk and presenting a login over colx_tty.

Project link: https://github.com/3xecutablefile/test

Install (Windows 10/11)
- Requirements:
  - Windows 10 1809+ (for ConPTY) or Windows 11
  - Administrator PowerShell for driver install/start
  - Visual Studio + Windows Driver Kit (WDK) to build the driver, or use a prebuilt `colinux.sys`
  - Rust (stable) and Cargo for the daemon: https://rustup.rs

1) Clone
   - `git clone https://github.com/3xecutablefile/test.git`
   - `cd test`

2) Enable test signing (dev only)
   - In elevated PowerShell: `./scripts/sign-test-cert.ps1`
   - Reboot when prompted. This allows loading test-signed drivers.

3) Build and install the driver
   - Build with WDK (open the driver_c project) or bring your own `colinux.sys`/`.inf`
   - Install (elevated PowerShell): `./scripts/install-driver.ps1`

4) Configure
   - Edit `config/colinux.yaml` (path to your raw image, memory, queue depths)
   - Example profile: `config/profiles/debian-dev.yaml`

5) Build the daemon
   - `cd daemon && cargo fmt -- --check && cargo clippy -- -D warnings && cargo build --release`

6) Run (choose one)
   - Console mode (elevated PowerShell from repo root):
     - `./daemon/target/release/colinux-daemon.exe config/colinux.yaml`
   - Service mode:
     - Install/start: `./scripts/install-service.ps1` then `./scripts/start-daemon.ps1`
     - Stop/uninstall: `./scripts/uninstall-service.ps1`

Prepare a rootfs (raw image)
- Debian (WSL/Linux host):
  - `cd userspace && sudo ./mkrootfs_debian.sh`
  - Move the produced `debian-rootfs.img` to Windows (e.g., `C:\KaliSync\debian-rootfs.img`)
  - Point `config/colinux.yaml -> vblk_backing` to that path
- Kali (WSL/Linux host):
  - Use debootstrap with Kali repos (see Kali docs) or adapt the Debian script
  - Ensure you create a raw ext4 image (not VHDX) and update `vblk_backing`

Smoke tests
- Daemon I/O path only (no guest kernel yet):
  - `RUST_LOG=info` then run the daemon; you should see mapping + steady ticks
  - Optional: `daemon\target\release\smoke.exe config\colinux.yaml` (map/ping + 4 KiB read/write test)

Help & scripts
- `scripts/sign-test-cert.ps1`: create a local test cert and enable test signing
- `scripts/install-driver.ps1`: install the driver `.inf` via `pnputil`
- `scripts/install-service.ps1` / `scripts/uninstall-service.ps1`: manage Windows service
- `scripts/start-daemon.ps1`: start service if present, otherwise run console daemon

Docs
- `docs/debian-rootfs.md`: build a Debian raw image
- `docs/kali-kernel-build.md`: overlay experimental Linux front-ends and build a Debian/Kali kernel package

Notes
- The storage and shared-memory paths are real; booting a guest Linux kernel is in progress. The end goal is mounting a Kali rootfs via `colx_vblk` and presenting a login over `colx_tty`.
- Use raw images (`*.img`). VHDX requires a separate Virtual Disk API layer.

Release rootfs images (optional)
- We can publish compressed images as GitHub Release assets to keep the repo lean.
- Expected assets (example tag `rootfs-2025-08-31`):
  - `kali-rootfs-rolling-2025-08-31.img.zst`
  - `kali-rootfs-rolling-2025-08-31.img.zst.sha256`
- Download + verify + decompress on Windows:
  - `./scripts/get-rootfs.ps1 -Version rootfs-2025-08-31 -AssetBase kali-rootfs-rolling-2025-08-31.img.zst -OutDir C:\KaliSync`
  - Ensure `zstd.exe` is available (add to PATH or place at `scripts\bin\zstd.exe`).
- Point `config\colinux.yaml`:
  - `vblk_backing: "C:\\KaliSync\\kali-rootfs-rolling-2025-08-31.img"`

Trust and provenance
- Provide SHA256SUMS and, ideally, a detached signature (.sig) with a public key published in this repo.
- Users can regenerate images locally via `userspace/mkrootfs_debian.sh` (or Kali variant) if they prefer not to trust binaries.

Rootfs images: AMD64 and ARM64
- We publish both arches. Pick the one that matches your Windows CPU:
  - Intel/AMD Windows → amd64 image + x64 driver/daemon
  - Windows on ARM → arm64 image + arm64 driver/daemon
- Download examples:
  - AMD64: `./scripts/get-rootfs.ps1 -Version rootfs-YYYY-MM-DD -Arch amd64 -OutDir C:\\KaliSync`
  - ARM64: `./scripts/get-rootfs.ps1 -Version rootfs-YYYY-MM-DD -Arch arm64 -OutDir C:\\KaliSync`
  - Local build on any Linux host (cross-arch supported):
  - `userspace/mkrootfs_kali.sh amd64 kali-rootfs-amd64.img 8192`
  - `userspace/mkrootfs_kali.sh arm64 kali-rootfs-arm64.img 6144`

Build targets (code)
- Driver (WDK): build x64 and arm64 if you plan to support Windows on ARM; ship both `.sys`/`.inf`.
- Daemon (Rust):
  - x64: `cargo build --release`
  - ARM64: `rustup target add aarch64-pc-windows-msvc && cargo build --release --target aarch64-pc-windows-msvc`
  - When packaging Releases, name artifacts distinctly, e.g.:
    - `colinux-daemon-x64.exe` and `colinux-daemon-arm64.exe`
    - Drivers: `colinux-x64.sys` and `colinux-arm64.sys` (+ .inf/.cat)
