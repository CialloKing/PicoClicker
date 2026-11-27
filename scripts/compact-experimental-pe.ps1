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

function Write-U32([byte[]] $buffer, [int] $offset, [uint32] $value) {
    [BitConverter]::GetBytes($value).CopyTo($buffer, $offset)
}

$temporary = "$destination.$([Guid]::NewGuid().ToString('N')).tmp"
try {
    [IO.File]::Copy($source, $temporary)
    & (Join-Path $PSScriptRoot 'compact-pe.ps1') -Executable $temporary -AllowResourceGrowth | Out-Null
    [byte[]] $image = [IO.File]::ReadAllBytes($temporary)
    $pe = 64
    $optional = $pe + 24
    $table = $optional + [BitConverter]::ToUInt16($image, $pe + 20)
    $directories = $table - 16 * 8
    $headers = [BitConverter]::ToUInt32($image, $optional + 60)
    if ([BitConverter]::ToUInt16($image, $pe + 6) -ne 3 -or $headers -ne 512 -or
        [BitConverter]::ToUInt32($image, $directories - 4) -ne 16 -or
        [BitConverter]::ToUInt32($image, $optional + 36) -ne 512 -or $table + 120 -gt $headers) {
        throw 'Expected the compact three-section Lite PE.'
    }

    $sections = for ($index = 0; $index -lt 3; $index++) {
        $header = $table + 40 * $index
        [pscustomobject]@{
            Header = $header
            Name = [Text.Encoding]::ASCII.GetString($image, $header, 8).TrimEnd([char]0)
            Size = [BitConverter]::ToUInt32($image, $header + 8)
            Rva = [BitConverter]::ToUInt32($image, $header + 12)
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

    # The base relocation directory has no references from application code. If
    # it fills the resource tail, try placing it in the existing .text padding.
    $reloc = $directories + 5 * 8
    $relocRva = [BitConverter]::ToUInt32($image, $reloc)
    $relocSize = [BitConverter]::ToUInt32($image, $reloc + 4)
    $copyStart = [int]([Math]::Ceiling($text.Size / 4.0) * 4)
    $codeEnd = $copyStart + $relocSize
    $initialized = [BitConverter]::ToUInt32($image, $optional + 8)
    if ($relocSize -and $relocRva -gt $resources.Rva -and
        $relocRva + $relocSize -eq $resources.Rva + $resources.Size -and $codeEnd -le $text.RawSize) {
        $resourceEnd = $relocRva - $resources.Rva
        $resourceRawSize = [int]([Math]::Ceiling($resourceEnd / 512.0) * 512)
        $originalSize = [Math]::Min($text.RawSize + $resources.Size, $resources.RawSize + $text.Size)
        $candidateSize = [Math]::Min($text.RawSize + $resourceEnd, $resourceRawSize + $codeEnd)
        if ($candidateSize -lt $originalSize) {
            for ($index = 0; $index -lt 16; $index++) {
                if ($index -eq 5) { continue }
                $start = [BitConverter]::ToUInt32($image, $directories + 8 * $index)
                $size = [BitConverter]::ToUInt32($image, $directories + 8 * $index + 4)
                if ($size -and $start -lt $relocRva + $relocSize -and $start + $size -gt $relocRva) {
                    throw 'Another data directory overlaps the base relocation table.'
                }
            }

            $sourceOffset = $resources.Offset + $resourceEnd
            [Array]::Copy($image, $sourceOffset, $image, $text.Offset + $copyStart, $relocSize)
            [Array]::Clear($image, $sourceOffset, $resources.RawSize - $resourceEnd)
            Write-U32 $image $reloc ($text.Rva + $copyStart)
            $text.Size = $codeEnd
            $resources.Size = $resourceEnd
            $initialized -= $resources.RawSize - $resourceRawSize
            $resources.RawSize = $resourceRawSize
        }
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
    Write-U32 $output ($first.Header + 20) $headers
    Write-U32 $output ($last.Header + 20) $lastOffset
    Write-U32 $output ($first.Header + 16) $first.RawSize
    Write-U32 $output ($last.Header + 16) $last.Size
    Write-U32 $output ($text.Header + 8) $text.Size
    Write-U32 $output ($resources.Header + 8) $resources.Size
    if ($last.Name -ceq '.text') {
        Write-U32 $output ($optional + 4) ($text.RawSize - $padding)
    } else {
        $initialized -= $padding
    }
    Write-U32 $output ($optional + 8) $initialized
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
