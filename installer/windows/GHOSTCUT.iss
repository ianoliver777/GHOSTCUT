; GHOSTCUT Windows installer (Inno Setup 6)
;   iscc /DAppVersion=0.9.0 /DBuildDir=..\..\build /DNotices=..\..\dist\THIRD_PARTY_NOTICES.txt /DOutDir=..\..\dist GHOSTCUT.iss
; Optional code signing: configure a SignTool in Inno Setup (see docs/SIGNING.md) and add
;   SignTool=ghostcutsign
; to [Setup].

#ifndef AppVersion
  #define AppVersion "0.9.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build"
#endif
#ifndef Notices
  #define Notices "..\..\dist\THIRD_PARTY_NOTICES.txt"
#endif
#ifndef OutDir
  #define OutDir "..\..\dist"
#endif
#define Art BuildDir + "\GHOSTCUT_artefacts\Release"

[Setup]
AppId={{7F3A7E2C-777A-4C1E-9B6D-6A0D7E57C0DE}
AppName=GHOSTCUT
AppVersion={#AppVersion}
AppPublisher=TRIPLE SEVEN AUDIO
AppPublisherURL=https://triplesevenaudio.com
DefaultDirName={autopf}\TRIPLE SEVEN AUDIO\GHOSTCUT
DefaultGroupName=TRIPLE SEVEN AUDIO
DisableProgramGroupPage=yes
LicenseFile={#Notices}
OutputDir={#OutDir}
OutputBaseFilename=GHOSTCUT-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=GHOSTCUT (TRIPLE SEVEN AUDIO)

[Types]
Name: "full"; Description: "VST3 plug-in + standalone app"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (C:\Program Files\Common Files\VST3)"; Types: full custom
Name: "app";  Description: "Standalone app"; Types: full

[Files]
; VST3 bundle: the plug-in DLL and onnxruntime.dll live together in Contents\x86_64-win
Source: "{#Art}\VST3\GHOSTCUT.vst3\*"; DestDir: "{commoncf64}\VST3\GHOSTCUT.vst3"; Components: vst3; Flags: recursesubdirs ignoreversion
Source: "{#Art}\Standalone\GHOSTCUT.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#Art}\Standalone\onnxruntime.dll"; DestDir: "{app}"; Components: app; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#Notices}"; DestDir: "{app}"; DestName: "THIRD_PARTY_NOTICES.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\GHOSTCUT"; Filename: "{app}\GHOSTCUT.exe"; Components: app

[Run]
Filename: "{app}\GHOSTCUT.exe"; Description: "Launch GHOSTCUT"; Flags: nowait postinstall skipifsilent; Components: app
