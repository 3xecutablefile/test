New-SelfSignedCertificate -Type CodeSigning -Subject "CN=INFOTRON Test" -CertStoreLocation Cert:\LocalMachine\My | Out-Null
bcdedit /set testsigning on
Write-Host "Reboot to enable test signing."

