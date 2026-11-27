param(
    [Parameter(Mandatory = $true)]
    [string] $Executable,
    [Parameter(Mandatory = $true)]
    [string] $OutputFile
)

$ErrorActionPreference = 'Stop'
$source = (Get-Item -LiteralPath $Executable).FullName
$destination = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputFile)
if ($source -ieq $destination) {
    throw 'The experimental PE needs a separate output file.'
}

$temporary = "$destination.$([Guid]::NewGuid().ToString('N')).tmp"
try {
    [IO.File]::Copy($source, $temporary)
    & (Join-Path $PSScriptRoot 'compact-pe.ps1') -Executable $temporary | Out-Null
    [byte[]] $image = [IO.File]::ReadAllBytes($temporary)
    $pe = 64
    $optional = $pe + 24
    $table = $optional + [BitConverter]::ToUInt16($image, $pe + 20)
    $headers = [BitConverter]::ToUInt32($image, $optional + 60)
    if ([BitConverter]::ToUInt16($image, $pe + 6) -ne 3 -or $headers -ne 512 -or
        [BitConverter]::ToUInt32($image, $optional + 36) -ne 512 -or $table + 120 -gt $headers) {
        throw 'Expected the compact three-section Lite PE.'
    }

    $sections = for ($index = 0; $index -lt 3; $index++) {
        $header = $table + 40 * $index
        [pscustomobject]@{
            Header = $header
            Name = [Text.Encoding]::ASCII.GetString($image, $header, 8).TrimEnd([char]0)
            Size = [BitConverter]::ToUInt32($image, $header + 8)
            RawSize = [BitConverter]::ToUInt32($image, $header + 16)
            Offset = [BitConverter]::ToUInt32($image, $header + 20)
        }
    }
    $text, $data, $resources = $sections
    if (($sections.Name -join ',') -cne '.text,.data,.rsrc' -or $data.RawSize -or
        !$text.Size -or $text.Size -gt $text.RawSize -or !$resources.Size -or $resources.Size -gt $resources.RawSize -or
        $text.Offset -ne $headers -or $resources.Offset -ne $text.Offset + $text.RawSize -or
        $resources.Offset + $resources.RawSize -ne $image.Length) {
        throw 'Unexpected on-disk layout in the compact Lite PE.'
    }

    # Keep the RVAs and permissions. Put the section with more padding last so only
    # its raw size may end between file-alignment boundaries.
    $first, $last = @($text, $resources) | Sort-Object { $_.RawSize - $_.Size }, Offset
    $padding = $last.RawSize - $last.Size
    $lastOffset = $headers + $first.RawSize
    [byte[]] $output = [byte[]]::new($lastOffset + $last.Size)
    [Array]::Copy($image, $output, $headers)
    [Array]::Copy($image, $first.Offset, $output, $headers, $first.RawSize)
    [Array]::Copy($image, $last.Offset, $output, $lastOffset, $last.Size)
    [BitConverter]::GetBytes([uint32]$headers).CopyTo($output, $first.Header + 20)
    [BitConverter]::GetBytes([uint32]$lastOffset).CopyTo($output, $last.Header + 20)
    [BitConverter]::GetBytes([uint32]$last.Size).CopyTo($output, $last.Header + 16)

    $sizeField = if ($last.Name -ceq '.text') { $optional + 4 } else { $optional + 8 }
    $size = [BitConverter]::ToUInt32($image, $sizeField) - $padding
    [BitConverter]::GetBytes([uint32]$size).CopyTo($output, $sizeField)
    [IO.File]::WriteAllBytes($temporary, $output)
    if ([IO.File]::Exists($destination)) {
        [IO.File]::Replace($temporary, $destination, [NullString]::Value)
    } else {
        [IO.File]::Move($temporary, $destination)
    }
    Write-Output ("{0} {1} -> {2} bytes" -f [IO.Path]::GetFileName($destination), $image.Length, $output.Length)
} finally {
    [IO.File]::Delete($temporary)
}
