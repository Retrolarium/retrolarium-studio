[CmdletBinding()]
param(
    [string]$Bin,
    [string]$Python,
    [string]$Mame,
    [string]$Out,
    # Package a working tree with uncommitted changes (for trying a package
    # out; BUILD-INFO.txt then says so). A package to publish comes from a
    # commit, so its source is exactly what the commit holds.
    [switch]$AllowDirty
)

# Assembles a Retrolarium Studio that runs on a computer with nothing else
# installed: the studio, the kit, a private Python to run the kit and MAME
# to play the games. Writes build\package\RetrolariumStudio, a zip of it and,
# with retrolarium-setup.exe built, the setup program.
#
#   -Bin     the built studio (default build\win-amd64)
#   -Python  a CPython installation to take the private Python from (default:
#            the one `python` on PATH runs); the kit needs no packages
#   -Mame    consoles.exe (default mame_testing\mame\consoles.exe), built by
#            tools\build-mame-docker.ps1 (or tools\build-mame.sh in MSYS2)
#            from the MAME commit named there

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$repositoryRoot = Split-Path $projectRoot -Parent
if (-not $Bin) { $Bin = Join-Path $projectRoot "build\win-amd64" }
if (-not $Out) { $Out = Join-Path $projectRoot "build\package" }
if (-not $Mame) { $Mame = Join-Path $repositoryRoot "mame_testing\mame\consoles.exe" }
if (-not $Python) {
    $Python = (& python -c "import sys; print(sys.base_prefix)").Trim()
}

foreach ($required in @((Join-Path $Bin "retrolarium-studio.exe"), (Join-Path $Python "python.exe"), $Mame)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing $required"
    }
}

