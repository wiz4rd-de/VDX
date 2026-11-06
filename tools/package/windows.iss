; Inno Setup script for the Windows installer (D-PL17). Run through
; tools/package/windows.ps1, which passes Version, Artefacts and OutDir.
; Installs the standalone app into Program Files\VDX and the VST3 plugin into
; Common Files\VST3. AppId must never change: it ties upgrades and the
; uninstaller to earlier installs.

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Artefacts
  #error Pass /DArtefacts=<VDX_artefacts\Release>
#endif
#ifndef OutDir
  #define OutDir "."
#endif

[Setup]
AppId={{C6B42F40-24C6-4DDA-88DE-0824755E0250}
AppName=VDX
AppVersion={#Version}
AppVerName=VDX {#Version}
AppPublisher=Marco Funk
AppPublisherURL=https://github.com/wiz4rd-de/VDX
AppSupportURL=https://github.com/wiz4rd-de/VDX/issues
DefaultDirName={autopf}\VDX
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19045
OutputDir={#OutDir}
OutputBaseFilename=VDX-{#Version}-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\VDX.exe
UninstallDisplayName=VDX

[Types]
Name: "full"; Description: "Standalone app and VST3 plugin"
Name: "custom"; Description: "Choose components"; Flags: iscustom

[Components]
Name: "app"; Description: "VDX standalone app"; Types: full custom
Name: "vst3"; Description: "VST3 plugin (Common Files\VST3)"; Types: full custom

[Files]
Source: "{#Artefacts}\Standalone\VDX.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#Artefacts}\Standalone\NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Artefacts}\VST3\VDX.vst3\*"; DestDir: "{commoncf64}\VST3\VDX.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\VDX"; Filename: "{app}\VDX.exe"; Components: app

[Run]
Filename: "{app}\VDX.exe"; Description: "Start VDX"; Flags: nowait postinstall skipifsilent; Components: app
