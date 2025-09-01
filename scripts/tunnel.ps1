param(
  [ValidateSet('cloudflare','tailscale')]
  [string]$Provider = 'cloudflare',
  [int]$Port = 8080
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host "This helper exposes a local service for testing. Use only on networks and with data you are authorized to access."

switch ($Provider) {
  'cloudflare' {
    if (-not (Get-Command cloudflared -ErrorAction SilentlyContinue)) {
      Write-Warning "'cloudflared' not found. Install from https://developers.cloudflare.com/cloudflare-one/connections/connect-apps/install-and-setup/installation/"
      return
    }
    Write-Host "Launching ephemeral Cloudflare Quick Tunnel to http://localhost:$Port ..."
    & cloudflared tunnel --url "http://localhost:$Port"
  }
  'tailscale' {
    if (-not (Get-Command tailscale -ErrorAction SilentlyContinue)) {
      Write-Warning "'tailscale' not found. Install from https://tailscale.com/download and login with 'tailscale up'"
      return
    }
    Write-Host "Enabling Tailscale Funnel (requires admin on your tailnet) for http://localhost:$Port ..."
    & tailscale funnel --bg "tcp:$Port"
  }
}

