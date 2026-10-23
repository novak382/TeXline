; TeXline per-user preview. Original TeXworks installer is preserved separately.
#ifndef PayloadDir
  #define PayloadDir "..\out\release\payload"
#endif
#ifndef ReleaseDir
  #define ReleaseDir "..\out\release"
#endif
#define PreviewVersion "2026.10.20-preview.1"

[Setup]
AppId={{2C52BB26-ED3D-4919-A82A-7F873FD8B1E5}
AppName=TeXline
AppVersion={#PreviewVersion}
AppPublisher=TeXline contributors
AppCopyright=TeXline contributors and TeXworks authors; see bundled notices
DefaultDirName={localappdata}\Programs\TeXline
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
DisableProgramGroupPage=yes
DisableWelcomePage=no
UsePreviousAppDir=no
UsePreviousLanguage=no
UsePreviousSetupType=no
UsePreviousTasks=no
UsePreviousGroup=no
CreateUninstallRegKey=not IsolatedMode
UninstallDisplayIcon={app}\TeXline.exe
UninstallDisplayName=TeXline {#PreviewVersion}
LicenseFile=..\LICENSES\GPL-3.0.txt
InfoBeforeFile=preview-welcome.txt
SetupIconFile=..\res\images\TeXworks-setup.ico
OutputDir={#ReleaseDir}
OutputBaseFilename=TeXline-{#PreviewVersion}-Setup-x64
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
ChangesAssociations=no
VersionInfoVersion=2026.10.20.1
WizardStyle=modern
SetupLogging=yes

[Languages]
Name: english; MessagesFile: compiler:Default.isl
Name: chinesesimplified; MessagesFile: extra-setup-languages\ChineseSimplified.isl

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Excludes: "settings\*"; Flags: ignoreversion recursesubdirs createallsubdirs

; No [Icons], [Registry] file associations or [Run] entry. Never auto-launch.
[Code]
function OpenExistingFile(FileName: String; Access, Share, Security,
  Creation, Attributes, Template: LongWord): LongWord;
  external 'CreateFileW@kernel32.dll stdcall';
function CloseNativeHandle(Handle: LongWord): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';
function SetNativeFrameAttribute(Handle, Attribute: LongWord; var Value: LongWord;
  Size: LongWord): LongInt;
  external 'DwmSetWindowAttribute@dwmapi.dll stdcall delayload';

procedure InitializeWizard();
var
  BorderColor: LongWord;
begin
  BorderColor := $00D6D6D6;
  try
    { Windows 11 accepts an explicit neutral visible frame. Windows 10 ignores it. }
    SetNativeFrameAttribute(WizardForm.Handle, 34, BorderColor, 4);
  except
    { Keep the native frame when the attribute is unavailable. }
  end;
end;

function IsolatedMode(): Boolean;
begin
  Result := ExpandConstant('{param:ISOLATED|0}') = '1';
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Existing, Backup, Digest, Folder: String;
  Handle: LongWord;
begin
  Result := '';
  Folder := ExpandConstant('{app}');
  if (Folder = ExtractFileDrive(Folder) + '\') or DirExists(Folder + '\.git') then begin
    Result := 'Choose a dedicated TeXline installation folder.';
    Exit;
  end;
  Existing := Folder + '\TeXline.exe';
  if not FileExists(Existing) then Exit;
  if not FileExists(Folder + '\release-manifest.json') then begin
    Result := 'This folder is not a TeXline preview installation. Choose a new folder; the existing files have been kept.';
    Exit;
  end;
  Handle := OpenExistingFile(Existing, $C0000000, 0, 0, 3, 128, 0);
  if Handle = $FFFFFFFF then begin
    Result := 'TeXline is in use. Close your own TeXline session, then retry. No process was closed.';
    Exit;
  end;
  CloseNativeHandle(Handle);
  Digest := GetSHA256OfFile(Existing);
  if Digest = '' then begin Result := 'Could not verify the existing executable.'; Exit; end;
  Backup := Folder + '\rollback\TeXline-' + Copy(Digest,1,16) + '.exe';
  if not ForceDirectories(Folder + '\rollback') then begin Result := 'Could not prepare rollback.'; Exit; end;
  if not FileExists(Backup) then
    if not FileCopy(Existing, Backup, True) then begin Result := 'Could not back up the existing executable.'; Exit; end;
  if GetSHA256OfFile(Backup) <> Digest then Result := 'Rollback verification failed; upgrade stopped.';
end;
