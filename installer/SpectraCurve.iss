; SpectraCurve EQ + Free - Windows installer (Inno Setup)
; Built by GitHub Actions on windows-latest

#ifndef MyAppVersion
  #define MyAppVersion "1.1.0"
#endif

[Setup]
AppId={{A1B2C3D4-EQFE-4A5B-9C0D-FEARESQ110}}
AppName=SpectraCurve EQ + Free
AppVersion={#MyAppVersion}
AppVerName=SpectraCurve {#MyAppVersion} by Fear Escape
AppPublisher=Fear Escape
DefaultDirName={autopf}\Fear Escape\SpectraCurve
DefaultGroupName=Fear Escape\SpectraCurve
OutputDir=..\dist-win
OutputBaseFilename=SpectraCurve-{#MyAppVersion}-Windows-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ShowLanguageDialog=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; VST3 bundles -> standard system VST3 folder
Source: "..\build\SmartEQ_artefacts\Release\VST3\SmartEQ.vst3\*"; DestDir: "{commoncf64}\VST3\SmartEQ.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion; Check: FileExists(ExpandConstant('{commoncf64}\VST3')) or True
Source: "..\build\SmartEQFree_artefacts\Release\VST3\SmartEQFree.vst3\*"; DestDir: "{commoncf64}\VST3\SmartEQFree.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
; Standalone apps -> program folder
Source: "..\build\SmartEQ_artefacts\Release\Standalone\SmartEQ.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\SmartEQFree_artefacts\Release\Standalone\SmartEQFree.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\SpectraCurve EQ (Standalone)"; Filename: "{app}\SmartEQ.exe"
Name: "{group}\SpectraCurve Free (Standalone)"; Filename: "{app}\SmartEQFree.exe"
Name: "{group}\Uninstall SpectraCurve"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\SmartEQ.exe"; Description: "Launch SpectraCurve EQ standalone"; Flags: nowait postinstall skipifsilent unchecked
