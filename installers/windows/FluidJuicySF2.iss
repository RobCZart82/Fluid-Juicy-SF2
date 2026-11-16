; InputDir, OutputDir and ProductVersion are supplied by the packaging script.
[Setup]
AppId=RobCZart82.FluidJuicySF2.Preview
AppName=Fluid Juicy SF2
AppVersion={#ProductVersion}
AppPublisher=RobCZart82
DefaultDirName={commonpf64}\Fluid Juicy SF2
DefaultGroupName=Fluid Juicy SF2
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
OutputDir={#OutputDir}
OutputBaseFilename=Fluid-Juicy-SF2-windows-x64-preview-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\Fluid_Juicy_SF2.exe
CloseApplications=yes
RestartApplications=no

[Files]
Source: "{#InputDir}\Fluid_Juicy_SF2.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#InputDir}\Fluid_Juicy_SF2.vst3\*"; DestDir: "{commoncf64}\VST3\Fluid_Juicy_SF2.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#InputDir}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#InputDir}\build-record.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#InputDir}\binary-verification.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#InputDir}\Fluid-Juicy-SF2-Installation-*.pdf"; DestDir: "{app}\Documentation"; Flags: ignoreversion

[Icons]
Name: "{group}\Fluid Juicy SF2"; Filename: "{app}\Fluid_Juicy_SF2.exe"
Name: "{group}\Uninstall Fluid Juicy SF2"; Filename: "{uninstallexe}"
