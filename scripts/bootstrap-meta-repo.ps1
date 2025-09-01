param(
  [Parameter(Mandatory=$true)] [string]$Path,
  [string]$KernelRepo = "https://github.com/stable-rt/linux-stable.git",
  [string]$HostRepo   = "https://github.com/3xecutablefile/test.git"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force -Path $Path | Out-Null
Push-Location $Path
try {
  if (-not (Test-Path ".git")) { git init | Out-Null }

  New-Item -ItemType Directory -Force -Path kernels, host, scripts | Out-Null

  if (-not (Test-Path "kernels/linux/.git")) {
    git submodule add $KernelRepo kernels/linux | Out-Null
  }
  if (-not (Test-Path "host/colinux2/.git")) {
    git submodule add $HostRepo host/colinux2 | Out-Null
  }

  git submodule update --init --recursive | Out-Null

  $overlay = @'
#!/usr/bin/env bash
set -euo pipefail
# Copy guest driver sources into a kernel tree
# Usage: ./overlay-drivers-into-kernel.sh /path/to/kernel /path/to/colinux2

KERNEL_DIR=${1:-}
HOST_DIR=${2:-}
if [[ -z "${KERNEL_DIR}" || -z "${HOST_DIR}" ]]; then
  echo "usage: $0 /path/to/kernel /path/to/colinux2" >&2
  exit 1
fi

SRC="${HOST_DIR}/linux/drivers/colinux"
DST="${KERNEL_DIR}/drivers/colinux"

if [[ ! -d "${SRC}" ]]; then
  echo "missing source: ${SRC}" >&2
  exit 1
fi

mkdir -p "${DST}"
rsync -a --delete "${SRC}/" "${DST}/"

echo "Overlay complete: ${SRC} -> ${DST}"
echo "Remember to enable drivers in Kconfig and rebuild the kernel."
'@
  Set-Content -Path "scripts/overlay-drivers-into-kernel.sh" -Value $overlay -Encoding UTF8
}
finally {
  Pop-Location
}

Write-Host "Meta repo bootstrapped at $Path"
Write-Host "- Kernel: $KernelRepo -> kernels/linux"
Write-Host "- Host:   $HostRepo   -> host/colinux2"
Write-Host "Next: run ./scripts/overlay-drivers-into-kernel.sh kernels/linux host/colinux2 (from WSL or bash)"

