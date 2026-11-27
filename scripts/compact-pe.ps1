param(
    [Parameter(Mandatory = $true)]
    [string] $Executable,
    [switch] $AllowResourceGrowth
)

$ErrorActionPreference = 'Stop'
$path = (Get-Item -LiteralPath $Executable).FullName
[byte[]] $image = [IO.File]::ReadAllBytes($path)

function Read-U16([int] $offset) {
    [BitConverter]::ToUInt16($image, $offset)
}

function Read-U32([int] $offset) {
    [BitConverter]::ToUInt32($image, $offset)
}

function Write-U32([int] $offset, [uint32] $value) {
    [BitConverter]::GetBytes($value).CopyTo($output, $offset)
}

function Get-ResourceLeaves($section) {
    # LLD emits a type, ID and language level. Only the leaf payload RVAs change.
    if ($section.Size -lt 24) { return }
    $types = Read-U16 ($section.RawOffset + 14)
    if ($types -ne 2 -and ($types -ne 1 -or (Read-U32 ($section.RawOffset + 16)) -ne 24)) {
        return
    }
    $nodes = @(0)
    for ($depth = 0; $depth -lt 3; $depth++) {
        $count = if ($depth -eq 0) { $types } else { 1 }
        $next = foreach ($node in $nodes) {
            if ($node % 4 -or $node + 16 + 8 * $count -gt $section.Size) { return }
            $offset = $section.RawOffset + $node
            if ((Read-U16 ($offset + 12)) -ne 0 -or (Read-U16 ($offset + 14)) -ne $count) { return }
            for ($index = 0; $index -lt $count; $index++) {
                $entry = $offset + 16 + $index * 8
                $child = Read-U32 ($entry + 4)
                if ((Read-U32 $entry) -gt 0xffff -or (($child -shr 31) -ne 0) -ne ($depth -lt 2)) { return }
                $child -band 0x7fffffff
            }
        }
        $nodes = @($next)
    }

    if ($types -eq 2 -and $nodes[0] -eq $nodes[1]) { return }
    $payloadStart = [Math]::Max($nodes[0], $nodes[-1]) + 16
    if ($payloadStart -gt $section.Size) { return }
    foreach ($node in $nodes) {
        if ($node % 4) { return }
        $offset = $section.RawOffset + $node
        $rva = Read-U32 $offset
        $size = Read-U32 ($offset + 4)
        if ($rva -lt $section.RVA + $payloadStart -or $rva + $size -gt $section.RVA + $section.Size -or $size -eq 0) {
            return
        }
    }
    $nodes
}

# Only the unsigned five-section layout produced by the release linker is eligible.
$pe = 64
$optional = $pe + 24
if ($image.Length -lt 512 -or (Read-U16 0) -ne 0x5A4D -or
    (Read-U32 60) -ne $pe -or (Read-U32 $pe) -ne 0x4550 -or
    (Read-U16 ($pe + 6)) -ne 5 -or (Read-U32 ($pe + 12)) -ne 0 -or
    (Read-U32 ($pe + 16)) -ne 0 -or (Read-U32 ($optional + 64)) -ne 0) {
    return
}

$machine = Read-U16 ($pe + 4)
$optionalSize = Read-U16 ($pe + 20)
$magic = Read-U16 $optional
if (($machine -ne 0x14C -or $magic -ne 0x10B -or $optionalSize -ne 224) -and
    ($machine -ne 0x8664 -or $magic -ne 0x20B -or $optionalSize -ne 240)) {
    return
}

$table = $optional + $optionalSize
$directories = $table - 128
$headers = Read-U32 ($optional + 60)
if ($headers -notin 512, 1024 -or $table + 200 -gt $headers -or $headers -gt $image.Length -or
    (Read-U32 ($optional + 32)) -ne 4096 -or (Read-U32 ($optional + 36)) -ne 512 -or
    (Read-U32 ($directories - 4)) -ne 16) {
    return
}

foreach ($directory in 4, 6) {
    if ((Read-U32 ($directories + 8 * $directory)) -ne 0 -or
        (Read-U32 ($directories + 8 * $directory + 4)) -ne 0) {
        return
    }
}

$sections = for ($index = 0; $index -lt 5; $index++) {
    $start = $table + 40 * $index
    [pscustomobject]@{
        Header = $start
        Name = [Text.Encoding]::ASCII.GetString($image, $start, 8).TrimEnd([char]0)
        Size = Read-U32 ($start + 8)
        RVA = Read-U32 ($start + 12)
        RawSize = Read-U32 ($start + 16)
        RawOffset = Read-U32 ($start + 20)
        Flags = Read-U32 ($start + 36)
    }
    if ((Read-U32 ($start + 24)) -ne 0 -or (Read-U32 ($start + 28)) -ne 0) {
        return
    }
}

$layout = $sections.Name -join ','
$packResources = $layout -ceq '.text,.data,.rpack,.rsrc,.reloc'
if (!$packResources -and $layout -cne '.text,.rdata,.data,.rsrc,.reloc') {
    return
}

$rawEnd = $headers
$virtualEnd = 4096
foreach ($section in $sections) {
    if ($section.RVA -ne $virtualEnd -or !$section.Size) { return }
    $virtualEnd += [Math]::Ceiling($section.Size / 4096.0) * 4096
    if ($section.RawSize -eq 0) {
        if ($section.RawOffset -ne 0 -or $section.Name -cne '.data') { return }
        continue
    }
    if ($section.RawOffset -ne $rawEnd -or $section.RawSize -ne [Math]::Ceiling($section.Size / 512.0) * 512) { return }
    $rawEnd += $section.RawSize
}
if ($rawEnd -ne $image.Length -or (Read-U32 ($optional + 56)) -ne $virtualEnd) { return }