# What the package is made from: the studio, kit and core checkouts side by
# side (their own repositories, or folders of one repository).
$sources = [ordered]@{ "Studio" = "retrolarium-studio"; "Kit" = "retrolarium-kit"; "Core" = "retrolarium-core" }
$commits = [ordered]@{}
$changes = @()
foreach ($name in $sources.Keys) {
    $folder = Join-Path $repositoryRoot $sources[$name]
    $commit = & git -C $folder rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw "Not a git checkout: $folder" }
    $commits[$name] = $commit.Trim()
    $changes += & git -C $folder status --porcelain --untracked-files=all -- .
}
$dirty = [bool]$changes
if ($dirty -and -not $AllowDirty) {
    throw "Uncommitted changes in the studio, kit or core; commit them first, or pass -AllowDirty to try a package out:`n$($changes -join "`n")"
}
$mameCommit = "c662f9ca1900ed7462dc4fe8d338a98e8e4ce3c3"

$package = Join-Path $Out "RetrolariumStudio"
if (Test-Path -LiteralPath $package) {
    Remove-Item -LiteralPath $package -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $package | Out-Null

function Copy-Tree([string]$From, [string]$To, [string[]]$SkipDirectories, [string[]]$SkipFiles) {
    New-Item -ItemType Directory -Force -Path $To | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $From -Force) {
        if ($item.PSIsContainer) {
            if ($SkipDirectories -contains $item.Name) { continue }
            Copy-Tree $item.FullName (Join-Path $To $item.Name) $SkipDirectories $SkipFiles
        } else {
            $skip = $false
            foreach ($pattern in $SkipFiles) { if ($item.Name -like $pattern) { $skip = $true } }
            if (-not $skip) { Copy-Item -LiteralPath $item.FullName -Destination $To }
        }
    }
}

# The studio, and in tools\ the helpers it runs: the level writer Flicky's
# builds use, the YM2612 renderer its sound previews play through, and
# rlcore, the core's codecs the kit runs.
Copy-Item -LiteralPath (Join-Path $Bin "retrolarium-studio.exe") -Destination $package
$helpers = Join-Path $package "tools"
New-Item -ItemType Directory -Force -Path $helpers | Out-Null
foreach ($helper in @("retrolarium-flicky-level-writer.exe", "retrolarium-ymfm-renderer.exe", "rlcore.exe")) {
    Copy-Item -LiteralPath (Join-Path $Bin "tools\$helper") -Destination $helpers
}

# The kit, as a plain package next to a launcher, with the assemblers its
# checkout built (retrolarium-kit\tools\build_toolchains.ps1); the package
# carries them, so users need nothing installed.
$toolchainStatus = & python -c "import json, sys; sys.path[:0] = [r'$(Join-Path $repositoryRoot 'retrolarium-kit\src')', r'$(Join-Path $repositoryRoot 'retrolarium-core\python')']; from retrolarium_kit.core.build_toolchains import packaged_toolchain_status as s; print(all(item['available'] for item in s('windows-x86_64')['suites'].values()))"
if ($toolchainStatus -ne "True") {
    throw "The kit's assemblers are missing or differ from its manifest; run retrolarium-kit\tools\build_toolchains.ps1 first."
}
$kit = Join-Path $package "kit"
# Only the Windows toolchains: the package runs on Windows.
Copy-Tree (Join-Path $repositoryRoot "retrolarium-kit\src\retrolarium_kit") (Join-Path $kit "retrolarium_kit") `
    @("__pycache__", "linux-x86_64") @("*.pyc")
# The core package the kit builds on, beside it.
Copy-Tree (Join-Path $repositoryRoot "retrolarium-core\python\retrolarium_core") (Join-Path $kit "retrolarium_core") `
    @("__pycache__") @("*.pyc")
Set-Content -LiteralPath (Join-Path $kit "rltool.py") -Encoding ascii -Value @'
"""Run the Retrolarium Kit that ships with the studio."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

from retrolarium_kit.cli import main  # noqa: E402

if __name__ == "__main__":
    raise SystemExit(main())
'@

# A private Python: the interpreter, its runtime DLLs and the standard
# library without what the kit never uses. The ._pth file isolates it from
# any Python settings on the computer.
$runtime = Join-Path $package "python"
New-Item -ItemType Directory -Force -Path $runtime | Out-Null
$dll = Get-ChildItem -LiteralPath $Python -Filter "python3*.dll" | Where-Object { $_.Name -ne "python3.dll" } | Select-Object -First 1
if (-not $dll) { throw "No python3x.dll in $Python" }
foreach ($file in @("python.exe", "pythonw.exe", "python3.dll", $dll.Name, "vcruntime140.dll", "vcruntime140_1.dll", "LICENSE.txt")) {
    $source = Join-Path $Python $file
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $runtime }
}
Copy-Tree (Join-Path $Python "DLLs") (Join-Path $runtime "DLLs") @() `
    @("_tkinter.pyd", "tcl*.dll", "tk*.dll", "*.ico", "_test*.pyd", "_ctypes_test.pyd", "xxlimited*.pyd")
Copy-Tree (Join-Path $Python "Lib") (Join-Path $runtime "Lib") `
    @("site-packages", "test", "tests", "idlelib", "tkinter", "turtledemo", "ensurepip", "__pycache__", "lib2to3", "pydoc_data") `
    @("*.pyc")
$pth = [System.IO.Path]::GetFileNameWithoutExtension($dll.Name) + "._pth"
Set-Content -LiteralPath (Join-Path $runtime $pth) -Encoding ascii -Value "Lib`r`nDLLs`r`n."

# MAME, with the folder the Disk System BIOS goes into.
$emulator = Join-Path $package "mame"
New-Item -ItemType Directory -Force -Path (Join-Path $emulator "roms\fds") | Out-Null
Copy-Item -LiteralPath $Mame -Destination $emulator
$mameSource = Split-Path $Mame -Parent
foreach ($file in @("COPYING", "README.md")) {
    $source = Join-Path $mameSource $file
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $emulator }
}
# The studio marks MAME's known-problems screen as seen (an embedded MAME
# never gets the key it waits for); MAME honours the mark only with this on.
Set-Content -LiteralPath (Join-Path $emulator "ui.ini") -Encoding ascii -Value "skip_warnings             1"
# MAME's license texts and those of the libraries in its source tree.
$mameLegal = Join-Path $emulator "legal"
if (Test-Path -LiteralPath (Join-Path $mameSource "docs\legal")) {
    Copy-Tree (Join-Path $mameSource "docs\legal") $mameLegal @() @()
    foreach ($library in Get-ChildItem -LiteralPath (Join-Path $mameSource "3rdparty") -Directory) {
        $texts = Get-ChildItem -LiteralPath $library.FullName -File |
            Where-Object { $_.Name -match '^(licen[cs]e|copying)' -or ($library.Name -eq "libjpeg" -and $_.Name -eq "README") }
        foreach ($text in $texts) {
            $target = Join-Path $mameLegal "3rdparty\$($library.Name)"
            New-Item -ItemType Directory -Force -Path $target | Out-Null
            Copy-Item -LiteralPath $text.FullName -Destination $target
        }
    }
} else {
    Write-Warning "No MAME source next to $Mame (docs\legal, 3rdparty): the package is not ready to publish"
}

