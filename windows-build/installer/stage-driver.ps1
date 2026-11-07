# Runs in the native, elevated PowerShell selected by the installer (x64 or ARM64).
# Let Windows apply its own signature policy; never import a publisher certificate.
param([Parameter(Mandatory)][string]$Inf, [long]$ParentWindow = 0, [int]$QuietMode = 0)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BecomeMyScreenDriverSetup {
    [DllImport("setupapi.dll")]
    public static extern bool SetupSetNonInteractiveMode(bool value);
    [DllImport("newdev.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
    public static extern bool DiInstallDriverW(IntPtr parent, string inf, uint flags, out bool reboot);
}
'@
$previous = [BecomeMyScreenDriverSetup]::SetupSetNonInteractiveMode($QuietMode -ne 0)
try {
    $reboot = $false
    $ok = [BecomeMyScreenDriverSetup]::DiInstallDriverW([IntPtr]::new($ParentWindow), (Resolve-Path -LiteralPath $Inf).Path, 0, [ref]$reboot)
    if (!$ok) {
        $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        Write-Output ('Windows driver installation failed: 0x{0:X8} ({1}). See %windir%\INF\setupapi.dev.log.' -f $code,([ComponentModel.Win32Exception]::new($code).Message))
        exit 1
    }
    Write-Output 'Windows accepted the display driver.'
    if ($reboot) { exit 3010 }
    exit 0
} finally { [void][BecomeMyScreenDriverSetup]::SetupSetNonInteractiveMode($previous) }