$constants = if ($packResources) { $sections[2] } else { $sections[1] }
$resources = $sections[3]
$relocations = $sections[4]
$keptCount = if ($packResources) { 3 } else { 4 }
$removed = $sections[$keptCount]
$resourceDirectory = $directories + 2 * 8
$relocDirectory = $directories + 5 * 8
$initialized = Read-U32 ($optional + 8)
if ($constants.Flags -ne 0x40000040 -or $resources.Flags -ne 0x40000040 -or
    $relocations.Flags -ne 0x42000040 -or $relocations.RawSize -ne 512 -or
    $initialized -lt $rawEnd - $removed.RawOffset -or
    $relocations.RVA -ne (Read-U32 $relocDirectory) -or
    $relocations.Size -ne (Read-U32 ($relocDirectory + 4)) -or $relocations.Size -lt 8) {
    return
}

for ($index = 0; $index -lt 16; $index++) {
    if ($index -eq 5 -or ($packResources -and $index -eq 2)) { continue }
    $rva = Read-U32 ($directories + 8 * $index)
    $size = Read-U32 ($directories + 8 * $index + 4)
    if ($rva -ge $removed.RVA -or $size -gt $removed.RVA - $rva) { return }
}

# Every relocation block must still target a page in a retained section.
for ($index = 0; $index -lt $relocations.Size; $index += $blockSize) {
    $block = $relocations.RawOffset + $index
    $page = Read-U32 $block
    $blockSize = Read-U32 ($block + 4)
    if ($page -lt 4096 -or $page % 4096 -or $page -ge $removed.RVA -or
        $blockSize -lt 8 -or $blockSize % 4 -or $index + $blockSize -gt $relocations.Size) {
        return
    }
}

# Keep the directory DWORD-aligned. The experimental resource section is the last
# retained section, so its raw data can grow within its original virtual pages.
$tableOffset = [int]([Math]::Ceiling($constants.Size / 4.0) * 4)
$tableEnd = $tableOffset + $relocations.Size
$growth = 0
if ($tableEnd -gt $constants.RawSize) {
    if (!$AllowResourceGrowth -or !$packResources -or $tableEnd -gt $removed.RVA - $constants.RVA) { return }
    $growth = [int]([Math]::Ceiling($tableEnd / 512.0) * 512) - $constants.RawSize
}

for ($index = $constants.Size; $index -lt [Math]::Min($tableEnd, $constants.RawSize); $index++) {
    if ($image[$constants.RawOffset + $index] -ne 0) {
        return
    }
}

if ($packResources) {
    if ($constants.Size -lt 8 -or (Read-U32 $constants.RawOffset) -ne 0x4f434950 -or
        $resources.RVA -ne (Read-U32 $resourceDirectory) -or
        $resources.Size -ne (Read-U32 ($resourceDirectory + 4))) { return }
    $capacity = Read-U32 ($constants.RawOffset + 4)
    if ($capacity -lt $resources.Size -or $capacity -gt $constants.Size -or $capacity % 8) { return }
    for ($index = 8; $index -lt $capacity; $index++) {
        if ($image[$constants.RawOffset + $index] -ne 0) { return }
    }
    $resourceLeaves = @(Get-ResourceLeaves $resources)
    if (!$resourceLeaves.Count) { return }
}

# The retained sections fit in one header block and keep their original RVAs.
$headerSize = 512
$shift = $headers - $headerSize
$constantStart = $constants.RawOffset - $shift
[byte[]] $output = [byte[]]::new($removed.RawOffset - $shift + $growth)
[Array]::Copy($image, 0, $output, 0, $headerSize)
[Array]::Copy($image, $headers, $output, $headerSize, $removed.RawOffset - $headers)
[Array]::Copy($image, $relocations.RawOffset, $output, $constantStart + $tableOffset, $relocations.Size)
[Array]::Clear($output, $removed.Header, $headerSize - $removed.Header)
[BitConverter]::GetBytes([uint16]$keptCount).CopyTo($output, $pe + 6)
Write-U32 ($optional + 8) ($initialized - ($rawEnd - $removed.RawOffset) + $growth)
Write-U32 ($optional + 56) $removed.RVA
Write-U32 ($optional + 60) $headerSize
Write-U32 ($constants.Header + 8) $tableEnd
Write-U32 ($constants.Header + 16) ($constants.RawSize + $growth)
Write-U32 $relocDirectory ($constants.RVA + $tableOffset)
foreach ($section in ($sections | Select-Object -First $keptCount)) {
    if ($section.RawOffset) {
        Write-U32 ($section.Header + 20) ($section.RawOffset - $shift)
    }
}

if ($packResources) {
    # Put the resource directory at the start of a named .rsrc section for SDK tools.
    [Array]::Copy($image, $resources.RawOffset, $output, $constantStart, $resources.Size)
    foreach ($leaf in $resourceLeaves) {
        $payload = Read-U32 ($resources.RawOffset + $leaf)
        Write-U32 ($constantStart + $leaf) ($constants.RVA + ($payload - $resources.RVA))
    }
    [Array]::Clear($output, $constants.Header, 8)
    [Text.Encoding]::ASCII.GetBytes('.rsrc').CopyTo($output, $constants.Header)
    Write-U32 $resourceDirectory $constants.RVA
}

$temporary = "$path.$([Guid]::NewGuid().ToString('N')).tmp"
try {
    [IO.File]::WriteAllBytes($temporary, $output)
    [IO.File]::Replace($temporary, $path, [NullString]::Value)
} finally {
    if ([IO.File]::Exists($temporary)) {
        [IO.File]::Delete($temporary)
    }
}
Write-Output ("{0} {1} -> {2} bytes" -f [IO.Path]::GetFileName($path), $image.Length, $output.Length)
