param(
  [string]$Distro = "kali-linux",
  [string]$NetworkingMode = "mirrored",
  [switch]$OpenWebPorts,               # open 80,443,8080 on Private,Domain
  [string]$Profiles = "Private,Domain", # firewall profile scope
  [string]$AllowFrom = "",            # optional CIDR/CIDRs to restrict remote sources
  [switch]$Public,                     # include Public profile
  [string]$Memory = "",               # e.g., 8GB (optional)
  [string]$Processors = "",           # e.g., 4 (optional)
  [string]$RepoUrl = "",              # Optional: clone this repo first
  [string]$InstallDir = "$env:USERPROFILE\\colinux2" # Where to clone if RepoUrl set
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

if ($RepoUrl -and $RepoUrl.Trim() -ne '') {
  if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Error "Git is required to clone. Install Git for Windows and retry."
    exit 1
  }
  if (-not (Test-Path $InstallDir)) { New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null }
  if (-not (Test-Path (Join-Path $InstallDir '.git'))) {
    git clone $RepoUrl $InstallDir
  }
}

Write-Host "Installing/Updating environment..."
try { & wsl.exe --install 2>$null | Out-Null } catch {}
try { & wsl.exe --update --web-download | Out-Null } catch {}
& wsl.exe --set-default-version 2 | Out-Null

$wslcfg = Join-Path $env:USERPROFILE ".wslconfig"
if (Test-Path $wslcfg) { Copy-Item $wslcfg "$wslcfg.bak" -Force }
$lines = @("[wsl2]", "networkingMode=$NetworkingMode", "dnsTunneling=true", "autoProxy=true")
if ($Memory) { $lines += "memory=$Memory" }
if ($Processors) { $lines += "processors=$Processors" }
$lines | Set-Content -Path $wslcfg -Encoding ASCII
& wsl.exe --shutdown | Out-Null

function Add-CoLinuxFirewallRules {
  param([string[]]$Ports, [string]$Profiles, [string]$AllowFrom)
  if (-not $Ports -or $Ports.Count -eq 0) { return }
  foreach ($p in $Ports) {
    if ($p -notmatch '^[0-9]+$') { continue }
    $args = @{
      DisplayName = "coLinux inbound port $p"
      Direction   = 'Inbound'
      Action      = 'Allow'
      Enabled     = 'True'
      Profile     = $Profiles
      Protocol    = 'TCP'
      LocalPort   = $p
    }
    if ($AllowFrom) { $args['RemoteAddress'] = $AllowFrom }
    try { New-NetFirewallRule @args | Out-Null } catch {}
  }
}

if ($Public) { $Profiles = 'Private,Domain,Public' }
if ($OpenWebPorts.IsPresent) { Add-CoLinuxFirewallRules -Ports @('80','443','8080') -Profiles $Profiles -AllowFrom $AllowFrom }

# Persist helper command
try {
  if (-not (Test-Path (Split-Path -Parent $PROFILE))) { New-Item -ItemType Directory -Path (Split-Path -Parent $PROFILE) -Force | Out-Null }
  if (-not (Test-Path $PROFILE)) { New-Item -ItemType File -Path $PROFILE -Force | Out-Null }
  $alias = @"
function ex3cutableLinux { param([Parameter(ValueFromRemainingArguments=`$true)] [string[]] `$Args) & wsl.exe -d `$env:COLINUX_DISTRO @Args }
`$env:COLINUX_DISTRO = '$Distro'
"@
  $raw = Get-Content -Path $PROFILE -Raw
  if ($raw -notmatch 'ex3cutableLinux') { Add-Content -Path $PROFILE -Value "`n# coLinux helper`n$alias" }
  Write-Host "Added PowerShell command: ex3cutableLinux (restart shell to load)"
} catch {}

Write-Host "Ensuring distro installed: $Distro"
try {
  $installed = (& wsl.exe --list --quiet) -split "\r?\n" | Where-Object { $_ }
  if ($installed -notcontains $Distro) { & wsl.exe --install -d $Distro }
} catch {}

Write-Host "Launching Linux environment: $Distro"
try { & wsl.exe -d $Distro } catch {}

Write-Host "Done. Run 'ex3cutableLinux' any time to start it."

