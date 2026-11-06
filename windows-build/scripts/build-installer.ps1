param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [string]$PayloadRoot,
    [string]$Compiler = 'C:/Program Files (x86)/Inno Setup 7/ISCC.exe',
    [switch]$DownloadEncoderOnFirstRun,
    [string]$SigningThumbprint,
    [string]$SignTool = 'signtool.exe'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$PayloadRoot) { $PayloadRoot = Join-Path $root 'artifacts/universal' }
$PayloadRoot = (Resolve-Path $PayloadRoot).ProviderPath
. (Join-Path $PSScriptRoot 'package-validation.ps1')
foreach ($runtime in @('win-x64','win-arm64')) {
    $payload = Join-Path $PayloadRoot $runtime
    foreach ($file in @('BecomeMyScreen.exe','driver/MttVDD.dll')) { Assert-PeArchitecture (Join-Path $payload $file) $runtime }
    if (!$DownloadEncoderOnFirstRun) { Assert-PeArchitecture (Join-Path $payload 'tools/ffmpeg.exe') $runtime }
    elseif (!(Test-Path (Join-Path $payload 'ENCODER-DOWNLOAD.txt'))) { throw 'Missing first-run encoder notice.' }
    foreach ($file in @('tools/adb.exe','tools/NOTICE.Android.txt','tools/LICENSE.FFmpeg.txt','tablet/BecomeMyScreen.apk','tablet/tablet-version.txt','driver/MttVDD.inf','driver/mttvdd.cat','driver/LICENSE.VirtualDrivers')) {
        if (!(Test-Path (Join-Path $payload $file) -PathType Leaf)) { throw "Missing package component: $runtime/$file" }
    }
    foreach ($file in @('driver/MttVDD.dll','driver/mttvdd.cat')) {
        if ((Get-AuthenticodeSignature (Join-Path $payload $file)).Status -ne 'Valid') { throw "Invalid upstream signature: $runtime/$file" }
    }
    $appVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $payload 'BecomeMyScreen.dll'))
    if ("$($appVersion.FileMajorPart).$($appVersion.FileMinorPart).$($appVersion.FileBuildPart)" -ne $Version) { throw "Payload version mismatch: $runtime" }
}
$arguments = @('/Q', "/DAppVersion=$Version", "/DPayloadRoot=$PayloadRoot")
if ($SigningThumbprint) {
    if ($SigningThumbprint -notmatch '^[a-fA-F0-9]{40}$') { throw 'Invalid code signing certificate thumbprint.' }
    $SignTool = (Get-Command $SignTool -ErrorAction Stop).Source
    foreach ($runtime in @('win-x64','win-arm64')) {
        & $SignTool sign /sha1 $SigningThumbprint /fd SHA256 /tr https://timestamp.digicert.com /td SHA256 (Join-Path $PayloadRoot "$runtime/BecomeMyScreen.exe")
        if ($LASTEXITCODE -ne 0) { throw 'Application signing failed.' }
    }
    $arguments += '/DSignedRelease'
    $arguments += ('/SProductSign="{0}" sign /sha1 {1} /fd SHA256 /tr https://timestamp.digicert.com /td SHA256 $f' -f $SignTool, $SigningThumbprint)
}
& $Compiler @arguments (Join-Path $root 'installer/BecomeMyScreen.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
$installer = Join-Path $root "artifacts/installer/BecomeMyScreen-Windows-Setup-$Version.exe"
if ($SigningThumbprint -and (Get-AuthenticodeSignature $installer).Status -ne 'Valid') { throw 'Installer signature verification failed.' }
$result = [ordered]@{
    version = $Version; file = (Split-Path $installer -Leaf)
    sha256 = (Get-FileHash $installer -Algorithm SHA256).Hash.ToLowerInvariant()
    signed = [bool]$SigningThumbprint; architectures = @('x64','arm64')
    releaseTag = "windows-v$Version"
    releaseReady = $false
    encoderDownloadedOnFirstRun = [bool]$DownloadEncoderOnFirstRun
    pending = @('normal-security driver installation and extended-display QA on both architectures', 'install/update/relaunch/uninstall device QA')
}
$result | ConvertTo-Json -Depth 4 | Set-Content ($installer + '.json')
Write-Host "Installer built: $installer"
if (!$SigningThumbprint) { Write-Host 'Unsigned installer. Updates validate the fixed GitHub release asset SHA-256; Authenticode is optional.' }
