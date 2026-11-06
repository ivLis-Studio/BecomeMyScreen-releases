function Assert-PeArchitecture([string]$Path, [string]$Runtime) {
    $expected = if ($Runtime -eq 'win-arm64') { 0xAA64 } elseif ($Runtime -eq 'win-x64') { 0x8664 } else { throw "Unsupported runtime: $Runtime" }
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Not a Windows executable: $Path" }
        $stream.Position = 0x3C
        $offset = $reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt $stream.Length - 6) { throw "Invalid PE header: $Path" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne $expected) {
            throw "Executable architecture does not match ${Runtime}: $Path"
        }
    } finally { $reader.Dispose(); $stream.Dispose() }
}
