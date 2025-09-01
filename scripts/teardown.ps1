param(
  [switch]$RestoreConfig,  # restore %UserProfile%\.wslconfig from .bak if present
  [switch]$ShutdownWSL     # stop all running WSL instances
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Ensure-Admin {
  $id = [Security.Principal.WindowsIdentity]::GetCurrent()
  $p = [Security.Principal.WindowsPrincipal]::new($id)
  if (-not $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Elevating to Administrator..."
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = 'powershell.exe'
    $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" " + ($MyInvocation.BoundParameters.GetEnumerator() | ForEach-Object { if ($_.Value -is [array]) { " -$($_.Key) `"$($_.Value -join ',')`"" } elseif ($_.Value -ne $null -and $($_.Value) -ne '') { " -$($_.Key) `"$($_.Value)`"" } else { '' } }) -join ''
    $psi.Verb = 'runas'
    $proc = [System.Diagnostics.Process]::Start($psi)
    $proc.WaitForExit()
    exit $proc.ExitCode
  }
}

Ensure-Admin

Write-Host "Removing inbound firewall rules added by setup..."
try {
  $rules = Get-NetFirewallRule -ErrorAction SilentlyContinue |
    Where-Object { $_.DisplayName -like 'coLinux inbound port *' -or $_.DisplayName -like 'coLinux2 inbound*' }
  if ($rules) { $rules | Remove-NetFirewallRule }
} catch { Write-Warning $_ }

Write-Host "Cleaning PowerShell profile alias (ex3cutableLinux)..."
try {
  if (Test-Path $PROFILE) {
    $lines = Get-Content -Path $PROFILE -Raw -ErrorAction SilentlyContinue
    if ($lines) {
      $filtered = ($lines -split "`r?`n") | Where-Object {
        ($_ -notmatch '(?i)#\s*coLinux helper') -and
        ($_ -notmatch '(?i)function\s+ex3cutableLinux') -and
        ($_ -notmatch '(?i)\$env:COLINUX_DISTRO')
      }
      $filtered -join "`r`n" | Set-Content -Path $PROFILE -Encoding ASCII
    }
  }
} catch { Write-Warning $_ }

if ($RestoreConfig) {
  $wslcfg = Join-Path $env:USERPROFILE '.wslconfig'
  $bak = "$wslcfg.bak"
  if (Test-Path $bak) {
    Write-Host "Restoring $wslcfg from backup..."
    Copy-Item $bak $wslcfg -Force
  } else {
    Write-Host "No backup found at $bak; skipping restore."
  }
}

if ($ShutdownWSL) {
  Write-Host "Shutting down WSL instances..."
  try { & wsl.exe --shutdown | Out-Null } catch { Write-Warning $_ }
}

Write-Host "Teardown complete."

