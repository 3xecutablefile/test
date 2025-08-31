param([string]$Bin="daemon\target\release\colinux-daemon.exe")
Write-Host "Installing service coLinux2..."
& $Bin --install
sc.exe start coLinux2

