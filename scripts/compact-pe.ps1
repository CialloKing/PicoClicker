param(
    [Parameter(Mandatory = $true)]
    [string] $Executable
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

if (($sections.Name -join ',') -cne '.text,.rdata,.data,.rsrc,.reloc') {
    return
}

$rawEnd = $headers
foreach ($section in $sections) {
    if ($section.RawSize -eq 0) {
        if ($section.RawOffset -ne 0) { return }
        continue
    }
    if ($section.RawOffset -ne $rawEnd -or $section.RawSize % 512) { return }
    $rawEnd += $section.RawSize
}
if ($rawEnd -ne $image.Length) { return }

$constants = $sections[1]
$relocations = $sections[4]
$directory = $directories + 5 * 8
$initialized = Read-U32 ($optional + 8)
if ($constants.Flags -ne 0x40000040 -or $relocations.Flags -ne 0x42000040 -or $relocations.RawSize -ne 512 -or
    (Read-U32 ($optional + 56)) -ne $relocations.RVA + 4096 -or
    $initialized -lt $relocations.RawSize -or
    $relocations.RVA -ne (Read-U32 $directory) -or $relocations.Size -ne (Read-U32 ($directory + 4)) -or
    $relocations.Size -lt 8 -or $relocations.Size -gt $relocations.RawSize) {
    return
}

for ($index = 0; $index -lt 16; $index++) {
    if ($index -eq 5) { continue }
    $rva = Read-U32 ($directories + 8 * $index)
    $size = Read-U32 ($directories + 8 * $index + 4)
    if ($rva -ge $relocations.RVA -or $size -gt $relocations.RVA - $rva) { return }
}

# The removed section must contain only relocation blocks for earlier image pages.
for ($index = 0; $index -lt $relocations.Size; $index += $blockSize) {
    $block = $relocations.RawOffset + $index
    $page = Read-U32 $block
    $blockSize = Read-U32 ($block + 4)
    if ($page -lt 4096 -or $page % 4096 -or $page -ge $relocations.RVA -or
        $blockSize -lt 8 -or $blockSize % 4 -or $index + $blockSize -gt $relocations.Size) {
        return
    }
}

# The directory can live in the unused tail of read-only .rdata at a DWORD boundary.
$tableOffset = [int]([Math]::Ceiling($constants.Size / 4.0) * 4)
$tableEnd = $tableOffset + $relocations.Size
if ($tableEnd -gt $constants.RawSize -or $constants.RVA + $tableEnd -gt $sections[2].RVA -or
    $constants.RawOffset -lt $headers -or $constants.RawOffset + $constants.RawSize -gt $relocations.RawOffset) {
    return
}

for ($index = $constants.Size; $index -lt $tableEnd; $index++) {
    if ($image[$constants.RawOffset + $index] -ne 0) {
        return
    }
}

# The remaining sections fit in one aligned header block. Only their file offsets move.
$headerSize = 512
$shift = $headers - $headerSize
[byte[]] $output = [byte[]]::new($relocations.RawOffset - $shift)
[Array]::Copy($image, 0, $output, 0, $headerSize)
[Array]::Copy($image, $headers, $output, $headerSize, $relocations.RawOffset - $headers)
[Array]::Copy($image, $relocations.RawOffset, $output, $constants.RawOffset - $shift + $tableOffset, $relocations.Size)
[Array]::Clear($output, $relocations.Header, $headerSize - $relocations.Header)
[BitConverter]::GetBytes([uint16]4).CopyTo($output, $pe + 6)
Write-U32 ($optional + 8) ($initialized - $relocations.RawSize)
Write-U32 ($optional + 56) $relocations.RVA
Write-U32 ($optional + 60) $headerSize
Write-U32 ($constants.Header + 8) $tableEnd
Write-U32 $directory ($constants.RVA + $tableOffset)
foreach ($section in ($sections | Select-Object -First 4)) {
    if ($section.RawOffset) {
        Write-U32 ($section.Header + 20) ($section.RawOffset - $shift)
    }
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
