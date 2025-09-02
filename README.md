# coLinux 2.0

> AMD64-only: Targets Intel/AMD Windows 10/11 (x86_64). No ARM.

Rust-first cooperative Linux on Windows: kernel driver (vblk/vtty), IOCP-based daemon, and experimental Linux front-ends. This repo is a work-in-progress toward booting a Kali rootfs via colx_vblk and presenting a login over colx_tty.

Project link: https://github.com/3xecutablefile/test

One‑Shot Setup (TL;DR)
- From a Windows shell, run: `scripts/setup.sh`
- Options:
  - `--open-web-ports` to allow inbound 80/443/8080 on Private/Domain profiles
  - `--public` to include the Public profile (use cautiously on public Wi‑Fi)
  - `--allow-from 10.0.0.0/8,192.168.0.0/16` to restrict allowed source ranges
  - `--memory 8GB --cpus 4` to set resource limits
- After setup, launch the Linux environment any time: `ex3cutableLinux`
- If your network blocks inbound, expose a port externally for testing:
  - `scripts/tunnel.ps1 -Provider cloudflare -Port 8080` (or `tailscale`)

Quickstart (manual)
- Open an elevated PowerShell (Run as Administrator).
- Clone: `git clone https://github.com/3xecutablefile/test.git && cd test`
- Enable dev test signing: `./scripts/sign-test-cert.ps1` (reboot if prompted)
- Build/install driver: `./scripts/install-driver.ps1`
- Download rootfs (Releases → amd64 `.img.zst` + `.sha256` → save under `C:\KaliSync`) and verify/decompress (see below).
- Configure: set `vblk_backing` in `config\colinux.yaml` to your decompressed `.img`.
- Build daemon: `cd daemon && cargo fmt -- --check && cargo clippy -- -D warnings && cargo build --release`
- Run daemon (console): `..\daemon\target\release\colinux-daemon.exe ..\config\colinux.yaml`
- Or install service: `..\scripts\install-service.ps1` then `..\scripts\start-daemon.ps1`

Experimental: pure kernel (WHP)
- This adds a Windows Hypervisor Platform (Hyper-V) path to eventually boot a real Linux kernel.
- Requirements: Virtualization enabled in BIOS; Windows Features → “Hyper-V Platform” or “Windows Hypervisor Platform”.
- Usage (operator console; skeleton only):
  - Run with no args: `./daemon/target/release/colinux-daemon.exe`
  - Select PVH/OVMF and Stealth/Passthrough, then enter kernel/initrd/cmdline.
  - You should see a WHP run/exit message; full kernel init is in progress.
  - This is separate from the cooperative path and won’t affect vblk/vtty.

Networking (WHP guest, experimental)
- Toggle runtime backend:
  - Stealth: `--net stealth` (shared‑IP via slirp/host‑socket proxy; no new MAC)
  - Passthrough: `--net passthrough` (L2 presence via TAP/USB NIC)
- See `docs/hypervisor-net.md` for the architecture and roadmap.

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

Host‑IP access (inbound)
- Some networks allow inbound access to services you run on your PC; others block it (client isolation, captive portals).
- To allow inbound on common web ports (when permitted), run the one‑shot setup with `--open-web-ports`.
- If inbound is blocked on your network, expose a single port with the helper:
  - `scripts/tunnel.ps1 -Provider cloudflare -Port 8080`

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
