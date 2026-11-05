#define MyAppName "Multimeter VST3"
#define MyAppVersion "1.0.0"
[Setup]
AppId={{D2E1A9C8-0A7B-4C7C-8F26-31D1F3D1A001}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=Axtlon Audio
DefaultDirName={autopf}\Common Files\VST3
DisableProgramGroupPage=yes
OutputDir=dist
OutputBaseFilename=Multimeter-Setup-x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
Uninstallable=yes
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
[Files]
Source: "package\VST3\Multimeter.vst3\*"; DestDir: "{app}\Multimeter.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
[Icons]
Name: "{group}\Multimeter VST3"; Filename: "{app}\Multimeter.vst3"
