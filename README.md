# coLinux 2.0

> AMD64-only: Targets Intel/AMD Windows 10/11 (x86_64). No ARM.

Rust-first cooperative Linux on Windows: kernel driver (vblk/vtty), IOCP-based daemon, and experimental Linux front-ends. This repo is a work-in-progress toward booting a Kali rootfs via colx_vblk and presenting a login over colx_tty.

Project link: https://github.com/3xecutablefile/test

Install (Windows 10/11 on Intel/AMD)
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

Prepare a rootfs (raw image, amd64 only)
- Build locally on a Linux host (WSL OK):
  - `cd userspace && sudo ./mkrootfs_kali.sh` (creates `kali-rootfs-amd64.img`)
  - Move it to Windows (e.g., `C:\\KaliSync\\kali-rootfs-amd64.img`)
  - Point `config/colinux.yaml -> vblk_backing` to that path

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
- `docs/kali-kernel-build.md`: overlay experimental Linux front-ends and build a Debian/Kali kernel package (for guest-side experiments)

Notes
- The storage and shared-memory paths are real; booting a guest Linux kernel is in progress. The end goal is mounting a Kali rootfs via `colx_vblk` and presenting a login over `colx_tty`.
- Use raw images (`*.img`). VHDX requires a separate Virtual Disk API layer.

Rootfs images (optional prebuilt, amd64 only)
- We publish compressed images as GitHub Release assets to keep the repo lean.
- Expected assets (example tag `rootfs-2025-09-01`):
  - `kali-rootfs-rolling-2025-09-01-amd64.img.zst`
  - `kali-rootfs-rolling-2025-09-01-amd64.img.zst.sha256`
- Download + verify + decompress on Windows:
  - `./scripts/get-rootfs.ps1 -Version rootfs-2025-09-01 -AssetBase kali-rootfs-rolling-2025-09-01-amd64.img.zst -OutDir C:\\KaliSync`
  - Ensure `zstd.exe` is available (add to PATH or place at `scripts\\bin\\zstd.exe`).
- Point `config\\colinux.yaml`:
  - `vblk_backing: "C:\\KaliSync\\kali-rootfs-rolling-2025-09-01-amd64.img"`

Trust and provenance
- Provide SHA256SUMS and, ideally, a detached signature (.sig) with a public key published in this repo.
- Users can regenerate images locally via `userspace/mkrootfs_kali.sh` if they prefer not to trust binaries.

Rootfs images: AMD64 only
coLinux 2.0 currently targets Intel/AMD Windows 10/11 (x86_64) only.

Build targets (code)
- Driver (WDK): build x64 (amd64) only and ship `.sys`/`.inf`.
- Daemon (Rust): `cargo build --release` (x86_64-pc-windows-msvc)
