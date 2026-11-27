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

function Get-ImportName([byte[]] $image, [int] $field, [long] $rva, [int] $hintSize, $readOnly, [long] $iatEnd) {
    if ($rva -lt $iatEnd -or $rva + $hintSize -ge $readOnly.Rva + $readOnly.Size) { return }
    $offset = [int]($readOnly.Offset + $rva - $readOnly.Rva)
    $start = $offset + $hintSize
    $limit = $readOnly.Offset + $readOnly.Size
    $nul = [Array]::IndexOf($image, [byte]0, $start, $limit - $start)
    if ($nul -le $start) { return }
    [pscustomobject]@{ Field = $field; Start = $rva; End = $rva + $nul - $offset + 1 }
}

function Get-ImportNames([byte[]] $image, [int] $directories, $readOnly, [int] $width) {
    $importRva = [BitConverter]::ToUInt32($image, $directories + 8)
    $importSize = [BitConverter]::ToUInt32($image, $directories + 12)
    $iat = [BitConverter]::ToUInt32($image, $directories + 96)
    $iatSize = [BitConverter]::ToUInt32($image, $directories + 100)
    $iatEnd = $iat + $iatSize
    if ($importSize -ne 60 -or $importRva -lt $readOnly.Rva -or $importRva + $importSize -gt $iat -or
        !$iatSize -or $iatSize % $width -or $iatEnd -gt $readOnly.Rva + $readOnly.Size) { return }

    # The hand-built descriptors share their IAT with the loader's lookup table.
    $bias = [long]$readOnly.Offset - $readOnly.Rva
    $names = [Collections.Generic.List[object]]::new()
    for ($dll = 0; $dll -lt 2; $dll++) {
        $descriptor = $bias + $importRva + 20 * $dll
        if ([BitConverter]::ToUInt64($image, $descriptor) -or
            [BitConverter]::ToUInt32($image, $descriptor + 8) -or
            [BitConverter]::ToUInt32($image, $descriptor + 16) -ne $iat) { return }
        $dllName = Get-ImportName $image ($descriptor + 12) ([BitConverter]::ToUInt32($image, $descriptor + 12)) 0 $readOnly $iatEnd
        if (!$dllName) { return }
        $names.Add($dllName)
        do {
            if ($iat -ge $iatEnd) { return }
            $slot = $bias + $iat
            $iat += $width
            $nameRva = if ($width -eq 8) { [BitConverter]::ToUInt64($image, $slot) } else { [BitConverter]::ToUInt32($image, $slot) }
            if (!$nameRva) { break }
            if ($nameRva -gt [int]::MaxValue) { return }
            $name = Get-ImportName $image $slot $nameRva 2 $readOnly $iatEnd
            if (!$name) { return }
            $names.Add($name)
        } while ($true)
    }
    $descriptor = $bias + $importRva + 40
    if ($iat -ne $iatEnd -or ($image[$descriptor..($descriptor + 19)] -ne 0)) { return }
    $names | Sort-Object Start
}

function Get-ImportTailLayout($names, [int] $first, $text, $readOnly, [int] $textSize) {
    $cut = [Math]::Max($names[$first].Start, ($names[0..($first - 1)].End | Measure-Object -Maximum).Maximum)
    $size = $cut - $readOnly.Rva
    $rawSize = [int]([Math]::Ceiling($size / 512.0) * 512)
    [pscustomobject]@{
        Cut = $cut
        Size = $size
        RawSize = $rawSize
        FileSize = [Math]::Min($text.RawSize + $size, $rawSize + $textSize)
    }
}

