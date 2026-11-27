param(
    [Parameter(Mandatory = $true)]
    [string] $Resource,
    [Parameter(Mandatory = $true)]
    [string] $OutputFile,
    [switch] $EncodeWords
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
if ($size -lt 18 -or $size % 2 -or $start -ne 64 -or $end -gt $bytes.Length -or $bytes.Length - $end -gt 3 -or
    [BitConverter]::ToUInt16($bytes, 40) -ne 0xffff -or [BitConverter]::ToUInt16($bytes, 42) -ne 5 -or
    [BitConverter]::ToUInt16($bytes, 44) -ne 0xffff) {
    throw 'Expected one dialog with a numeric resource ID.'
}

if (!$EncodeWords) {
    # Strip the resource record without changing the template's layout or strings.
    [byte[]] $template = [byte[]]::new($size)
    [Array]::Copy($bytes, $start, $template, 0, $size)
    [IO.File]::WriteAllBytes($OutputFile, $template)
    return
}

# Most words are small control fields, zeroes or ASCII characters in UTF-16.
# Byte 255 represents 0xffff; byte 254 introduces a literal little-endian word.
$encoded = [Collections.Generic.List[byte]]::new()
for ($offset = $start; $offset -lt $end; $offset += 2) {
    $word = [BitConverter]::ToUInt16($bytes, $offset)
    if ($word -eq 0xffff) {
        $encoded.Add(255)
    } elseif ($word -ge 254) {
        $encoded.Add(254)
        $encoded.Add($bytes[$offset])
        $encoded.Add($bytes[$offset + 1])
    } else {
        $encoded.Add($bytes[$offset])
    }
}

$assembly = [Collections.Generic.List[string]]::new()
$assembly.Add(".set .Ldialog_words, $($size / 2)")
for ($offset = 0; $offset -lt $encoded.Count; $offset += 16) {
    $count = [Math]::Min(16, $encoded.Count - $offset)
    $assembly.Add('.byte ' + ($encoded.GetRange($offset, $count) -join ', '))
}

# The runtime uses these strings as ANSI text. Alias an exact NUL-terminated
# match in the encoded template, or emit a copy if the dialog text changes.
$encoding = [Text.Encoding]::GetEncoding(28591).GetString($encoded.ToArray())
$labels = [ordered]@{ '.Ltitle' = 'PicoClicker'; '.Lstart' = 'Start' }
foreach ($entry in $labels.GetEnumerator()) {
    $literal = $entry.Value
    $position = $encoding.IndexOf($literal + [char]0, [StringComparison]::Ordinal)
    if ($position -ge 0) {
        $assembly.Add(".set $($entry.Key), .Ldialog_encoding + $position")
    } else {
        $assembly.Add(('{0}: .asciz "{1}"' -f $entry.Key, $literal))
    }
}
[IO.File]::WriteAllLines($OutputFile, $assembly, [Text.Encoding]::ASCII)
