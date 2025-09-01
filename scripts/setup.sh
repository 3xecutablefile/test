#!/usr/bin/env bash
set -euo pipefail

# Wrapper to run the PowerShell setup with sensible defaults.
# Usage:
#   ./scripts/setup.sh [--distro NAME] [--ports 22,80,443] [--memory 8GB] [--cpus 4]

if [[ -z "${OS:-}" && "$(uname -s)" != *NT* && "$(uname -s)" != MINGW* && "$(uname -s)" != CYGWIN* ]]; then
  echo "This setup must be run on Windows (PowerShell available)." >&2
  exit 1
fi

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

DISTRO="kali-linux"
PORTS=""
MEM=""
CPUS=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --distro) DISTRO="$2"; shift 2;;
    --ports) PORTS="$2"; shift 2;;
    --memory) MEM="$2"; shift 2;;
    --cpus|--processors) CPUS="$2"; shift 2;;
    *) echo "Unknown arg: $1"; exit 1;;
  esac
done

POWERSHELL_EXE=${POWERSHELL_EXE:-powershell.exe}

ARGS=(
  -NoProfile -ExecutionPolicy Bypass
  -File "${SCRIPT_DIR//\//\\}\\setup.ps1"
  -Distro "$DISTRO"
  -NetworkingMode mirrored
)

if [[ -n "$PORTS" ]]; then ARGS+=( -OpenPorts "$PORTS" ); fi
if [[ -n "$MEM" ]]; then ARGS+=( -Memory "$MEM" ); fi
if [[ -n "$CPUS" ]]; then ARGS+=( -Processors "$CPUS" ); fi

"$POWERSHELL_EXE" "${ARGS[@]}"

