if (Get-Service coLinux2 -ErrorAction SilentlyContinue) {
  sc.exe start coLinux2 | Out-Null
} else {
  $exe = "daemon\target\release\colinux-daemon.exe"
  Start-Process -FilePath $exe -ArgumentList "config\colinux.yaml" -Verb RunAs
}
