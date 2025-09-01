param(
  [string]$Distro = "kali-linux",
  [string]$NetworkingMode = "mirrored",
  [string[]]$OpenPorts = @('80','443','8080'),
  [string]$Memory = "",       # e.g., 8GB (optional)
  [string]$Processors = ""    # e.g., 4 (optional)
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Ensure-Admin {
  $currentIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = New-Object Security.Principal.WindowsPrincipal($currentIdentity)
  if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Elevating to Administrator..."
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'powershell.exe'
    $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" " + ($MyInvocation.BoundParameters.GetEnumerator() | ForEach-Object { if ($_.Value -is [array]) { " -$($_.Key) `"$($_.Value -join ',')`"" } elseif ($_.Value -ne $null -and $($_.Value) -ne '') { " -$($_.Key) `"$($_.Value)`"" } else { '' } }) -join ''
    $psi.Verb = 'runas'
    $proc = [System.Diagnostics.Process]::Start($psi)
    $proc.WaitForExit()
    exit $proc.ExitCode
  }
}

Ensure-Admin

Write-Host "Installing prerequisites (may require reboot prompts)..."
try {
  # Modern WSL install will add required features automatically
  & wsl.exe --install 2>$null | Out-Null
} catch {}

Write-Host "Updating kernel and usermode tools..."
try { & wsl.exe --update --web-download | Out-Null } catch {}

Write-Host "Setting default version to WSL2..."
& wsl.exe --set-default-version 2 | Out-Null

Write-Host "Ensuring distro is installed: $Distro"
try {
  $installed = (& wsl.exe --list --quiet) -split "`r?`n" | Where-Object { $_.Trim() -ne '' }
  if ($installed -notcontains $Distro) {
    & wsl.exe --install -d $Distro
  } else {
    Write-Host "Distro '$Distro' already installed."
  }
} catch {
  & wsl.exe --install -d $Distro
}

$wslcfg = Join-Path $env:USERPROFILE ".wslconfig"
if (Test-Path $wslcfg) {
  Copy-Item $wslcfg "$wslcfg.bak" -Force
}

$lines = @("[wsl2]", "networkingMode=$NetworkingMode", "dnsTunneling=true", "autoProxy=true")
if ($Memory -and $Memory.Trim() -ne '') { $lines += "memory=$Memory" }
if ($Processors -and $Processors.Trim() -ne '') { $lines += "processors=$Processors" }
$lines | Set-Content -Path $wslcfg -Encoding ASCII

Write-Host "Applied networking + resource config to $wslcfg"

Write-Host "Stopping running instances to apply config..."
& wsl.exe --shutdown | Out-Null

if ($OpenPorts.Count -gt 0) {
  foreach ($p in $OpenPorts) {
    if ($p -match '^[0-9]+$') {
      $ruleName = "coLinux2 inbound port $p"
      try {
        netsh advfirewall firewall add rule name="$ruleName" dir=in action=allow protocol=TCP localport=$p | Out-Null
      } catch {}
    }
  }
  Write-Host "Added Windows Firewall rules for ports: $($OpenPorts -join ', ')"
}

Write-Host "Done. Launch your distro to complete first-time setup (user/password)."
Write-Host "After that, services you run inside the distro will listen on the Windows host IP for the opened ports."
