param(
  [ValidateSet('cloudflare','tailscale')]
  [string]$Provider = 'cloudflare',
  [int]$Port = 8080
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host "This exposes a local service for testing. Use only where authorized."

switch ($Provider) {
  'cloudflare' {
    if (-not (Get-Command cloudflared -ErrorAction SilentlyContinue)) {
      Write-Warning "Install 'cloudflared' first: https://developers.cloudflare.com/cloudflare-one/connections/connect-apps/install-and-setup/installation/"
      return
    }
    Write-Host "Starting Cloudflare Quick Tunnel to http://localhost:$Port ..."
    & cloudflared tunnel --url "http://localhost:$Port"
  }
  'tailscale' {
    if (-not (Get-Command tailscale -ErrorAction SilentlyContinue)) {
      Write-Warning "Install 'tailscale' first: https://tailscale.com/download and run 'tailscale up'"
      return
    }
    Write-Host "Enabling Tailscale Funnel for tcp:$Port ..."
    & tailscale funnel --bg "tcp:$Port"
  }
}