# Licenses: the studio's own and those of what the package carries
# (THIRD-PARTY.md says which text covers what).
Copy-Item -LiteralPath (Join-Path $projectRoot "LICENSE") -Destination (Join-Path $package "LICENSE.txt")
Copy-Item -LiteralPath (Join-Path $projectRoot "THIRD-PARTY.md") -Destination $package
$licenses = Join-Path $package "licenses"
New-Item -ItemType Directory -Force -Path $licenses | Out-Null
$core = Join-Path $repositoryRoot "retrolarium-core\third_party"
foreach ($pair in @(
        @((Join-Path $projectRoot "resources\fonts\OFL-Barlow.txt"), "OFL-Barlow.txt"),
        @((Join-Path $projectRoot "resources\fonts\OFL-IBMPlex.txt"), "OFL-IBMPlex.txt"),
        @((Join-Path $projectRoot "resources\licenses\Qt-LGPL-3.0.txt"), "Qt-LGPL-3.0.txt"),
        @((Join-Path $projectRoot "resources\licenses\mingw-w64-runtime.txt"), "mingw-w64-runtime.txt"),
        @((Join-Path $projectRoot "resources\licenses\ymfm-BSD-3-Clause.txt"), "ymfm-BSD-3-Clause.txt"))) {
    if (Test-Path -LiteralPath $pair[0] -PathType Leaf) {
        Copy-Item -LiteralPath $pair[0] -Destination (Join-Path $licenses $pair[1])
    } else {
        Write-Warning "Missing license text $($pair[0]): the package is not ready to publish"
    }
}
# The libraries Qt builds into the studio (THIRD-PARTY.md lists them).
$qtThirdParty = Join-Path $licenses "qt-3rdparty"
New-Item -ItemType Directory -Force -Path $qtThirdParty | Out-Null
foreach ($name in @("zlib", "libpng", "libjpeg-turbo", "pcre2", "freetype", "harfbuzz", "double-conversion", "md4c")) {
    $source = Join-Path $projectRoot "resources\licenses\qt-3rdparty\$name.txt"
    if (Test-Path -LiteralPath $source -PathType Leaf) {
        Copy-Item -LiteralPath $source -Destination $qtThirdParty
    } else {
        Write-Warning "Missing license text $($source): the package is not ready to publish"
    }
}

# A smoke check: the private Python runs the kit.
$check = & (Join-Path $runtime "python.exe") (Join-Path $kit "rltool.py") --help 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "The packaged kit does not start: $check"
}
Get-ChildItem -LiteralPath $kit -Recurse -Directory -Filter "__pycache__" | Remove-Item -Recurse -Force

# Bytecode for the standard library, the kit and core. The studio is usually
# installed where Python may not write, so without it every kit call would
# compile the modules it imports again. Hash-checked .pyc files stay valid
# whatever file times the zip or the setup program give the sources.
& (Join-Path $runtime "python.exe") -m compileall -q -j 0 --invalidation-mode checked-hash `
    (Join-Path $runtime "Lib") $kit
if ($LASTEXITCODE -ne 0) {
    throw "Compiling the private Python's and the kit's bytecode failed"
}

# project(VERSION ...), the one place the version is set; not cmake_minimum_required's.
$cmake = Get-Content -LiteralPath (Join-Path $projectRoot "CMakeLists.txt") -Raw
$match = [regex]::Match($cmake, "project\([^)]*?VERSION\s+(\d+\.\d+\.\d+)", "Singleline")
if (-not $match.Success) { throw "No project(VERSION) in CMakeLists.txt" }
$version = $match.Groups[1].Value

# Which sources the package holds, so a binary can be matched to them.
$bios = Get-Content -LiteralPath (Join-Path $kit "retrolarium_kit\core\opendisksys\manifest.json") -Raw | ConvertFrom-Json
$info = @(
    "Retrolarium Studio $version",
    "Packaged: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm')) UTC",
    "Studio: $($commits['Studio'])",
    "Kit: $($commits['Kit'])",
    "Core: $($commits['Core'])",
    "MAME: mamedev/mame $mameCommit (tools/build-mame.sh)",
    "OpenDiskSysROM: $($bios.commit) ($($bios.source))"
)
if ($dirty) { $info += "Built with uncommitted changes (not for publishing)" }
Set-Content -LiteralPath (Join-Path $package "BUILD-INFO.txt") -Encoding ascii -Value $info
$zip = Join-Path $Out "RetrolariumStudio-$version-win64.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -LiteralPath $package -DestinationPath $zip -CompressionLevel Optimal
$size = [math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1)
Write-Host "Packaged: $package"
Write-Host "Archive: $zip ($size MB)"

# The setup program: the installer with the package folder attached, checked
# file by file after it is written.
$stub = Join-Path $Bin "retrolarium-setup.exe"
if (Test-Path -LiteralPath $stub -PathType Leaf) {
    $setup = Join-Path $Out "RetrolariumStudio-$version-setup.exe"
    & $stub --create-package $setup $package $version | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Creating $setup failed with exit code $LASTEXITCODE" }
    & $stub --verify-package $setup | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "$setup does not verify (exit code $LASTEXITCODE)" }
    $size = [math]::Round((Get-Item -LiteralPath $setup).Length / 1MB, 1)
    Write-Host "Setup: $setup ($size MB)"
}
