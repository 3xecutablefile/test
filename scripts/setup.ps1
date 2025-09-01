param(
  [string]$Distro = "kali-linux",
  [string]$NetworkingMode = "mirrored",
  [string[]]$OpenPorts = @(),          # secure default: no inbound ports opened
  [string]$Profiles = "Private,Domain", # firewall profile scope
  [string]$AllowFrom = "",            # optional CIDR/CIDRs to restrict remote sources
  [switch]$OpenWebPorts,               # convenience: open 80,443,8080
  [switch]$Public,                     # convenience: include Public profile
  [string]$Memory = "",               # e.g., 8GB (optional)
  [string]$Processors = "",           # e.g., 4 (optional)
  [string]$RepoUrl = "",              # Optional: clone this repo first
  [string]$InstallDir = "$env:USERPROFILE\\colinux2" # Where to clone if RepoUrl set
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

if ($RepoUrl -and $RepoUrl.Trim() -ne '') {
  Write-Host "Cloning repository from $RepoUrl to $InstallDir ..."
  if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Error "Git not found on PATH. Please install Git for Windows and retry."
    exit 1
  }
  if (-not (Test-Path $InstallDir)) { New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null }
  if (-not (Test-Path (Join-Path $InstallDir '.git'))) {
    git clone $RepoUrl $InstallDir
  } else {
    Write-Host "Repository already present in $InstallDir; skipping clone."
  }
}

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

# Inbound firewall rules (secure defaults)
function Add-CoLinuxFirewallRules {
  param([string[]]$Ports, [string]$Profiles, [string]$AllowFrom)
  if ($Ports.Count -eq 0) { return }
  $displayPrefix = "coLinux2 inbound"
  foreach ($p in $Ports) {
    if ($p -notmatch '^[0-9]+$') { continue }
    $display = "$displayPrefix port $p"
    $args = @{
      DisplayName = $display
      Direction   = 'Inbound'
      Action      = 'Allow'
      Enabled     = 'True'
      Profile     = $Profiles
      Protocol    = 'TCP'
      LocalPort   = $p
    }
    if ($AllowFrom -and $AllowFrom.Trim() -ne '') { $args['RemoteAddress'] = $AllowFrom }
    try {
      New-NetFirewallRule @args | Out-Null
    } catch {
      Write-Warning "Firewall rule failed for port $p: $($_.Exception.Message)"
    }
  }
  Write-Host "Added Windows Firewall rules (profiles: $Profiles) for ports: $($Ports -join ', ')"
}

function Remove-CoLinuxFirewallRules {
  $rules = Get-NetFirewallRule -DisplayName 'coLinux2 inbound*' -ErrorAction SilentlyContinue
  if ($rules) { $rules | Remove-NetFirewallRule }
}

$effectivePorts = @()
if ($OpenWebPorts) { $effectivePorts = @('80','443','8080') }
if ($OpenPorts.Count -gt 0) { $effectivePorts = $OpenPorts }
if ($Public) { $Profiles = 'Private,Domain,Public' }
Add-CoLinuxFirewallRules -Ports $effectivePorts -Profiles $Profiles -AllowFrom $AllowFrom

Write-Host "Creating PowerShell command shortcut: ex3cutableLinux"
try {
  $profilePath = $PROFILE
  $profileDir = Split-Path -Parent $profilePath
  if (-not (Test-Path $profileDir)) { New-Item -ItemType Directory -Path $profileDir -Force | Out-Null }
  if (-not (Test-Path $profilePath)) { New-Item -ItemType File -Path $profilePath -Force | Out-Null }
  $aliasBlock = @"
function ex3cutableLinux {
  param([Parameter(ValueFromRemainingArguments=
    `$true)] [string[]] `$Args)
  & wsl.exe -d ""$Distro"" @Args
}
"@
  $existing = Get-Content -Path $profilePath -Raw
  if ($existing -notmatch 'function\s+ex3cutableLinux') {
    Add-Content -Path $profilePath -Value "`n# coLinux alias`n$aliasBlock"
  }
  Write-Host "Added alias to $profilePath. Restart PowerShell to use it."
} catch {
  Write-Warning "Could not create PowerShell alias: $($_.Exception.Message)"
}

Write-Host "Starting the Linux environment: $Distro (first run will prompt for user setup)."
try { & wsl.exe -d $Distro } catch {}

Write-Host "Setup complete. Next time, run 'ex3cutableLinux' in PowerShell to launch."
