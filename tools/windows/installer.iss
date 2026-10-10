; One-click installer for Stabewwy (Inno Setup 6). Build it with tools\windows\make-installer.bat after build.bat.
; Installs per user (no administrator rights needed) to %LOCALAPPDATA%\Programs\Stabewwy, adds Start menu and
; desktop shortcuts and an uninstaller, and starts the player when done.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\build\installer"
#endif

#define AppName "Stabewwy"
#define AppExe "strawberry.exe"

[Setup]
AppId={{5F0AC566-A0E4-4CD1-9A7C-78A20B5D5DCB}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=saikanov
AppPublisherURL=https://github.com/saikanov/stabewwy
AppSupportURL=https://github.com/saikanov/stabewwy/issues
AppComments=Music player based on Strawberry Music Player
DefaultDirName={localappdata}\Programs\{#AppName}
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; One click: no questions, just the progress bar.
DisableWelcomePage=yes
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableFinishedPage=yes
CloseApplications=yes
RestartApplications=no
SetupIconFile=..\..\dist\windows\strawberry.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression=lzma2/ultra64
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename={#AppName}-{#AppVersion}-Setup-x64

[Files]
Source: "{#BuildDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\gio-modules\*"; DestDir: "{app}\gio-modules"; Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\sqldrivers\*"; DestDir: "{app}\sqldrivers"; Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\gstreamer-plugins\*"; DestDir: "{app}\gstreamer-plugins"; Flags: ignoreversion recursesubdirs
; Visual C++ runtime next to the exe, so it also runs on PCs without the Visual C++ Redistributable.
#ifdef CrtDir
Source: "{#CrtDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
#endif
Source: "..\..\COPYING"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{userprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"

[Run]
; Start the player right after installing (skipped for silent installs).
Filename: "{app}\{#AppExe}"; Flags: nowait skipifsilent
