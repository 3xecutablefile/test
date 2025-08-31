# coLinux 2.0

> AMD64-only: Targets Intel/AMD Windows 10/11 (x86_64). No ARM.

Rust-first cooperative Linux on Windows: kernel driver (vblk/vtty), IOCP-based daemon, and experimental Linux front-ends. This repo is a work-in-progress toward booting a Kali rootfs via colx_vblk and presenting a login over colx_tty.

Project link: https://github.com/3xecutablefile/test

Quickstart (TL;DR)
- Open an elevated PowerShell (Run as Administrator).
- Clone: `git clone https://github.com/3xecutablefile/test.git && cd test`
- Enable dev test signing: `./scripts/sign-test-cert.ps1` (reboot if prompted)
- Build/install driver: `./scripts/install-driver.ps1`
- Download rootfs (Releases → amd64 `.img.zst` + `.sha256` → save under `C:\KaliSync`) and verify/decompress (see below).
- Configure: set `vblk_backing` in `config\colinux.yaml` to your decompressed `.img`.
- Build daemon: `cd daemon && cargo fmt -- --check && cargo clippy -- -D warnings && cargo build --release`
- Run daemon (console): `..\daemon\target\release\colinux-daemon.exe ..\config\colinux.yaml`
- Or install service: `..\scripts\install-service.ps1` then `..\scripts\start-daemon.ps1`

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

Prepare a rootfs (amd64, manual download)
- Download from the Releases page (amd64 only):
  - `kali-rootfs-rolling-YYYY-MM-DD-amd64.img.zst`
  - `kali-rootfs-rolling-YYYY-MM-DD-amd64.img.zst.sha256`
- Verify SHA256 in PowerShell:
  - `$asset = "C:\\KaliSync\\kali-rootfs-rolling-YYYY-MM-DD-amd64.img.zst"`
  - `$sha = (Get-Content "$asset.sha256").Split(' ')[0].ToLower()`
  - `$hash = (Get-FileHash $asset -Algorithm SHA256).Hash.ToLower()`
  - `if ($sha -ne $hash) { throw "SHA mismatch" }`
- Decompress to raw .img:
  - zstd: `zstd -d -f $asset -o ($asset -replace '\\.zst$','')` (install zstd.exe or use 7‑Zip)
- Set `config/colinux.yaml -> vblk_backing` to the decompressed `.img` path

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
- `docs/TROUBLESHOOTING.md`: common driver/service/rootfs issues and fixes

Notes
- The storage and shared-memory paths are real; booting a guest Linux kernel is in progress. The end goal is mounting a Kali rootfs via `colx_vblk` and presenting a login over `colx_tty`.
- Use raw images (`*.img`). VHDX requires a separate Virtual Disk API layer.

Rootfs images (Releases, amd64 only)
- We publish compressed images as GitHub Release assets to keep the repo lean.
- Expected assets (example tag `rootfs-2025-09-01`):
  - `kali-rootfs-rolling-2025-09-01-amd64.img.zst`
  - `kali-rootfs-rolling-2025-09-01-amd64.img.zst.sha256`
  (See “Prepare a rootfs” for verify + decompress and `vblk_backing` setup.)

Trust and provenance
- Provide SHA256SUMS and, ideally, a detached signature (.sig) with a public key published in this repo.
- Users can regenerate images locally via `userspace/mkrootfs_kali.sh` if they prefer not to trust binaries.

Rootfs images: AMD64 only
coLinux 2.0 currently targets Intel/AMD Windows 10/11 (x86_64) only.

Build targets (code)
- Driver (WDK): build x64 (amd64) only and ship `.sys`/`.inf`.
- Daemon (Rust): `cargo build --release` (x86_64-pc-windows-msvc)

Contributing (commit & push)
- Configure Git (first time only):
  - `git config user.name "Your Name"`
  - `git config user.email "you@example.com"`
- Create a branch: `git checkout -b feature/your-change`
- Make edits, then:
  - `git add -A`
  - `git commit -m "feat: describe your change"`
  - `git push -u origin feature/your-change`
- Open a Pull Request on GitHub.
# coLinux 2.0 — AMD64-Only, Windows Host

> Status: pre‑alpha. AMD64‑only (Intel/AMD Windows 10/11). No ARM.

coLinux 2.0 is a Rust‑first, cooperative Linux runtime for Windows. It pairs a Windows kernel driver (vblk/vtty), an IOCP‑based Rust daemon, and experimental Linux guest front‑ends to push real I/O through shared memory rings — the endgame is booting a Kali rootfs and interacting over a native Windows console.

- Safe I/O: Overlapped `DeviceIoControl` + IOCP reactor (zero busy waits in hot paths)
- Real storage: sector‑based vblk backed by a raw `.img` file
- Shared memory: pagefile‑backed section mapped in kernel + user; simple ring header with tick/ping
- Console path: VTTY rings + Windows console bridge (stdin/stdout)
- Service mode: SCM install/start/stop, Event Log, graceful shutdown

Project link: https://github.com/3xecutablefile/test

Highlights
- AMD64‑only target for clarity and performance
- Minimal, auditable IOCTL surface with clear contracts
- Clean separation of host (Windows driver/daemon) and guest (Linux modules)

