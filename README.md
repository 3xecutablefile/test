# coLinux 2.0

Rust-first cooperative Linux on Windows: kernel driver (vblk/vtty), IOCP-based daemon, and experimental Linux front-ends. This repo is a work-in-progress toward booting a Kali rootfs via colx_vblk and presenting a login over colx_tty.

Project link: https://github.com/3xecutablefile/test

Quick start
- Driver: build with WDK and install via `scripts/install-driver.ps1`
- Daemon: `cd daemon && cargo build --release`
- Rootfs: see `userspace/mkrootfs_debian.sh` and docs

See `config/colinux.yaml` for settings and `docs/` for details.
