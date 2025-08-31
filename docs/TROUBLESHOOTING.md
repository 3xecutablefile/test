# Troubleshooting (Windows 10/11, AMD64)

This guide covers common setup and runtime issues for coLinux 2.0 on Intel/AMD Windows.

## Driver install & signing
- Test signing not enabled:
  - Symptom: `pnputil` installs but Windows refuses to start the service or loads then stops.
  - Fix: In elevated PowerShell: `./scripts/sign-test-cert.ps1` then reboot. Confirm desktop shows “Test Mode”.
- Cert install failed:
  - Ensure the self-signed CodeSigning cert was created under `Cert:\\LocalMachine\\My` and test mode is ON.
- `pnputil /add-driver ...` errors:
  - Check `.inf` path is correct and references the built `.sys`.
  - Remove older versions: `pnputil /enum-drivers | Select-String coLinux` then `pnputil /delete-driver oemXX.inf /uninstall /force`.

## Daemon & service
- Service won’t start / exits:
  - Check Windows Event Viewer → Applications and Services Logs → coLinux2.
  - Run in console to see logs: `RUST_LOG=info .\\daemon\\target\\release\\colinux-daemon.exe config\\colinux.yaml`.
- Permission issues:
  - Run PowerShell as Administrator when installing/starting the driver and service.

## Rootfs image
- zstd not found:
  - Install via winget: `winget install -e --id Facebook.Zstandard` or use 7‑Zip to extract `.img.zst`.
- SHA256 mismatch:
  - Re-download both `.img.zst` and `.sha256`. Verify again with:
    - `$sha=(Get-Content $asset.sha256).Split(' ')[0].ToLower()`
    - `$hash=(Get-FileHash $asset -Algorithm SHA256).Hash.ToLower()`
    - `if ($sha -ne $hash){ throw "SHA mismatch" }`
- `vblk_backing` invalid:
  - Ensure the decompressed `.img` exists and the path in `config\\colinux.yaml` is quoted and uses double backslashes, e.g. `C:\\KaliSync\\kali-rootfs-amd64.img`.

## IO / tick issues
- Device open fails (`\\\\.\\coLinux`):
  - Driver not started or access denied. Ensure test mode ON and driver installed.
- IOCTL timeouts:
  - Check the backing image path and permissions.
  - Reduce timeouts in `config` and logs: `RUST_LOG=debug`.
- High CPU / unresponsive:
  - Ensure you are not running a massive I/O in debug logs; set `RUST_LOG=info`.

## Developer checks
- Formatting/lints/tests:
  - `cd daemon && cargo fmt -- --check && cargo clippy -- -D warnings && cargo test`.
- Driver Verifier (optional, advanced):
  - `verifier /standard /driver coLinux.sys`, reboot, observe for violations in Event Viewer.
  - Disable when done: `verifier /reset` and reboot.

## Uninstall / cleanup
- Stop service: `sc stop coLinux2` (ignore if not installed).
- Remove driver: `pnputil /enum-drivers | Select-String coLinux` → `pnputil /delete-driver oemXX.inf /uninstall /force`.
- Disable test signing (reboot required): `bcdedit /set testsigning off`.

If issues persist, open an issue with logs (Event Viewer + console with `RUST_LOG=debug`) and your `config/colinux.yaml` (redact paths if needed).

