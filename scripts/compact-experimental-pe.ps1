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
        throw 'Expected a compact three-section experimental PE.'
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
    if (($sections.Name -join ',') -cnotin '.text,.rdata,.data', '.text,.data,.rsrc') {
        throw 'Unexpected sections in the compact experimental PE.'
    }
    $text = $sections[0]
    $data = $sections | Where-Object Name -ceq '.data'
    $readOnly = $sections | Where-Object Name -cin '.rdata', '.rsrc'
    if ($data.RawSize -or !$text.Size -or $text.Size -gt $text.RawSize -or
        !$readOnly.Size -or $readOnly.Size -gt $readOnly.RawSize -or
        $text.Offset -ne $headers -or $readOnly.Offset -ne $text.Offset + $text.RawSize -or
        $readOnly.Offset + $readOnly.RawSize -ne $image.Length) {
        throw 'Unexpected on-disk layout in the compact experimental PE.'
    }

    # Base relocations have no references from application code. Compare keeping
    # them at the read-only section's tail, using .text padding or using the headers.
    $reloc = $directories + 5 * 8
    $relocRva = [BitConverter]::ToUInt32($image, $reloc)
    $relocSize = [BitConverter]::ToUInt32($image, $reloc + 4)
    $copyStart = [int]([Math]::Ceiling($text.Size / 4.0) * 4)
    $codeEnd = $copyStart + $relocSize
    $initialized = [BitConverter]::ToUInt32($image, $optional + 8)
    if ($relocSize -and $relocRva -gt $readOnly.Rva -and
        $relocRva + $relocSize -eq $readOnly.Rva + $readOnly.Size) {
        $readOnlyEnd = $relocRva - $readOnly.Rva
        $readOnlyRawSize = [int]([Math]::Ceiling($readOnlyEnd / 512.0) * 512)
        $bestSize = [Math]::Min($text.RawSize + $readOnly.Size, $readOnly.RawSize + $text.Size)
        $placement = $null
        $inCode = [Math]::Min($text.RawSize + $readOnlyEnd, $readOnlyRawSize + $codeEnd)
        if ($codeEnd -le $text.RawSize -and $inCode -lt $bestSize) {
            $placement = 'code'
            $bestSize = $inCode
        }

        $inHeaders = [Math]::Min($text.RawSize + $readOnlyEnd, $readOnlyRawSize + $text.Size)
        $directoryCount = 16
        $newTable = $table
        if ($inHeaders -lt $bestSize) {
            # Drop only as many empty trailing directories as needed. Keep the IAT directory.
            while ($newTable + 120 + $relocSize -gt $headers -and $directoryCount -gt 13 -and
                [BitConverter]::ToUInt64($image, $newTable - 8) -eq 0) {
                $newTable -= 8
                $directoryCount--
            }
            if ($newTable + 120 + $relocSize -le $headers) {
                $placement = 'headers'
            }
        }

        if ($placement) {
            for ($index = 0; $index -lt 16; $index++) {
                if ($index -eq 5) { continue }
                $start = [BitConverter]::ToUInt32($image, $directories + 8 * $index)
                $size = [BitConverter]::ToUInt32($image, $directories + 8 * $index + 4)
                if ($size -and $start -lt $relocRva + $relocSize -and $start + $size -gt $relocRva) {
                    throw 'Another data directory overlaps the base relocation table.'
                }
            }

            $sourceOffset = $readOnly.Offset + $readOnlyEnd
            if ($placement -ceq 'headers') {
                $headerEnd = $newTable + 120
                [Array]::Copy($image, $table, $image, $newTable, 120)
                [Array]::Clear($image, $headerEnd, $headers - $headerEnd)
                foreach ($section in $sections) {
                    $section.Header -= $table - $newTable
                }
                [BitConverter]::GetBytes([uint16]($newTable - $optional)).CopyTo($image, $pe + 20)
                Write-U32 $image ($directories - 4) $directoryCount
                $destinationOffset = $destinationRva = $headerEnd
            } else {
                $destinationOffset = $text.Offset + $copyStart
                $destinationRva = $text.Rva + $copyStart
                $text.Size = $codeEnd
            }
            [Array]::Copy($image, $sourceOffset, $image, $destinationOffset, $relocSize)
            [Array]::Clear($image, $sourceOffset, $readOnly.RawSize - $readOnlyEnd)
            Write-U32 $image $reloc $destinationRva
            $readOnly.Size = $readOnlyEnd
            $initialized -= $readOnly.RawSize - $readOnlyRawSize
            $readOnly.RawSize = $readOnlyRawSize
        }
    }

    # Keep the RVAs and permissions. Put the section with more padding last so only
    # its raw size may end between file-alignment boundaries.
    $first, $last = @($text, $readOnly) | Sort-Object { $_.RawSize - $_.Size }, Offset
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
    Write-U32 $output ($readOnly.Header + 8) $readOnly.Size
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
