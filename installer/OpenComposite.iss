; OpenComposite-Setup.exe: installs OpenComposite for the current user, without an administrator, in
; %LOCALAPPDATA%\Programs\OpenComposite, which is then the OpenComposite folder openvrpaths.vrpath names:
;
;   bin\vrclient_x64.dll, bin\vrclient.dll, bin\version.txt   the runtime (64- and 32-bit games)
;   OpenComposite.exe, Newtonsoft.Json.dll                     the app: the switch, per-app settings, updates
;   SOURCE.txt, LICENSE*.txt                                   the release and its licences
;
; Built by scripts/ci/build-installer.ps1, which passes:
;   /DAppVersion=2026.11.06 /DNumericVersion=2026.11.6.0 /DFolder=<the OpenComposite folder, as in the zip>
;   /DSwitcher=<RuntimeSwitcher's build output> /DOutput=<folder for OpenComposite-Setup.exe>
;
; Command line, besides Inno Setup's own (/SILENT, /VERYSILENT, /SUPPRESSMSGBOXES, ...):
;   /SWITCH         with /SILENT or /VERYSILENT: OpenVR games use OpenComposite afterwards; if that fails, Setup
;                   exits with code 100. Interactively, the last page's checkbox (on) does it.
;   /OPENSWITCHER   with /SILENT or /VERYSILENT: the app opens afterwards (its update button uses this).
; The folder is always the same, so other programs (Mirador) can rely on it, and SOURCE.txt's first line names the release.
; The uninstaller takes OpenComposite out of openvrpaths.vrpath (SteamVR first again if OpenComposite was first) before it
; removes anything, and refuses while a game uses OpenComposite.

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef NumericVersion
  #define NumericVersion "0.0.0.0"
#endif
#ifndef Folder
  #error Pass /DFolder=<the OpenComposite folder> (scripts/ci/build-installer.ps1 does)
#endif
#ifndef Switcher
  #error Pass /DSwitcher=<RuntimeSwitcher's build output> (scripts/ci/build-installer.ps1 does)
#endif
#ifndef Output
  #define Output "."
#endif

#define Repository "https://github.com/fzeruhn/OpenComposite"

[Setup]
AppId={{2D229E89-50A5-4553-B9BE-604A572138F4}
AppName=OpenComposite
AppVersion={#AppVersion}
AppVerName=OpenComposite {#AppVersion}
AppPublisher=OpenComposite (fzeruhn's fork)
AppPublisherURL={#Repository}
AppSupportURL={#Repository}/issues
AppUpdatesURL={#Repository}/releases
VersionInfoVersion={#NumericVersion}
VersionInfoProductTextVersion={#AppVersion}
VersionInfoDescription=OpenComposite Setup
; Per user: no administrator, and %LOCALAPPDATA%\Programs\OpenComposite, always.
PrivilegesRequired=lowest
DefaultDirName={userpf}\OpenComposite
DisableDirPage=yes
UsePreviousAppDir=no
DisableProgramGroupPage=yes
DefaultGroupName=OpenComposite
MinVersion=10.0
OutputDir={#Output}
OutputBaseFilename=OpenComposite-Setup
SetupIconFile=..\RuntimeSwitcher\icon.ico
UninstallDisplayIcon={app}\OpenComposite.exe
UninstallDisplayName=OpenComposite
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; A game holding the DLLs: Setup offers to close it (and the app, during an update).
CloseApplications=yes
RestartApplications=no
TimeStampsInUTC=yes

[Messages]
FinishedLabel=OpenComposite is installed. Its app (OpenComposite, in the Start menu) switches OpenVR games between OpenComposite and SteamVR.

[Files]
Source: "{#Folder}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Switcher}\OpenComposite.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Switcher}\OpenComposite.exe.config"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Switcher}\Newtonsoft.Json.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Switcher}\LICENSE_Newtonsoft.Json.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\OpenComposite"; Filename: "{app}\OpenComposite.exe"; Comment: "Switch OpenVR games between OpenComposite and SteamVR"

[Run]
; Silent installs: only when asked. (/SWITCH: CurStepChanged.)
Filename: "{app}\OpenComposite.exe"; Flags: nowait; Check: SilentWith('/OPENSWITCHER')
; The last page's checkboxes.
Filename: "{app}\OpenComposite.exe"; Parameters: "--use-opencomposite"; Description: "Use OpenComposite for OpenVR games now"; Flags: postinstall skipifsilent
Filename: "{app}\OpenComposite.exe"; Description: "Open OpenComposite"; Flags: postinstall skipifsilent nowait unchecked

[UninstallDelete]
; What OpenComposite and its app wrote there: applist.json, apps-config.json, opencomposite.ini, updates.json.
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
    SuppressibleMsgBox('OpenComposite needs the .NET Framework 4.8, which Windows 10 (version 1903 and later) and ' +
      'Windows 11 come with. Install it from Microsoft, then run this again.', mbCriticalError, MB_OK, IDOK);
end;

function App: String;
begin
  Result := ExpandConstant('{app}\OpenComposite.exe');
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
    if not Exec(App, '--use-opencomposite --quiet', '', SW_SHOW, ewWaitUntilTerminated, Code) then
      Code := -1;
    Log(Format('OpenComposite.exe --use-opencomposite: exit code %d', [Code]));
    if Code <> 0 then
    begin
      SwitchFailed := True;
      SuppressibleMsgBox('OpenComposite is installed, but OpenVR games couldn''t be switched to it. Open ' +
        'OpenComposite from the Start menu and click "Use OpenComposite".', mbError, MB_OK, IDOK);
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

// Before anything is asked or removed. Exit code 3: a game uses OpenComposite, or openvrpaths.vrpath is read-only;
// the uninstall stops (the app says why, unless silent). Any other failure (a broken paths file, say): asked whether to
// uninstall anyway, or, silent, uninstalled.
function InitializeUninstall: Boolean;
var
  Code: Integer;
  Quiet: String;
begin
  Result := True;
  // OpenComposite.exe's window (Program.WindowMutexName) holds files the uninstaller removes. Not AppMutex in [Setup]:
  // that would also stop a silent install while the window is open, which CloseApplications handles.
  while CheckForMutexes('OpenComposite.RuntimeSwitcher') do
  begin
    if UninstallSilent or (MsgBox('Close the OpenComposite window, then click OK.', mbInformation, MB_OKCANCEL) <> IDOK) then
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
  Log(Format('OpenComposite.exe --can-remove: exit code %d', [Code]));
  if Code = 3 then
    Result := False
  else if (Code <> 0) and not UninstallSilent then
    Result := MsgBox('OpenComposite may not be able to take itself out of openvrpaths.vrpath. Uninstall anyway? ' +
      'If OpenVR games don''t start VR after this, start SteamVR from Steam once.', mbConfirmation, MB_YESNO) = IDYES;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Code: Integer;
begin
  if (CurUninstallStep = usUninstall) and FileExists(App) then
    if not Exec(App, '--remove --quiet', '', SW_SHOW, ewWaitUntilTerminated, Code) or (Code <> 0) then
      if not UninstallSilent then
        MsgBox('OpenComposite could not take itself out of openvrpaths.vrpath. If OpenVR games don''t start VR ' +
          'after this, start SteamVR from Steam once.', mbError, MB_OK);
end;
