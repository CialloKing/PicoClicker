param(
    [string] $VerificationReport,
    [switch] $IncludeExperimental
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
if ($IncludeExperimental) {
    $targets += @(
        @{ Name = 'x86'; Suffix = '-Experimental'; MaxSize = 6KB }
        @{ Name = 'x64'; Suffix = '-Experimental'; MaxSize = 7.5KB }
    )
}
$requiredFields = @(
    'IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE'
    'IMAGE_DLL_CHARACTERISTICS_NX_COMPAT'
    'IMAGE_DLL_CHARACTERISTICS_GUARD_CF'
    'CF_INSTRUMENTED'
    'CF_FUNCTION_TABLE_PRESENT'
    'SecurityCookie: 0x[1-9A-F]'
    'BaseRelocationTableSize: 0x[1-9A-F]'
)

$staging = [IO.Directory]::CreateDirectory((Join-Path ([IO.Path]::GetTempPath()) "PicoClicker-pack-$([Guid]::NewGuid().ToString('N'))"))
try {
    $builds = foreach ($target in $targets) {
        $folder = Join-Path $root "build/release-$($target.Name)"
        $inputFile = Join-Path $folder "PicoClicker$($target.Suffix).exe"
        $experimental = $target.Suffix -eq '-Experimental'
        $metadata = @(Get-Content -LiteralPath (Join-Path $folder 'release-info.txt'))
        if ($metadata.Count -ne 2 -or $metadata[0] -notmatch '^\d+\.\d+\.\d+$' -or !$metadata[1]) {
            throw "Invalid release metadata in $folder"
        }
        $stagedPath = Join-Path $staging.FullName "PicoClicker$($target.Suffix)-$($target.Name).exe"
        Copy-Item -LiteralPath $inputFile -Destination $stagedPath
        if (!$experimental) {
            & (Join-Path $PSScriptRoot 'compact-pe.ps1') -Executable $stagedPath | Out-Null
        }
        $executable = Get-Item -LiteralPath $stagedPath
        if ($executable.Length -gt $target.MaxSize) {
            throw "$($executable.FullName) is $($executable.Length) bytes. Budget is $($target.MaxSize) bytes."
        }

        $inspectedImage = $executable.FullName
        if ($experimental) {
            # LLVM's reader expects the relocation RVA to be inside a section. Verify
            # the staged bytes by reproducing them from the linked image that LLVM reads.
            $inspectedImage = Join-Path $folder 'PicoClicker-Experimental-linked.exe'
            $repacked = Join-Path $staging.FullName "repacked-$($target.Name).exe"
            & (Join-Path $PSScriptRoot 'compact-experimental-pe.ps1') -Executable $inspectedImage -OutputFile $repacked | Out-Null
            if ((Get-FileHash -LiteralPath $repacked).Hash -ne (Get-FileHash -LiteralPath $stagedPath).Hash) {
                throw "$($executable.Name) differs from the linked image after packing. Rebuild the experimental target."
            }
        }
        $pe = (& $reader --file-headers --coff-imports --coff-load-config --coff-resources $inspectedImage) -join [Environment]::NewLine
        if ($LASTEXITCODE -ne 0 -or $pe -notmatch "Machine: $($machines[$target.Name])\b") {
            throw "Unexpected PE architecture in $($executable.FullName)"
        }
        $imports = @([regex]::Matches($pe, '(?im)^[ \t]+Name:[ \t]+([\w.-]+\.dll)[ \t\r]*$') |
            ForEach-Object { $_.Groups[1].Value } | Sort-Object)
        if (($imports -join ',') -ine 'KERNEL32.dll,USER32.dll') {
            throw "Unexpected imports in $($target.Name) build ($($imports -join ', '))"
        }
        if ($experimental -and [regex]::Matches($pe, '(?m)^\s*ImportLookupTableRVA: 0x0\s*$').Count -ne 2) {
            throw "$($executable.Name) does not use the shared import table."
        }
        foreach ($field in $requiredFields) {
            if ($pe -notmatch $field) {
                throw "Missing PE protection field in $($target.Name) build ($field)"
            }
        }
        if ($target.Name -eq 'x64' -and $pe -notmatch 'ExceptionTableSize: 0x[1-9A-F]') {
            throw 'x64 unwind data is missing.'
        }
        if ($target.Name -eq 'x64' -and $pe -match 'LoadConfigTableSize: 0xC0\b') {
            # EH continuation, XFG, CastGuard and GuardMemcpy need fields beyond the CFG prefix.
            $flags = [regex]::Match($pe, 'GuardFlags \[ \(0x([0-9A-Fa-f]+)\)')
            if (!$flags.Success -or ([Convert]::ToUInt32($flags.Groups[1].Value, 16) -band 0x03C00000)) {
                throw "$($executable.Name) needs the full load config for its guard flags."
            }
            $mapSuffix = if ($experimental) { '-Experimental-linked' } else { $target.Suffix }
            $map = Join-Path $folder "PicoClicker$mapSuffix.map"
            $tailSlots = '\b__(guard_(memcpy_fptr|xfg_\w+_fptr)|castguard_check_failure_os_handled_fptr)\b'
            if (Select-String -LiteralPath $map -Pattern $tailSlots -Quiet) {
                throw "$($executable.Name) links guard slots outside its compact load config."
            }
        }
        if ($pe -notmatch 'Type: MANIFEST \(ID 24\)') {
            throw "Missing manifest resource in $($executable.Name)."
        }
        if ($experimental) {
            if ($pe -notmatch 'Symbol: CreateDialogIndirectParamA\b' -or $pe -match 'Type: DIALOG \(ID 5\)') {
                throw "$($executable.Name) does not use an embedded dialog template."
            }
        } elseif ($pe -notmatch 'Type: DIALOG \(ID 5\)') {
            throw "Missing dialog resource in $($executable.Name)."
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
        throw 'All builds must use the same version.'
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
        $edition = switch ($build.Suffix) {
            '-Experimental' { '实验性极简版' }
            '-Lite' { '极限精简版' }
            default { '普通版' }
        }
        $path = Join-Path $destination $name
        Copy-Item -LiteralPath $build.File.FullName -Destination $path -Force
        $notes += "| $edition | $($build.Name) | $name | $($build.File.Length) | $($build.Toolchain) |"
        "$((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant())  $name"
    }
    $notes += @('', '各 EXE 均直接导入 Kernel32.dll 和 User32.dll，并保留 ASLR、DEP、CFG 和栈保护。')
    if ($IncludeExperimental) {
        $notes += @('', '文件名带 Experimental 的构建采用实验性 PE 布局。需要兼容常规 PE 工具或其他 Windows 版本时，请选用普通版或 Lite。')
    }
    if ($report) {
        $notes += @('', $report)
    }
    [IO.File]::WriteAllLines((Join-Path $destination 'RELEASE_NOTES.md'), [string[]]$notes)
    [IO.File]::WriteAllLines((Join-Path $destination 'SHA256SUMS.txt'), [string[]]$checksums)
    Get-ChildItem -LiteralPath $destination -File | Select-Object Name, Length
} finally {
    foreach ($stagedFile in $staging.GetFiles()) {
        $stagedFile.Delete()
    }
    $staging.Delete()
}
