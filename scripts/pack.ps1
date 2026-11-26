param(
    [string] $VerificationReport
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$reader = (Get-Command llvm-readobj -CommandType Application).Source
$machines = @{
    x86 = 'IMAGE_FILE_MACHINE_I386'
    x64 = 'IMAGE_FILE_MACHINE_AMD64'
}
$targets = @(
    @{ Name = 'x86'; Suffix = ''; MaxSize = 9KB }
    @{ Name = 'x64'; Suffix = ''; MaxSize = 13KB }
    @{ Name = 'x86'; Suffix = '-Lite'; MaxSize = 6KB }
    @{ Name = 'x64'; Suffix = '-Lite'; MaxSize = 7.5KB }
)
$requiredFields = @(
    'IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE'
    'IMAGE_DLL_CHARACTERISTICS_NX_COMPAT'
    'IMAGE_DLL_CHARACTERISTICS_GUARD_CF'
    'CF_INSTRUMENTED'
    'CF_FUNCTION_TABLE_PRESENT'
    'SecurityCookie: 0x[1-9A-F]'
    'BaseRelocationTableSize: 0x[1-9A-F]'
)

$builds = foreach ($target in $targets) {
    $folder = Join-Path $root "build/release-$($target.Name)"
    $executable = Get-Item -LiteralPath (Join-Path $folder "PicoClicker$($target.Suffix).exe")
    $metadata = @(Get-Content -LiteralPath (Join-Path $folder 'release-info.txt'))
    if ($metadata.Count -ne 2 -or $metadata[0] -notmatch '^\d+\.\d+\.\d+$' -or !$metadata[1]) {
        throw "Invalid release metadata in $folder"
    }
    if ($executable.Length -gt $target.MaxSize) {
        throw "$($executable.FullName) is $($executable.Length) bytes. Budget is $($target.MaxSize) bytes."
    }

    $pe = (& $reader --file-headers --coff-imports --coff-load-config $executable.FullName) -join [Environment]::NewLine
    if ($LASTEXITCODE -ne 0 -or $pe -notmatch "Machine: $($machines[$target.Name])\b") {
        throw "Unexpected PE architecture in $($executable.FullName)"
    }
    $imports = @([regex]::Matches($pe, '(?im)^[ \t]+Name:[ \t]+([\w.-]+\.dll)[ \t\r]*$') |
        ForEach-Object { $_.Groups[1].Value } | Sort-Object)
    if (($imports -join ',') -ine 'KERNEL32.dll,USER32.dll') {
        throw "Unexpected imports in $($target.Name) build ($($imports -join ', '))"
    }
    foreach ($field in $requiredFields) {
        if ($pe -notmatch $field) {
            throw "Missing PE protection field in $($target.Name) build ($field)"
        }
    }
    if ($target.Name -eq 'x64' -and $pe -notmatch 'ExceptionTableSize: 0x[1-9A-F]') {
        throw 'x64 unwind data is missing.'
    }

    [pscustomobject]@{
        Name = $target.Name
        Suffix = $target.Suffix
        File = $executable
        Version = $metadata[0]
        Toolchain = $metadata[1]
    }
}

$version = $builds[0].Version
if ($builds | Where-Object Version -ne $version) {
    throw 'All four builds must use the same version.'
}
$sourceCommit = git -C $root rev-parse --verify HEAD
if ($LASTEXITCODE -ne 0) {
    throw 'Could not resolve the source commit.'
}
$report = if ($VerificationReport) {
    [IO.File]::ReadAllText((Resolve-Path -LiteralPath $VerificationReport).Path).Trim()
}

$destination = Join-Path $root "dist/v$version"
[void](New-Item -Path $destination -ItemType Directory -Force)
$notes = @(
    "# PicoClicker v$version"
    ''
    "源码提交 $sourceCommit"
    ''
    '| 版本 | 架构 | 文件 | 字节数 | 工具链 |'
    '| --- | --- | --- | ---: | --- |'
)

$checksums = foreach ($build in $builds) {
    $name = "PicoClicker$($build.Suffix)-$version-$($build.Name).exe"
    $edition = if ($build.Suffix) { '极限精简版' } else { '普通版' }
    $path = Join-Path $destination $name
    Copy-Item -LiteralPath $build.File.FullName -Destination $path -Force
    $notes += "| $edition | $($build.Name) | $name | $($build.File.Length) | $($build.Toolchain) |"
    "$((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant())  $name"
}
$notes += @('', '四份 EXE 均直接导入 Kernel32.dll 和 User32.dll，并保留 ASLR、DEP、CFG 和栈保护。')
if ($report) {
    $notes += @('', $report)
}
[IO.File]::WriteAllLines((Join-Path $destination 'RELEASE_NOTES.md'), [string[]]$notes)
[IO.File]::WriteAllLines((Join-Path $destination 'SHA256SUMS.txt'), [string[]]$checksums)
Get-ChildItem -LiteralPath $destination -File | Select-Object Name, Length
