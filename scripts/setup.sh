#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   ./scripts/setup.sh [--repo URL] [--dir PATH] [--distro NAME]
#                      [--open-web-ports] [--profiles Private,Domain]
#                      [--allow-from CIDRs] [--public]
#                      [--memory 8GB] [--cpus 4]

case "${OS:-$(uname -s)}" in
  *MINGW*|*MSYS*|*CYGWIN*|*Windows*|*NT*) : ;; 
  *) echo "Run this on Windows (PowerShell available)." >&2; exit 1 ;;
esac

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

REPO=""; DIR=""; DISTRO="kali-linux"; PORTS=""; OPEN_WEB=false; PROFILES="Private,Domain"; ALLOW_FROM=""; PUBLIC=false; MEM=""; CPUS=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo) REPO="$2"; shift 2;;
    --dir) DIR="$2"; shift 2;;
    --distro) DISTRO="$2"; shift 2;;
    --open-web-ports) OPEN_WEB=true; shift 1;;
    --profiles) PROFILES="$2"; shift 2;;
    --allow-from) ALLOW_FROM="$2"; shift 2;;
    --public) PUBLIC=true; shift 1;;
    --memory) MEM="$2"; shift 2;;
    --cpus|--processors) CPUS="$2"; shift 2;;
    *) echo "Unknown arg: $1"; exit 1;;
  esac
done

POWERSHELL_EXE=${POWERSHELL_EXE:-powershell.exe}
ARGS=( -NoProfile -ExecutionPolicy Bypass -File "${SCRIPT_DIR//\//\\}\\setup.ps1" -Distro "$DISTRO" -NetworkingMode mirrored )
[[ -n "$REPO" ]] && ARGS+=( -RepoUrl "$REPO" )
[[ -n "$DIR" ]] && ARGS+=( -InstallDir "$DIR" )
[[ "$OPEN_WEB" == true ]] && ARGS+=( -OpenWebPorts )
[[ -n "$PROFILES" ]] && ARGS+=( -Profiles "$PROFILES" )
[[ -n "$ALLOW_FROM" ]] && ARGS+=( -AllowFrom "$ALLOW_FROM" )
[[ "$PUBLIC" == true ]] && ARGS+=( -Public )
[[ -n "$MEM" ]] && ARGS+=( -Memory "$MEM" )
[[ -n "$CPUS" ]] && ARGS+=( -Processors "$CPUS" )

"$POWERSHELL_EXE" "${ARGS[@]}"

