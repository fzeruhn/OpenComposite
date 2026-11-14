; OpenLamina-Setup.exe: installs OpenLamina for the current user, without an administrator, in
; %LOCALAPPDATA%\Programs\OpenLamina, which is then the OpenLamina folder openvrpaths.vrpath names:
;
;   bin\vrclient_x64.dll, bin\vrclient.dll, bin\version.txt   the runtime (64- and 32-bit games)
;   OpenLamina.exe, Newtonsoft.Json.dll                     the app: the switch, per-app settings, updates
;   SOURCE.txt, LICENSE*.txt                                   the release and its licences
;
; Built by scripts/ci/build-installer.ps1, which passes:
;   /DAppVersion=2026.11.06 /DNumericVersion=2026.11.6.0 /DFolder=<the OpenLamina folder, as in the zip>
;   /DSwitcher=<RuntimeSwitcher's build output> /DOutput=<folder for OpenLamina-Setup.exe>
;
; Command line, besides Inno Setup's own (/SILENT, /VERYSILENT, /SUPPRESSMSGBOXES, ...):
;   /SWITCH         with /SILENT or /VERYSILENT: OpenVR games use OpenLamina afterwards; if that fails, Setup
;                   exits with code 100. Interactively, the last page's checkbox (on) does it.
;   /OPENSWITCHER   with /SILENT or /VERYSILENT: the app opens afterwards (its update button uses this).
; The folder is always the same, so other programs (Mirador) can rely on it, and SOURCE.txt's first line names the release.
; The uninstaller takes OpenLamina out of openvrpaths.vrpath (SteamVR first again if OpenLamina was first) before it
; removes anything, and refuses while a game uses OpenLamina.

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef NumericVersion
  #define NumericVersion "0.0.0.0"
#endif
#ifndef Folder
  #error Pass /DFolder=<the OpenLamina folder> (scripts/ci/build-installer.ps1 does)
#endif
#ifndef Switcher
  #error Pass /DSwitcher=<RuntimeSwitcher's build output> (scripts/ci/build-installer.ps1 does)
#endif
#ifndef Output
  #define Output "."
#endif
; The commit's time (UTC), given to every installed file, so that the same commit builds the same setup.
#ifndef TouchDate
  #define TouchDate "current"
#endif
#ifndef TouchTime
  #define TouchTime "current"
#endif

#define Repository "https://github.com/fzeruhn/OpenLamina"

