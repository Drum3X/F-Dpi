; F-Dpi installer (Inno Setup). Built by script\build_windows.ps1 -Installer from deployment\windows-amd64.
; F-Dpi always runs elevated (WinDivert, Windows DNS), so it installs for all users under Program Files.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppVersionMajor
  #define AppVersionMajor "0"
#endif
#ifndef AppVersionMinor
  #define AppVersionMinor "0"
#endif
#ifndef AppVersionPatch
  #define AppVersionPatch "0"
#endif
#ifndef AppVersionBuild
  #define AppVersionBuild "0"
#endif

[Setup]
AppId={{D128F597-EBDA-46B9-B257-599BCBE187C5}
AppName=F-Dpi
AppVersion={#AppVersion}
AppVerName=F-Dpi {#AppVersion}
AppPublisher=F-Dpi
VersionInfoVersion={#AppVersionMajor}.{#AppVersionMinor}.{#AppVersionPatch}.{#AppVersionBuild}
VersionInfoProductName=F-Dpi
VersionInfoDescription=F-Dpi Setup
VersionInfoCopyright=F-Dpi (GPL-3.0, based on Throne)
SourceDir=..
OutputDir=deployment
OutputBaseFilename=F-DpiSetup
SetupIconFile=res\fdpi\F-Dpi.ico
UninstallDisplayName=F-Dpi
UninstallDisplayIcon={app}\F-Dpi.exe
LicenseFile=LICENSE
WizardStyle=modern
PrivilegesRequired=admin
DefaultDirName={autopf}\F-Dpi
DirExistsWarning=no
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
; Not Restart Manager: it lists F-Dpi, winws and the core before ssInstall, where StopFDpi ends them (the core with F-Dpi),
; and then fails on the gone processes and aborts the update. StopFDpi and -cleanup do the whole job instead.
CloseApplications=no
; Uninstall removes the associations F-Dpi registers at runtime, so Explorer has to reload them.
ChangesAssociations=yes
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
LZMANumBlockThreads=4
LZMABlockSize=118784

[Languages]
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
turkish.SelectDirBrowseLabel=Devam etmek için İleri'ye tıklayın. Seçtiğiniz klasörün adı F-Dpi değilse içinde bir F-Dpi klasörü oluşturulur; kaldırma işlemi yalnızca F-Dpi'nin kendi klasörünü siler.
english.SelectDirBrowseLabel=To continue, click Next. If the folder you choose is not named F-Dpi, Setup creates an F-Dpi folder inside it, so uninstalling only ever removes F-Dpi's own folder.

[CustomMessages]
turkish.DeleteUserData=F-Dpi profilleriniz, ayarlarınız ve günlükleriniz de silinsin mi?%n%nF-Dpi'yi daha sonra yeniden kurup bunları korumak istiyorsanız Hayır'ı seçin.
english.DeleteUserData=Also delete your F-Dpi profiles, settings and logs?%n%nChoose No if you plan to reinstall F-Dpi later and want to keep them.

[Files]
; The deployment folder doubles as a portable test install: its config\ (profiles, logs) never ships.
Source: "deployment\windows-amd64\*"; DestDir: "{app}"; Excludes: "*.pdb,\config,\config\*,\icons,\updater.exe,\updater.old"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\F-Dpi"; Filename: "{app}\F-Dpi.exe"
Name: "{autodesktop}\F-Dpi"; Filename: "{app}\F-Dpi.exe"

[Registry]
; Read by F-Dpi (Branding::InstallRegistryKey) to register its file and link associations.
Root: HKLM; Subkey: "Software\F-Dpi"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekey

[UninstallDelete]
Type: files; Name: "{app}\updater.old"

[Run]
; Setup runs elevated, so F-Dpi (requireAdministrator) starts without another prompt.
Filename: "{app}\F-Dpi.exe"; Description: "{cm:LaunchProgram,F-Dpi}"; Flags: postinstall nowait skipifsilent

[Code]
function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir: String;
begin
  Result := True;
  if CurPageID <> wpSelectDir then
    Exit;
  Dir := RemoveBackslashUnlessRoot(WizardDirValue);
  // Uninstalling can delete <dir>\config, so F-Dpi must get a folder of its own.
  if CompareText(ExtractFileName(Dir), 'F-Dpi') <> 0 then
    WizardForm.DirEdit.Text := AddBackslash(Dir) + 'F-Dpi';
end;

// Ends F-Dpi, its core and winws when they run from this install. A graceful exit is not available here; -cleanup below
// does what it would have done.
procedure StopFDpi;
var
  Locator, Service, Processes, Process: Variant;
  Prefix, ExePath: String;
  I: Integer;
  Stopped: Boolean;
begin
  Prefix := Lowercase(AddBackslash(ExpandConstant('{app}')));
  Stopped := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Service.ExecQuery('SELECT * FROM Win32_Process WHERE Name = ''F-Dpi.exe'' OR Name = ''ThroneCore.exe'' OR Name = ''winws.exe''');
    for I := 0 to Processes.Count - 1 do
    begin
      // One at a time: a process that exited since the query (Restart Manager closed it) must not stop the others.
      try
        Process := Processes.ItemIndex(I);
        if not VarIsNull(Process.ExecutablePath) then
        begin
          // Pascal Script converts a Variant to String on assignment, but not when passed as a String parameter.
          ExePath := Process.ExecutablePath;
          if Pos(Prefix, Lowercase(ExePath)) = 1 then
          begin
            Process.Terminate(0);
            Stopped := True;
          end;
        end;
      except
        Log('Skipped a process that is gone: ' + GetExceptionMessage);
      end;
    end;
  except
    Log('Could not stop F-Dpi: ' + GetExceptionMessage);
  end;
  if Stopped then
    Sleep(1500);
end;

// F-Dpi.exe -cleanup puts back the Windows DNS settings it changed for DPI Bypass and unloads the WinDivert driver, which
// would otherwise keep zapret\WinDivert64.sys locked.
procedure CleanupFDpi;
var
  Exe: String;
  Code: Integer;
begin
  Exe := ExpandConstant('{app}\F-Dpi.exe');
  if not FileExists(Exe) then
    Exit;
  if Exec(Exe, '-cleanup', ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, Code) then
    Log(Format('F-Dpi -cleanup exited with %d', [Code]))
  else
    Log('Could not run F-Dpi -cleanup: ' + SysErrorMessage(Code));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  // An update: before any file is replaced (a running F-Dpi, winws or the WinDivert driver keeps them locked).
  if CurStep = ssInstall then
  begin
    StopFDpi;
    CleanupFDpi;
  end;
end;

// F-Dpi writes these at runtime; an entry that points at another copy by now belongs to that copy.
function PointsAtApp(const SubKey: String): Boolean;
var
  Command: String;
begin
  Result := RegQueryStringValue(HKEY_CURRENT_USER, SubKey + '\shell\open\command', '', Command) and
    (Pos(Lowercase(ExpandConstant('{app}\F-Dpi.exe')), Lowercase(Command)) > 0);
end;

procedure RemoveOpenWith(const Ext: String);
begin
  RegDeleteValue(HKEY_CURRENT_USER, 'Software\Classes\' + Ext + '\OpenWithProgids', 'F-Dpi.Config');
end;

procedure RemoveAssociations;
begin
  // throne:// is shared with Throne: removed only while it still opens this copy.
  if PointsAtApp('Software\Classes\throne') then
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\throne');
  if PointsAtApp('Software\Classes\Applications\F-Dpi.exe') then
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\Applications\F-Dpi.exe');
  if not PointsAtApp('Software\Classes\F-Dpi.Config') then
    Exit;
  RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\F-Dpi.Config');
  RemoveOpenWith('.json');
  RemoveOpenWith('.conf');
  RemoveOpenWith('.yaml');
  RemoveOpenWith('.yml');
end;

// F-Dpi writes these to HKLM (it runs elevated).
procedure RemoveCrashDumpKey(const ExeName: String);
var
  SubKey, Folder: String;
begin
  SubKey := 'SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\' + ExeName;
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE, SubKey, 'DumpFolder', Folder) then
    Exit;
  if Pos(Lowercase(AddBackslash(ExpandConstant('{app}'))), Lowercase(AddBackslash(Folder))) = 1 then
    RegDeleteKeyIncludingSubkeys(HKEY_LOCAL_MACHINE, SubKey);
end;

// "Start with system" is a scheduled task named "F-Dpi AutoRun <hash>" (Branding::AutoRunTaskPrefix).
procedure RemoveAutoRunTasks;
var
  Code: Integer;
begin
  Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -NonInteractive -Command "Get-ScheduledTask -TaskName ''F-Dpi AutoRun *'' -ErrorAction SilentlyContinue | Unregister-ScheduledTask -Confirm:$false"',
    '', SW_HIDE, ewWaitUntilTerminated, Code);
end;

var
  DeleteUserData: Boolean;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  App: String;
begin
  App := ExpandConstant('{app}');
  if CurUninstallStep = usUninstall then
  begin
    StopFDpi;
    CleanupFDpi;
    RemoveAssociations;
    RemoveAutoRunTasks;
    RemoveCrashDumpKey('F-Dpi.exe');
    RemoveCrashDumpKey('ThroneCore.exe');
    DeleteUserData := SuppressibleMsgBox(CustomMessage('DeleteUserData'), mbConfirmation, MB_YESNO, IDYES) = IDYES;
  end
  else if (CurUninstallStep = usPostUninstall) and DeleteUserData then
  begin
    if FileExists(App + '\config\throne.db') then
      DelTree(App + '\config', True, True, True);
    // Where F-Dpi keeps its config when its own folder is not writable (Qt's AppConfigLocation). Only the config folder:
    // another program keeps files of its own in %LOCALAPPDATA%\F-Dpi.
    if FileExists(ExpandConstant('{localappdata}\F-Dpi\config\throne.db')) then
      DelTree(ExpandConstant('{localappdata}\F-Dpi\config'), True, True, True);
    RemoveDir(ExpandConstant('{localappdata}\F-Dpi'));
    RemoveDir(App);
  end;
end;
