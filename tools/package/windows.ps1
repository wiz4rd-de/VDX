# Packages the Windows build (D-PL17): VDX-<version>-Windows-x64-Setup.exe
# (Inno Setup 6) and VDX-<version>-Windows-x64.zip. The binaries are unsigned
# unless tools/sign/windows.ps1 ran first.
# Usage: tools/package/windows.ps1 <artefacts-dir> <version> <out-dir>
#   <artefacts-dir> is plugin\VDX_artefacts\Release in the build tree, after
#   tools/package/notices.py --install.
param(
  [Parameter(Mandatory)][string]$Artefacts,
  [Parameter(Mandatory)][string]$Version,
  [Parameter(Mandatory)][string]$Out
)
$ErrorActionPreference = "Stop"
$Artefacts = (Resolve-Path $Artefacts).Path
$exe = Join-Path $Artefacts "Standalone\VDX.exe"
$notices = Join-Path $Artefacts "Standalone\NOTICES.txt"
$vst3 = Join-Path $Artefacts "VST3\VDX.vst3"
foreach ($p in @($exe, $notices, $vst3, (Join-Path $vst3 "Contents\Resources\NOTICES.txt"))) {
  if (-not (Test-Path $p)) { throw "windows.ps1: missing $p (build, then run tools/package/notices.py --install)" }
}
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path $Out).Path

$iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") |
  Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "windows.ps1: Inno Setup 6 (ISCC.exe) not found" }
& $iscc "/DVersion=$Version" "/DArtefacts=$Artefacts" "/DOutDir=$Out" (Join-Path $PSScriptRoot "windows.iss")
if ($LASTEXITCODE -ne 0) { throw "windows.ps1: ISCC failed with exit code $LASTEXITCODE" }

$name = "VDX-$Version-Windows-x64"
$stage = Join-Path ([IO.Path]::GetTempPath()) $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item $exe, $notices -Destination $stage
Copy-Item -Recurse $vst3 -Destination $stage
@"
VDX $Version for Windows 10 (22H2) and 11, x64

$name-Setup.exe installs everything. To use this zip instead:
  Standalone app: run VDX.exe from any folder.
  VST3 plugin:    copy the VDX.vst3 folder into C:\Program Files\Common Files\VST3

The builds are not code-signed. Windows SmartScreen may warn the first time:
choose "More info", then "Run anyway".

The interface needs the Microsoft Edge WebView2 Runtime. Windows 11 includes it;
on Windows 10, install it from https://developer.microsoft.com/microsoft-edge/webview2/
if VDX says it is missing.

NOTICES.txt holds the licences of VDX and of the third-party code built into it.
https://github.com/wiz4rd-de/VDX
"@ | Set-Content -Path (Join-Path $stage "INSTALL.txt") -Encoding utf8
$zip = Join-Path $Out "$name.zip"
Compress-Archive -Path $stage -DestinationPath $zip -Force
Remove-Item -Recurse -Force $stage
Write-Host "windows.ps1: wrote $zip and $name-Setup.exe"
