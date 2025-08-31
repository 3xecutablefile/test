Write-Host "Stopping service coLinux2 (if running)..."
sc.exe stop coLinux2 | Out-Null
Start-Sleep -s 1
Write-Host "Uninstalling service coLinux2..."
daemon\target\release\colinux-daemon.exe --uninstall

