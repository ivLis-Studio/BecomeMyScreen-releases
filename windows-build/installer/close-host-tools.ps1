# Invoked by Setup after the driver prerequisite succeeds, before files are
# replaced. adb kill-server would address whichever server owns port 5037,
# which may belong to Android Studio or another Android SDK.
param([Parameter(Mandatory)][string]$InstallDir)
$ErrorActionPreference = 'Stop'
function Get-OwnedProcesses([string]$Path) {
    $wanted = [IO.Path]::GetFullPath($Path)
    foreach ($p in [Diagnostics.Process]::GetProcessesByName([IO.Path]::GetFileNameWithoutExtension($wanted))) {
        $keep = $false
        try {
            # Cache the process handle before inspection: a recycled PID is not a match.
            [void]$p.Handle
            $image = $p.MainModule.FileName
            $keep = [string]::Equals($image,$wanted,[StringComparison]::OrdinalIgnoreCase)
            if ($keep) { $p }
        } catch [ComponentModel.Win32Exception] { } catch [InvalidOperationException] { }
        finally { if (!$keep) { $p.Dispose() } }
    }
}
function Stop-Owned([string]$Path,[bool]$HostApp) {
    foreach ($p in @(Get-OwnedProcesses $Path)) {
        try {
            if ($p.HasExited) { continue }
            if ($HostApp) {
                try {
                    $event = [Threading.EventWaitHandle]::OpenExisting("Local\BecomeMyScreen.QuitForUpdate.$($p.Id)")
                    try { [void]$event.Set() } finally { $event.Dispose() }
                    if ($p.WaitForExit(5000)) { continue }
                } catch [Threading.WaitHandleCannotBeOpenedException] { } catch [UnauthorizedAccessException] { }
                # Older versions hide their main window on close and have no update event.
            }
            Write-Output "Closing owned process $($p.Id): $Path"
            if (!$p.HasExited) { $p.Kill() }
            if (!$p.WaitForExit(5000)) { throw "Could not close $Path" }
        } catch [InvalidOperationException] { if (!$p.HasExited) { throw } }
        finally { $p.Dispose() }
    }
}
$app = Join-Path $InstallDir 'BecomeMyScreen.exe'
$adb = Join-Path $InstallDir 'tools\adb.exe'
Stop-Owned $app $true
# The host cannot respawn the server once closed. Catch a server still starting
# from an in-flight adb client as well as the already-running daemon.
for ($n=0; $n -lt 3; $n++) { Stop-Owned $adb $false; Start-Sleep -Milliseconds 150 }
foreach ($file in @($app,$adb)) {
    $remaining = @(Get-OwnedProcesses $file)
    try { if ($remaining.Count) { throw "Still running: $file" } }
    finally { foreach ($p in $remaining) { $p.Dispose() } }
}
