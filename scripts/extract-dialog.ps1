param(
    [Parameter(Mandatory = $true)]
    [string] $Resource,
    [Parameter(Mandatory = $true)]
    [string] $OutputFile
)

$ErrorActionPreference = 'Stop'
[byte[]] $bytes = [IO.File]::ReadAllBytes($Resource)
# RC emits one empty 32-byte record before the dialog record.
if ($bytes.Length -lt 64 -or [BitConverter]::ToUInt32($bytes, 0) -ne 0 -or
    [BitConverter]::ToUInt32($bytes, 4) -ne 32) {
    throw 'Expected the empty header of a Win32 .res file.'
}

$size = [BitConverter]::ToUInt32($bytes, 32)
$start = 32 + [BitConverter]::ToUInt32($bytes, 36)
$end = $start + $size
if ($size -lt 18 -or $start -ne 64 -or $end -gt $bytes.Length -or $bytes.Length - $end -gt 3 -or
    [BitConverter]::ToUInt16($bytes, 40) -ne 0xffff -or [BitConverter]::ToUInt16($bytes, 42) -ne 5 -or
    [BitConverter]::ToUInt16($bytes, 44) -ne 0xffff) {
    throw 'Expected one dialog with a numeric resource ID.'
}

# Strip the resource metadata while preserving the compiler's dialog alignment and UTF-16 strings.
[byte[]] $template = [byte[]]::new($size)
[Array]::Copy($bytes, $start, $template, 0, $size)
[IO.File]::WriteAllBytes($OutputFile, $template)
