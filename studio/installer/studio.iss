; Studio (Laurence Who Studio) installer
; Builds "Studio Setup.exe" (+ data files next to it) from the assembled Studio folder.
#ifndef SrcDir
  #define SrcDir "..\..\Studio"
#endif
#ifndef AppVersion
  #define AppVersion "0.1"
#endif

[Setup]
AppId={{6B0C3F7A-5D2E-4C9B-9A41-2F6E0D7C1B59}
AppName=Studio
AppVersion={#AppVersion}
AppVerName=Studio {#AppVersion}
AppPublisher=Laurence Who
AppPublisherURL=https://laurencewho.com
DefaultDirName={autopf}\Studio
DefaultGroupName=Studio
DisableProgramGroupPage=yes
OutputBaseFilename=Studio Setup
OutputDir=output
SetupIconFile=..\icons\studio.ico
UninstallDisplayIcon={app}\Studio.exe
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
; Blender makes it big: compress harder to stay under 4 GB
Compression=lzma2/normal
LZMANumBlockThreads=4
SolidCompression=no
; one single Setup.exe (Inno Setup 6.5.2+ allows up to ~4 GB; compressed it is ~3.2 GB with Mail)
DiskSpanning=no
ChangesAssociations=yes
PrivilegesRequiredOverridesAllowed=dialog

[Tasks]
Name: "desktopicon"; Description: "Put Studio on the desktop"; GroupDescription: "Shortcuts:"
Name: "assoc"; Description: "Open scores (.mscz) and Tape projects (.aup4) with Studio"; GroupDescription: "Files:"

[InstallDelete]
; Mail is a whole Thunderbird: replace it, so files of an older version don't linger
; (accounts and mail live in the profile under %APPDATA%\LaurenceWho\Studio\MailProfile)
Type: filesandordirs; Name: "{app}\Mail"
; Live is a whole OBS: replace its program files (its settings in Live\config are kept)
Type: filesandordirs; Name: "{app}\Live\bin"
Type: filesandordirs; Name: "{app}\Live\data"
Type: filesandordirs; Name: "{app}\Live\obs-plugins"

[Files]
; everything: Score, Tape, Mail (Thunderbird + Studio's profile files), Sounds, launchers
Source: "{#SrcDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Studio"; Filename: "{app}\Studio.exe"
Name: "{autoprograms}\Studio\Score"; Filename: "{app}\Score.exe"
Name: "{autoprograms}\Studio\Tape"; Filename: "{app}\Tape.exe"
Name: "{autoprograms}\Studio\Mail"; Filename: "{app}\Mail.exe"
Name: "{autoprograms}\Studio\Sketch"; Filename: "{app}\Sketch.exe"
Name: "{autoprograms}\Studio\Live"; Filename: "{app}\Live.exe"
Name: "{autodesktop}\Studio"; Filename: "{app}\Studio.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.mscz\OpenWithProgids"; ValueType: string; ValueName: "LaurenceWho.Studio.Score"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\LaurenceWho.Studio.Score"; ValueType: string; ValueName: ""; ValueData: "Score"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\LaurenceWho.Studio.Score\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Score.exe,0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\LaurenceWho.Studio.Score\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Studio.exe"" ""%1"""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.aup4\OpenWithProgids"; ValueType: string; ValueName: "LaurenceWho.Studio.Tape"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\LaurenceWho.Studio.Tape"; ValueType: string; ValueName: ""; ValueData: "Tape project"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\LaurenceWho.Studio.Tape\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Tape.exe,0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\LaurenceWho.Studio.Tape\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Studio.exe"" ""%1"""; Tasks: assoc

[Run]
Filename: "{app}\Studio.exe"; Description: "Open Studio"; Flags: nowait postinstall skipifsilent
