param(
  [string]$Version = "rootfs-2025-08-31",
  [string]$AssetBase = "kali-rootfs-rolling-2025-08-31.img.zst",
  [string]$OutDir = "C:\\KaliSync",
  [string]$OwnerRepo = "3xecutablefile/test"
)

$ErrorActionPreference = 'Stop'

function Write-Info($msg)  { Write-Host "[INFO] $msg" -ForegroundColor Cyan }
function Write-Warn($msg)  { Write-Host "[WARN] $msg" -ForegroundColor Yellow }
function Write-ErrorLine($msg) { Write-Host "[ERROR] $msg" -ForegroundColor Red }

Write-Info "Owner/Repo: $OwnerRepo"
Write-Info "Release tag: $Version"
Write-Info "Asset base: $AssetBase"
Write-Info "Output dir: $OutDir"

$UriBase = "https://github.com/$OwnerRepo/releases/download/$Version"
$imgZst = Join-Path $OutDir $AssetBase
$shaFile = "$imgZst.sha256"
$imgRaw = $imgZst -replace '\.zst$', ''

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Write-Info "Downloading image (.zst)"
Invoke-WebRequest -UseBasicParsing -Uri "$UriBase/$AssetBase" -OutFile $imgZst

Write-Info "Downloading SHA256 file"
Invoke-WebRequest -UseBasicParsing -Uri "$UriBase/$AssetBase.sha256" -OutFile $shaFile

Write-Info "Verifying SHA256"
if (-not (Test-Path $shaFile)) { throw "SHA256 file missing: $shaFile" }
$firstLine = (Get-Content $shaFile -TotalCount 1)
if (-not $firstLine) { throw "Empty SHA256 file: $shaFile" }
$expected = ($firstLine -split '\s+')[0].ToLower()
$actual = (Get-FileHash $imgZst -Algorithm SHA256).Hash.ToLower()
if ($expected -ne $actual) {
  Write-ErrorLine "SHA256 mismatch. Expected $expected, got $actual"
  throw "Checksum verification failed"
}
Write-Info "SHA256 OK: $actual"

Write-Info "Decompressing with zstd"
$zstd = Join-Path $PSScriptRoot "bin\zstd.exe"
if (-not (Test-Path $zstd)) {
  Write-Warn "zstd.exe not found at $zstd"
  Write-Warn "Install zstd (e.g., winget install -e --id Facebook.Zstandard) and ensure zstd.exe is on PATH, or place it at scripts\\bin\\zstd.exe"
  $zstd = "zstd.exe"
}

& $zstd -d -f $imgZst -o $imgRaw
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $imgRaw)) {
  throw "Decompression failed. Ensure zstd is installed."
}

Write-Host "Rootfs ready at $imgRaw" -ForegroundColor Green