function Move-ImportNameTail([byte[]] $image, [int] $optional, [int] $directories, [int] $headers, $sections, $readOnly) {
    $reloc = $directories + 40
    $relocRva = [BitConverter]::ToUInt32($image, $reloc)
    $relocSize = [BitConverter]::ToUInt32($image, $reloc + 4)
    $text = $sections[0]
    $table = $text.Header
    $headerEnd = $table + 120
    $headerUsed = $headerEnd
    if ($relocRva -eq $headerEnd) {
        $headerUsed += $relocSize
    } elseif ($relocRva -lt $text.Rva -or $relocRva + $relocSize -gt $text.Rva + $text.Size) {
        return 0
    }
    $width = if ([BitConverter]::ToUInt16($image, $optional) -eq 0x20b) { 8 } else { 4 }
    $names = @(Get-ImportNames $image $directories $readOnly $width)
    if ($names.Count -lt 2 -or $headerUsed -ge $headers -or ($image[$headerUsed..($headers - 1)] -ne 0)) { return 0 }

    $bias = [long]$readOnly.Offset - $readOnly.Rva
    $end = ($names.End | Measure-Object -Maximum).Maximum
    $limit = $readOnly.Rva + $readOnly.Size
    if ($end -gt $limit -or $limit - $end -gt 3 -or
        ($end -lt $limit -and ($image[($bias + $end)..($bias + $limit - 1)] -ne 0))) { return 0 }

    $minimumTable = $table
    $minimumDirectories = [BitConverter]::ToUInt32($image, $directories - 4)
    while ($minimumDirectories -gt 13 -and [BitConverter]::ToUInt64($image, $minimumTable - 8) -eq 0) {
        $minimumTable -= 8
        $minimumDirectories--
    }

    $first = $names.Count - 1
    $capacity = $headers - $headerUsed + $table - $minimumTable
    for (; $first -gt 0; $first--) {
        $start = $names[$first].Start
        # Preserve the Hint/Name word alignment, including a shared preceding terminator.
        if ($end - $start + ($start -band 1) -gt $capacity) { break }
    }
    $first++
    if ($first -ge $names.Count) { return 0 }
    $start = $names[$first].Start
    $layout = Get-ImportTailLayout $names $first $text $readOnly $text.Size

    # If .text has padding left after relocations, it can hold the names just
    # before the header batch. Compare final section order and size both ways.
    $codeFirst = $first
    $candidateEnd = 0
    for ($index = $first - 1; $index -gt 0; $index--) {
        $candidateStart = $names[$index].Start
        $candidateEnd = [Math]::Max($candidateEnd, $names[$index].End)
        $candidateOffset = $text.Size + (($candidateStart -bxor $text.Size) -band 1)
        $candidateSize = $candidateOffset + $candidateEnd - $candidateStart
        if ($candidateSize -gt $text.RawSize) { break }
        $candidate = Get-ImportTailLayout $names $index $text $readOnly $candidateSize
        if ($candidate.FileSize -lt $layout.FileSize) {
            $layout = $candidate
            $codeFirst = $index
            $codeEnd = $candidateEnd
            $codeOffset = $candidateOffset
            $codeSize = $candidateSize
        }
    }
    if ($layout.FileSize -ge [Math]::Min($text.RawSize + $readOnly.Size, $readOnly.RawSize + $text.Size)) { return 0 }

    $covered = $names[$codeFirst].Start
    foreach ($name in $names[$codeFirst..($names.Count - 1)]) {
        if ($name.Start -gt $covered -and ($image[($bias + $covered)..($bias + $name.Start - 1)] -ne 0)) { return 0 }
        $covered = [Math]::Max($covered, $name.End)
    }
    for ($entry = $directories; $entry -lt $minimumTable; $entry += 8) {
        $rva = [BitConverter]::ToUInt32($image, $entry)
        $length = [BitConverter]::ToUInt32($image, $entry + 4)
        if ($length -and $rva -lt $limit -and $rva + $length -gt $layout.Cut) { return 0 }
    }
    $relocOffset = if ($relocRva -lt $headers) { $relocRva } else { $text.Offset + $relocRva - $text.Rva }
    for ($block = $relocOffset; $block -lt $relocOffset + $relocSize; $block += $blockSize) {
        $page = [BitConverter]::ToUInt32($image, $block)
        $blockSize = [BitConverter]::ToUInt32($image, $block + 4)
        if ($blockSize -lt 8 -or $blockSize % 4 -or $block + $blockSize -gt $relocOffset + $relocSize) { return 0 }
        for ($index = $block + 8; $index -lt $block + $blockSize; $index += 2) {
            $fixup = [BitConverter]::ToUInt16($image, $index)
            $rva = $page + ($fixup -band 0xfff)
            if (($fixup -shr 12) -and $rva + $width -gt $layout.Cut -and $rva -lt $limit) { return 0 }
        }
    }

    $shift = 0
    $target = $headerUsed + ($start -band 1)
    while ($target + $end - $start -gt $headers) {
        $shift += 8
        $target -= 8
    }
    if ($shift) {
        [Array]::Copy($image, $table, $image, $table - $shift, $headerUsed - $table)
        [Array]::Clear($image, $headerUsed - $shift, $headers - $headerUsed + $shift)
        foreach ($section in $sections) { $section.Header -= $shift }
        $optionalSize = [BitConverter]::ToUInt16($image, $optional - 4)
        [BitConverter]::GetBytes([uint16]($optionalSize - $shift)).CopyTo($image, $optional - 4)
        $count = [BitConverter]::ToUInt32($image, $directories - 4)
        Write-U32 $image ($directories - 4) ($count - $shift / 8)
        if ($relocRva -eq $headerEnd) { Write-U32 $image $reloc ($relocRva - $shift) }
    }
    [Array]::Copy($image, $bias + $start, $image, $target, $end - $start)
    foreach ($name in $names[$first..($names.Count - 1)]) {
        Write-U32 $image $name.Field ($target + $name.Start - $start)
    }
    if ($codeFirst -lt $first) {
        $codeStart = $names[$codeFirst].Start
        [Array]::Copy($image, $bias + $codeStart, $image, $text.Offset + $codeOffset, $codeEnd - $codeStart)
        foreach ($name in $names[$codeFirst..($first - 1)]) {
            Write-U32 $image $name.Field ($text.Rva + $codeOffset + $name.Start - $codeStart)
        }
        $text.Size = $codeSize
    }
    [Array]::Clear($image, $bias + $layout.Cut, $readOnly.RawSize - $layout.Size)
    $saved = $readOnly.RawSize - $layout.RawSize
    $readOnly.Size = $layout.Size
    $readOnly.RawSize = $layout.RawSize
    $saved
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

    if ($readOnly.Name -ceq '.rdata') {
        $initialized -= Move-ImportNameTail $image $optional $directories $headers $sections $readOnly
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
