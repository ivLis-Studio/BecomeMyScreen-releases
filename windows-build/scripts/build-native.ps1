param(
    [ValidateSet('win-x64','win-arm64')][string]$Runtime = 'win-x64',
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$OutputDirectory) { $OutputDirectory = Join-Path $root "artifacts/native/$Runtime" }
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path $OutputDirectory).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -property installationPath
if (!$installation) { throw 'Visual Studio C++ Build Tools and Windows SDK are required for the native callback bridge.' }
$vcvars = Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat'
$hostArch = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') { 'arm64' } else { 'amd64' }
$targetArch = if ($Runtime -eq 'win-arm64') { 'arm64' } else { 'amd64' }
$compilerArch = if ($hostArch -eq $targetArch) { $hostArch } else { "${hostArch}_${targetArch}" }
$source = Join-Path $root 'native/SoftwareDeviceBridge.c'
# /MT keeps this small user-mode DLL independent of an installed VC++ runtime.
$command = "`"$vcvars`" $compilerArch && cl.exe /nologo /W4 /WX /O2 /MT /LD /guard:cf `"$source`" /Fo`"$OutputDirectory\SoftwareDeviceBridge.obj`" /link /OUT:`"$OutputDirectory\BecomeMyScreen.Native.dll`" /IMPLIB:`"$OutputDirectory\BecomeMyScreen.Native.lib`" /DYNAMICBASE /NXCOMPAT /guard:cf swdevice.lib"
# Pass the cmd command line verbatim: PowerShell 7's native argument quoting
# otherwise adds a second layer around a Visual Studio path containing spaces.
$start = [Diagnostics.ProcessStartInfo]::new($env:ComSpec)
$start.UseShellExecute = $false
$start.Arguments = '/d /s /c "' + $command + '"'
$process = [Diagnostics.Process]::Start($start)
try {
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "Native bridge build failed: $Runtime (exit $($process.ExitCode))" }
} finally { $process.Dispose() }
. (Join-Path $PSScriptRoot 'package-validation.ps1')
Assert-PeArchitecture (Join-Path $OutputDirectory 'BecomeMyScreen.Native.dll') $Runtime
