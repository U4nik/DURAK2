[Setup]
AppName=Дурак
AppVersion=1.0
AppPublisher=Мой проект
DefaultDirName={autopf}\Durak2
DefaultGroupName=Дурак
UninstallDisplayIcon={app}\durak.exe
OutputBaseFilename=Durak2_Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
CloseApplications=force

[Files]
Source: "durak.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "openal32.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "sfml-graphics-2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "sfml-window-2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "sfml-system-2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "sfml-audio-2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "libstdc++-6.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "libgcc_s_seh-1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "libwinpthread-1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "Symbola-AjYx.ttf"; DestDir: "{app}"; Flags: ignoreversion
Source: "layout_game.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "stat.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist
Source: "cards\*"; DestDir: "{app}\cards"; Flags: ignoreversion
Source: "buttons\*"; DestDir: "{app}\buttons"; Flags: ignoreversion
Source: "sound\*"; DestDir: "{app}\sound"; Flags: ignoreversion
Source: "emotion\*"; DestDir: "{app}\emotion"; Flags: ignoreversion

[Icons]
Name: "{autodesktop}\Дурак"; Filename: "{app}\durak.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Создать значок на рабочем столе"; GroupDescription: "Дополнительные значки:"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Run]
Filename: "{app}\durak.exe"; Description: "Запустить Дурак"; Flags: nowait postinstall skipifsilent
