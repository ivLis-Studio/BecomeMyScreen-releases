; One native payload per OS; display driver is a required installation prerequisite.
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef PayloadRoot
  #define PayloadRoot "..\artifacts\universal"
#endif
[Setup]
AppId={{A681DC29-8D79-4432-A3B7-D5DA620F260D}
AppName=BecomeMyScreen
AppVersion={#AppVersion}
AppPublisher=ivLis Studio
AppPublisherURL=https://ivlis-studio.github.io/BecomeMyScreen-releases/
DefaultDirName={autopf}\BecomeMyScreen
DefaultGroupName=BecomeMyScreen
DisableProgramGroupPage=yes
DisableWelcomePage=no
DisableDirPage=auto
PrivilegesRequired=admin
ArchitecturesAllowed=x64os or arm64
ArchitecturesInstallIn64BitMode=x64os or arm64
MinVersion=10.0.22000
WizardStyle=modern
SetupIconFile=..\src\BecomeMyScreen.Windows\Assets\BecomeMyScreen.ico
UninstallDisplayIcon={app}\BecomeMyScreen.exe
UninstallDisplayName=BecomeMyScreen
OutputDir=..\artifacts\installer
OutputBaseFilename=BecomeMyScreen-Windows-Setup-{#AppVersion}
Compression=zip/1
SolidCompression=no
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
CloseApplicationsFilter=BecomeMyScreen.exe,BecomeMyScreen.dll,BecomeMyScreen.Core.dll,ffmpeg.exe
VersionInfoVersion={#AppVersion}
; App Authenticode is optional. The updater accepts fixed-repository GitHub assets verified by SHA-256.
#ifdef SignedRelease
SignTool=ProductSign
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"

[CustomMessages]
english.WelcomeLabel2=Your tablet. Your second screen.%n%nSetup will install BecomeMyScreen and the display driver for this PC automatically. The tablet companion app is prepared when you connect by USB.%n%nYour tablet pairing and preferences are kept when updating.
korean.WelcomeLabel2=태블릿을 Windows의 두 번째 화면으로.%n%n이 PC에 맞는 BecomeMyScreen과 디스플레이 드라이버를 자동으로 설치합니다. 태블릿 앱은 USB로 연결할 때 준비됩니다.%n%n업데이트할 때 태블릿 등록과 설정은 유지됩니다.
english.DriverProgress=Preparing your display… If Windows asks, review the SignPath Foundation driver and choose Install.
korean.DriverProgress=디스플레이를 준비하고 있습니다… Windows가 묻는 경우 SignPath Foundation 드라이버를 확인한 뒤 설치를 선택하세요.
english.DriverFailure=Windows did not accept the display driver. BecomeMyScreen has not been installed or updated.%n%nNo security settings were changed. Details: %1%n%nInstallation log: %2
korean.DriverFailure=Windows에서 디스플레이 드라이버 설치를 허용하지 않았습니다. 앱 설치 또는 업데이트를 진행하지 않았습니다.%n%n보안 설정은 변경하지 않았습니다. 상세 정보: %1%n%n설치 기록: %2
english.ArmHint=%n%nOn ARM64 Windows, this driver package may require a Microsoft-signed replacement. Repeated installation will not resolve that restriction.
korean.ArmHint=%n%nARM64 Windows에서는 Microsoft 서명이 적용된 드라이버 패키지가 필요할 수 있습니다. 설치를 반복해도 해당 제한은 해결되지 않습니다.
english.DriverReady=Display driver and app will be installed together.
korean.DriverReady=디스플레이 드라이버와 앱을 함께 설치합니다.
english.OpenApp=Open BecomeMyScreen
korean.OpenApp=BecomeMyScreen 실행
english.DesktopShortcut=Create a desktop shortcut
korean.DesktopShortcut=바탕 화면 바로 가기 만들기

[Tasks]
Name: desktopicon; Description: "{cm:DesktopShortcut}"; Flags: unchecked

[Files]
Source: "stage-driver.ps1"; DestDir: "{tmp}"; Flags: dontcopy
Source: "{#PayloadRoot}\win-x64\driver\*"; DestDir: "{tmp}\driver-x64"; Flags: dontcopy
Source: "{#PayloadRoot}\win-arm64\driver\*"; DestDir: "{tmp}\driver-arm64"; Flags: dontcopy
Source: "{#PayloadRoot}\win-x64\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: not IsArm64
Source: "{#PayloadRoot}\win-arm64\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: IsArm64

[Icons]
Name: "{autoprograms}\BecomeMyScreen"; Filename: "{app}\BecomeMyScreen.exe"
Name: "{autodesktop}\BecomeMyScreen"; Filename: "{app}\BecomeMyScreen.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\BecomeMyScreen.exe"; Description: "{cm:OpenApp}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallRun]
Filename: "{app}\BecomeMyScreen.DisplayService.exe"; Parameters: "--uninstall"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveBecomeMyScreenDisplay"

[Code]
var
  DriverReady: Boolean;
  DriverNeedsRestart: Boolean;
  DriverOutput: String;

procedure DriverLog(const S: String; const Error, FirstLine: Boolean);
begin
  Log(S);
  if Length(DriverOutput) < 2000 then DriverOutput := DriverOutput + S + #13#10;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Arch, InfPath, Args: String;
  ExitCode: Integer;
  Started: Boolean;
begin
  Result := '';
  if DriverReady then exit;
  if IsArm64 then Arch := 'arm64' else Arch := 'x64';
  WizardForm.PreparingLabel.Caption := CustomMessage('DriverProgress');
  ExtractTemporaryFiles('{tmp}\driver-' + Arch + '\*');
  InfPath := ExpandConstant('{tmp}\driver-' + Arch + '\MttVDD.inf');
  DriverOutput := '';
  { PnP verifies the signed catalog and payload. Never change test signing, Secure Boot or trust stores. }
  ExtractTemporaryFile('stage-driver.ps1');
  Args := '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + ExpandConstant('{tmp}\stage-driver.ps1') +
    '" -Inf "' + InfPath + '" -ParentWindow ' + IntToStr(WizardForm.Handle);
  if WizardSilent then Args := Args + ' -QuietMode 1';
  Started := ExecAndLogOutputWithNativeSysDir(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    Args, '', SW_HIDE, ewWaitUntilTerminated, ExitCode, @DriverLog);
  if Started and ((ExitCode = 0) or (ExitCode = 3010)) then begin
    DriverReady := True;
    if ExitCode = 3010 then DriverNeedsRestart := True;
  end else begin
    if Trim(DriverOutput) = '' then DriverOutput := IntToStr(ExitCode);
    Result := FmtMessage(CustomMessage('DriverFailure'), [Trim(DriverOutput), ExpandConstant('{log}')]);
    if IsArm64 then Result := Result + CustomMessage('ArmHint');
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ExitCode: Integer;
begin
  if CurStep = ssPostInstall then begin
    if not Exec(ExpandConstant('{app}\BecomeMyScreen.DisplayService.exe'), '--install', '', SW_HIDE, ewWaitUntilTerminated, ExitCode) or (ExitCode <> 0) then
      RaiseException(Format('BecomeMyScreen Display service setup failed (%d).', [ExitCode]));
  end;
end;

function NeedRestart: Boolean;
begin
  Result := DriverNeedsRestart;
end;

procedure InitializeWizard;
begin
  WizardForm.WelcomeLabel2.Caption := CustomMessage('WelcomeLabel2');
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfoInfo, MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := CustomMessage('DriverReady') + NewLine + NewLine + MemoDirInfoInfo;
  if MemoTasksInfo <> '' then Result := Result + NewLine + NewLine + MemoTasksInfo;
end;