[Setup]
; Its own AppId: an OpenComposite Setup (the name before OpenLamina) is another app, and stays until it's uninstalled
AppId={{B7C4E1A2-6D3F-4B8E-9A15-3E0F2C7D4A61}
AppName=OpenLamina
AppVersion={#AppVersion}
AppVerName=OpenLamina {#AppVersion}
AppPublisher=fzeruhn (a fork of OpenComposite)
AppPublisherURL={#Repository}
AppSupportURL={#Repository}/issues
AppUpdatesURL={#Repository}/releases
VersionInfoVersion={#NumericVersion}
VersionInfoProductTextVersion={#AppVersion}
VersionInfoDescription=OpenLamina Setup
; Per user: no administrator, and %LOCALAPPDATA%\Programs\OpenLamina, always.
PrivilegesRequired=lowest
DefaultDirName={userpf}\OpenLamina
DisableDirPage=yes
UsePreviousAppDir=no
DisableProgramGroupPage=yes
DefaultGroupName=OpenLamina
MinVersion=10.0
OutputDir={#Output}
OutputBaseFilename=OpenLamina-Setup
SetupIconFile=..\RuntimeSwitcher\icon.ico
UninstallDisplayIcon={app}\OpenLamina.exe
UninstallDisplayName=OpenLamina
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; A game holding the DLLs: Setup offers to close it (and the app, during an update).
CloseApplications=yes
RestartApplications=no
TimeStampsInUTC=yes
TouchDate={#TouchDate}
TouchTime={#TouchTime}

[Messages]
FinishedLabel=OpenLamina is installed. Its app (OpenLamina, in the Start menu) switches OpenVR games between OpenLamina and SteamVR.

[Files]
Source: "{#Folder}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs touch
Source: "{#Switcher}\OpenLamina.exe"; DestDir: "{app}"; Flags: ignoreversion touch
Source: "{#Switcher}\OpenLamina.exe.config"; DestDir: "{app}"; Flags: ignoreversion touch
Source: "{#Switcher}\Newtonsoft.Json.dll"; DestDir: "{app}"; Flags: ignoreversion touch
Source: "{#Switcher}\LICENSE_Newtonsoft.Json.txt"; DestDir: "{app}"; Flags: ignoreversion touch

[Icons]
Name: "{autoprograms}\OpenLamina"; Filename: "{app}\OpenLamina.exe"; Comment: "Switch OpenVR games between OpenLamina and SteamVR"

[Run]
; Silent installs: only when asked. (/SWITCH: CurStepChanged.)
Filename: "{app}\OpenLamina.exe"; Flags: nowait; Check: SilentWith('/OPENSWITCHER')
; The last page's checkboxes.
Filename: "{app}\OpenLamina.exe"; Parameters: "--use-openlamina"; Description: "Use OpenLamina for OpenVR games now"; Flags: postinstall skipifsilent
Filename: "{app}\OpenLamina.exe"; Description: "Open OpenLamina"; Flags: postinstall skipifsilent nowait unchecked

[UninstallDelete]
; What OpenLamina and its app wrote there: applist.json, apps-config.json, openlamina.ini, updates.json.
Type: filesandordirs; Name: "{app}"

[Code]
function HasParam(Name: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), Name) = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

function SilentWith(Name: String): Boolean;
begin
  Result := WizardSilent and HasParam(Name);
end;

// Windows 10 1903 and later, and 11, come with it.
function HasNetFramework48: Boolean;
var
  Release: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM, 'SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full', 'Release', Release)
    and (Release >= 528040);
end;

function InitializeSetup: Boolean;
begin
  Result := HasNetFramework48;
  if not Result then
    SuppressibleMsgBox('OpenLamina needs the .NET Framework 4.8, which Windows 10 (version 1903 and later) and ' +
      'Windows 11 come with. Install it from Microsoft, then run this again.', mbCriticalError, MB_OK, IDOK);
end;

function App: String;
begin
  Result := ExpandConstant('{app}\OpenLamina.exe');
end;

var
  SwitchFailed: Boolean;

// Silent installs with /SWITCH. A failure is logged and shown (unless /SUPPRESSMSGBOXES), and Setup's exit code is
// then 100 (GetCustomSetupExitCode), so a program that ran it can tell.
procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
begin
  if (CurStep = ssPostInstall) and SilentWith('/SWITCH') then
  begin
    if not Exec(App, '--use-openlamina --quiet', '', SW_SHOW, ewWaitUntilTerminated, Code) then
      Code := -1;
    Log(Format('OpenLamina.exe --use-openlamina: exit code %d', [Code]));
    if Code <> 0 then
    begin
      SwitchFailed := True;
      SuppressibleMsgBox('OpenLamina is installed, but OpenVR games couldn''t be switched to it. Open ' +
        'OpenLamina from the Start menu and click "Use OpenLamina".', mbError, MB_OK, IDOK);
    end;
  end;
end;

function GetCustomSetupExitCode: Integer;
begin
  if SwitchFailed then
    Result := 100
  else
    Result := 0;
end;

// Before anything is asked or removed. Exit code 3: a game uses OpenLamina, or openvrpaths.vrpath is read-only;
// the uninstall stops (the app says why, unless silent). Any other failure (a broken paths file, say): asked whether to
// uninstall anyway, or, silent, uninstalled.
function InitializeUninstall: Boolean;
var
  Code: Integer;
  Quiet: String;
begin
  Result := True;
  // OpenLamina.exe's window (Program.WindowMutexName) holds files the uninstaller removes. Not AppMutex in [Setup]:
  // that would also stop a silent install while the window is open, which CloseApplications handles.
  while CheckForMutexes('OpenLamina.RuntimeSwitcher') do
  begin
    if UninstallSilent or (MsgBox('Close the OpenLamina window, then click OK.', mbInformation, MB_OKCANCEL) <> IDOK) then
    begin
      Result := False;
      Exit;
    end;
  end;
  if not FileExists(App) then
    Exit;
  if UninstallSilent then
    Quiet := ' --quiet'
  else
    Quiet := '';
  if not Exec(App, '--can-remove' + Quiet, '', SW_SHOW, ewWaitUntilTerminated, Code) then
    Code := -1;
  Log(Format('OpenLamina.exe --can-remove: exit code %d', [Code]));
  if Code = 3 then
    Result := False
  else if (Code <> 0) and not UninstallSilent then
    Result := MsgBox('OpenLamina may not be able to take itself out of openvrpaths.vrpath. Uninstall anyway? ' +
      'If OpenVR games don''t start VR after this, start SteamVR from Steam once.', mbConfirmation, MB_YESNO) = IDYES;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Code: Integer;
begin
  if (CurUninstallStep = usUninstall) and FileExists(App) then
    if not Exec(App, '--remove --quiet', '', SW_SHOW, ewWaitUntilTerminated, Code) or (Code <> 0) then
      if not UninstallSilent then
        MsgBox('OpenLamina could not take itself out of openvrpaths.vrpath. If OpenVR games don''t start VR ' +
          'after this, start SteamVR from Steam once.', mbError, MB_OK);
end;
