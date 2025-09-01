#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   ./scripts/teardown.sh [--restore-config] [--shutdown]

case "${OS:-$(uname -s)}" in
  *MINGW*|*MSYS*|*CYGWIN*|*Windows*|*NT*) : ;; 
  *) echo "Run this on Windows (PowerShell available)." >&2; exit 1 ;;
esac

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

RESTORE=false
SHUTDOWN=false
while [[ $# -gt 0 ]]; do
  case "$1" in
    --restore-config) RESTORE=true; shift 1;;
    --shutdown) SHUTDOWN=true; shift 1;;
    *) echo "Unknown arg: $1"; exit 1;;
  esac
done

POWERSHELL_EXE=${POWERSHELL_EXE:-powershell.exe}
ARGS=( -NoProfile -ExecutionPolicy Bypass -File "${SCRIPT_DIR//\//\\}\\teardown.ps1" )
[[ "$RESTORE" == true ]] && ARGS+=( -RestoreConfig )
[[ "$SHUTDOWN" == true ]] && ARGS+=( -ShutdownWSL )

"$POWERSHELL_EXE" "${ARGS[@]}"

