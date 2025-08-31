# Release: rootfs-YYYY-MM-DD (AMD64)

Assets (attach both):
- `kali-rootfs-rolling-YYYY-MM-DD-amd64.img.zst`
- `kali-rootfs-rolling-YYYY-MM-DD-amd64.img.zst.sha256`

Verify + decompress on Windows (PowerShell):
- `$asset = "C:\\KaliSync\\kali-rootfs-rolling-YYYY-MM-DD-amd64.img.zst"`
- `$sha = (Get-Content "$asset.sha256").Split(' ')[0].ToLower()`
- `$hash = (Get-FileHash $asset -Algorithm SHA256).Hash.ToLower()`
- `if ($sha -ne $hash) { throw "SHA mismatch" }`
- `zstd -d -f $asset -o ($asset -replace '\\.zst$','')`

Configure daemon:
- `config\\colinux.yaml` → `vblk_backing: "C:\\KaliSync\\kali-rootfs-rolling-YYYY-MM-DD-amd64.img"`

Notes
- AMD64‑only (Intel/AMD Windows 10/11)
- Images are raw ext4; do not commit them to git; Releases keep the repo lean

