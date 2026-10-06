[CmdletBinding()]
param(
    [string]$Bin,
    [string]$Python,
    [string]$Mame,
    [string]$Out
)

# Assembles a Retrolarium Studio that runs on a computer with nothing else
# installed: the studio, the kit, a private Python to run the kit and MAME
# to play the games. Writes build\package\RetrolariumStudio, a zip of it and,
# with retrolarium-setup.exe built, the setup program.
#
#   -Bin     the built studio (default build\win-amd64)
#   -Python  a CPython installation to take the private Python from (default:
#            the one `python` on PATH runs); the kit needs no packages
#   -Mame    consoles.exe (default mame_testing\mame\consoles.exe)

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

# The studio and the helper its Flicky builds run.
foreach ($program in @("retrolarium-studio.exe", "retrolarium-flicky-level-writer.exe")) {
    Copy-Item -LiteralPath (Join-Path $Bin $program) -Destination $package
}

# The kit, as a plain package next to a launcher.
$kit = Join-Path $package "kit"
Copy-Tree (Join-Path $repositoryRoot "retrolarium_kit\src\retrolarium_kit") (Join-Path $kit "retrolarium_kit") `
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
Copy-Tree (Join-Path $Python "DLLs") (Join-Path $runtime "DLLs") @() @("_tkinter.pyd", "tcl*.dll", "tk*.dll", "*.ico")
Copy-Tree (Join-Path $Python "Lib") (Join-Path $runtime "Lib") `
    @("site-packages", "test", "tests", "idlelib", "tkinter", "turtledemo", "ensurepip", "__pycache__", "lib2to3", "pydoc_data") `
    @("*.pyc")
$pth = [System.IO.Path]::GetFileNameWithoutExtension($dll.Name) + "._pth"
Set-Content -LiteralPath (Join-Path $runtime $pth) -Encoding ascii -Value "Lib`r`nDLLs`r`n."

# MAME, with the folder the Disk System BIOS goes into.
$emulator = Join-Path $package "mame"
New-Item -ItemType Directory -Force -Path (Join-Path $emulator "roms\fds") | Out-Null
Copy-Item -LiteralPath $Mame -Destination $emulator
foreach ($file in @("COPYING", "README.md")) {
    $source = Join-Path (Split-Path $Mame -Parent) $file
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $emulator }
}

# Licenses: the studio's own and those of what the package carries
# (THIRD-PARTY.md says which text covers what).
Copy-Item -LiteralPath (Join-Path $projectRoot "LICENSE") -Destination (Join-Path $package "LICENSE.txt")
Copy-Item -LiteralPath (Join-Path $projectRoot "THIRD-PARTY.md") -Destination $package
$licenses = Join-Path $package "licenses"
New-Item -ItemType Directory -Force -Path $licenses | Out-Null
$core = Join-Path $repositoryRoot "retrolarium_core\third_party"
foreach ($pair in @(
        @((Join-Path $projectRoot "resources\fonts\OFL-Barlow.txt"), "OFL-Barlow.txt"),
        @((Join-Path $projectRoot "resources\fonts\OFL-IBMPlex.txt"), "OFL-IBMPlex.txt"),
        @((Join-Path $core "toml11\LICENSE"), "toml11-MIT.txt"),
        @((Join-Path $core "nlohmann\LICENSE.MIT"), "nlohmann-json-MIT.txt"),
        @((Join-Path $projectRoot "resources\licenses\Qt-LGPL-3.0.txt"), "Qt-LGPL-3.0.txt"))) {
    if (Test-Path -LiteralPath $pair[0] -PathType Leaf) {
        Copy-Item -LiteralPath $pair[0] -Destination (Join-Path $licenses $pair[1])
    } else {
        Write-Warning "Missing license text $($pair[0]): the package is not ready to publish"
    }
}

# A smoke check: the private Python runs the kit.
$check = & (Join-Path $runtime "python.exe") (Join-Path $kit "rltool.py") --help 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "The packaged kit does not start: $check"
}
Get-ChildItem -LiteralPath $kit -Recurse -Directory -Filter "__pycache__" | Remove-Item -Recurse -Force

# project(VERSION ...), the one place the version is set; not cmake_minimum_required's.
$cmake = Get-Content -LiteralPath (Join-Path $projectRoot "CMakeLists.txt") -Raw
$match = [regex]::Match($cmake, "project\([^)]*?VERSION\s+(\d+\.\d+\.\d+)", "Singleline")
if (-not $match.Success) { throw "No project(VERSION) in CMakeLists.txt" }
$version = $match.Groups[1].Value
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