Requirements (Windows 10/11, AMD64)
- Administrator PowerShell for driver/service actions
- Visual Studio + Windows Driver Kit (WDK) to build the driver (or use a prebuilt `.sys`)
- Rust (stable) + Cargo for the daemon: https://rustup.rs
- zstd (or 7‑Zip) for decompressing rootfs images

Quickstart (TL;DR)
1) Open an elevated PowerShell (Run as Administrator)
2) Clone: `git clone https://github.com/3xecutablefile/test.git && cd test`
3) Enable test signing (dev only): `./scripts/sign-test-cert.ps1` (reboot if prompted)
4) Build + install driver: `./scripts/install-driver.ps1`
5) Rootfs (amd64): Download `.img.zst` + `.sha256` from Releases to `C:\KaliSync`, then verify + decompress (see below)
6) Configure: set `vblk_backing` in `config\colinux.yaml` to the decompressed `.img`
7) Build daemon: `cd daemon && cargo fmt -- --check && cargo clippy -- -D warnings && cargo build --release`
8) Run (console): `..\daemon\target\release\colinux-daemon.exe ..\config\colinux.yaml`
   - Or service: `..\scripts\install-service.ps1` then `..\scripts\start-daemon.ps1`

Prepare a rootfs (amd64, manual download)
- From Releases (example tag `rootfs-2025-09-01`) download:
  - `kali-rootfs-rolling-2025-09-01-amd64.img.zst`
  - `kali-rootfs-rolling-2025-09-01-amd64.img.zst.sha256`
- Verify SHA256 in PowerShell:
  - `$asset = "C:\\KaliSync\\kali-rootfs-rolling-2025-09-01-amd64.img.zst"`
  - `$sha = (Get-Content "$asset.sha256").Split(' ')[0].ToLower()`
  - `$hash = (Get-FileHash $asset -Algorithm SHA256).Hash.ToLower()`
  - `if ($sha -ne $hash) { throw "SHA mismatch" }`
- Decompress to raw .img:
  - zstd: `zstd -d -f $asset -o ($asset -replace '\\.zst$','')` (or extract with 7‑Zip)
- Point `config\colinux.yaml -> vblk_backing` to the decompressed `.img` path

Run & Verify
- Console run: `RUST_LOG=info .\daemon\target\release\colinux-daemon.exe .\config\colinux.yaml`
  - Expect: “Mapped shared memory …” and steady “tick” progress (info logs)
- Smoke: `daemon\target\release\smoke.exe config\colinux.yaml` (map/ping + 4 KiB read/write test)
- Service logs: Event Viewer → Applications and Services Logs → `coLinux2`

Uninstall / Cleanup
- Stop service: `sc stop coLinux2` (ignore if missing)
- Remove driver: `pnputil /enum-drivers | Select-String coLinux` → `pnputil /delete-driver oemXX.inf /uninstall /force`
- Disable test signing (reboot required): `bcdedit /set testsigning off`

Troubleshooting
- Common issues and fixes are in `docs/TROUBLESHOOTING.md`:
  - Driver test mode, `pnputil` cleanup
  - Service diagnostics, `RUST_LOG`
  - Rootfs SHA/zstd, image path quoting

Architecture (host > guest)
- Windows driver (`driver_c/`):
  - IOCTLs: MAP_SHARED, RUN_TICK, VBLK_{SET_BACKING,READ,WRITE,SUBMIT}, VTTY_{PUSH,PULL}
  - Section mapping (pagefile‑backed), shared ring header with `tick_count` + ping
  - VBLK direct I/O with MDLs (512‑byte sectors, max 128 KiB), buffered async path retained
- Rust daemon (`daemon/`):
  - IOCP reactor with owned buffers; device helpers (vblk/vtty/tick)
  - VBLK: IOCTL queue dispatcher + shared‑ring service
  - VTTY console bridge (stdin/stdout ↔ vtty rings)
  - Windows service wrapper (install/start/stop), Event Log, graceful shutdown
- Linux guest (experimental, `linux/drivers/colinux/`):
  - `colx_core`: maps shared region, exposes `/dev/colx0` for inspection
  - `colx_tty`: `/dev/ttyCOLX0` over byte rings (workqueue pump)
  - `colx_vblk`: blk‑mq `colxblk0`, prototype ring submit (blocking/busy‑wait first cut)

Security & Safety
- AMD64 only; admin rights required for driver/service actions
- Device ACLs recommended (SDDL Admin/System) — to be finalized
- IOCTLs validate buffer sizes and return NTSTATUS; timeouts on user side

Roadmap (high‑value next)
- Multi‑slot ring ABI with status codes and ordering document
- Device SDDL + fuzz/length tests; Driver Verifier clean run under stress
- Guest vtty getty + initramfs so a login prompt appears via the bridge
- Replace vblk busy‑wait prototype with waitqueue + wake on host advance

Build targets (code)
- Driver (WDK): x64 (amd64) `.sys` + `.inf`
- Daemon (Rust): `cargo build --release` (x86_64‑pc‑windows‑msvc)

Contributing (commit & push)
- Configure Git (first time):
  - `git config user.name "Your Name"`
  - `git config user.email "you@example.com"`
- Create a branch: `git checkout -b feature/your-change`
- Make edits, then:
  - `git add -A`
  - `git commit -m "feat: describe your change"`
  - `git push -u origin feature/your-change`
- Open a Pull Request on GitHub
