# Signs the Windows binaries (CI only runs this when the secrets exist).
# Usage: tools/sign/windows.ps1 <build-dir>
# Env: WINDOWS_CERTIFICATE (base64 .pfx), WINDOWS_CERTIFICATE_PASSWORD
param([string]$Build = "build")
$ErrorActionPreference = "Stop"
$pfx = Join-Path $env:RUNNER_TEMP "cert.pfx"
[IO.File]::WriteAllBytes($pfx, [Convert]::FromBase64String($env:WINDOWS_CERTIFICATE))
$signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" | Select-Object -Last 1
Get-ChildItem -Recurse -Path $Build -Include VDX.exe,VDX.vst3 -File | Where-Object { $_.FullName -match "VDX_artefacts" } | ForEach-Object {
  & $signtool.FullName sign /f $pfx /p $env:WINDOWS_CERTIFICATE_PASSWORD /tr http://timestamp.digicert.com /td sha256 /fd sha256 $_.FullName
}
