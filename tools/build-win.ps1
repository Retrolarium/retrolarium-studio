[CmdletBinding()]
param(
    [string]$QtKit,
    [string]$Image = "qt5-win-amd64-gpl-enabled:latest",
    [ValidateRange(1, 8)]
    [int]$BuildJobs = 2,
    [string]$MemoryLimit = "3g",
    [double]$CpuLimit = 2,
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release"
)

# Cross-compiles Retrolarium Studio with the static Qt 5 MinGW SDK from
# Longreach Qt inside Docker and installs it into build\win-amd64: the app,
# the helper programs the plugins install, their tests and tests.json.

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $QtKit) {
    # The repository sits two folders below the one holding longreach-qt.
    $QtKit = Join-Path (Split-Path (Split-Path $projectRoot -Parent) -Parent) "longreach-qt"
}
$QtKit = (Resolve-Path -LiteralPath $QtKit).Path

$toolchain = Join-Path $QtKit "cmake\qt5\toolchain-win-amd64.cmake"
if (-not (Test-Path -LiteralPath $toolchain -PathType Leaf)) {
    throw "Qt 5 Windows toolchain not found: $toolchain"
}

$outputRoot = Join-Path $projectRoot "build\win-amd64"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

# The studio builds retrolarium_core from the sibling directory, so the
# repository root is mounted and the studio is configured from inside it.
$repositoryRoot = (Resolve-Path (Join-Path $projectRoot "..")).Path
$sourceMount = $repositoryRoot -replace "\\", "/"
$outputMount = $outputRoot -replace "\\", "/"
$cmakeMount = (Join-Path $QtKit "cmake") -replace "\\", "/"

# The image entrypoint re-parses this script, so keep it free of quoting
# subtleties and exit on the first failure.
$buildScript = @'
set -eu
mkdir -p /out/.cmake-build
cmake -S /src/retrolarium_studio -B /out/.cmake-build \
    -DCMAKE_BUILD_TYPE=$STUDIO_CONFIGURATION \
    -DCMAKE_TOOLCHAIN_FILE=/kit-cmake/qt5/toolchain-win-amd64.cmake \
    -DCMAKE_PREFIX_PATH=/usr/local/Qt-5.15
cmake --build /out/.cmake-build --parallel $STUDIO_BUILD_JOBS -- -k
cmake --install /out/.cmake-build --prefix /out
x86_64-w64-mingw32-objdump -p /out/retrolarium-studio.exe | grep 'DLL Name' | sort -u
'@

docker run --rm `
    --memory $MemoryLimit `
    --memory-swap $MemoryLimit `
    --cpus $CpuLimit `
    --pids-limit 256 `
    --env "STUDIO_BUILD_JOBS=$BuildJobs" `
    --env "STUDIO_CONFIGURATION=$Configuration" `
    --mount "type=bind,source=$sourceMount,target=/src,readonly" `
    --mount "type=bind,source=$outputMount,target=/out" `
    --mount "type=bind,source=$cmakeMount,target=/kit-cmake,readonly" `
    $Image $buildScript

if ($LASTEXITCODE -ne 0) {
    throw "Retrolarium Studio build failed with exit code $LASTEXITCODE"
}

Write-Host "Built: $(Join-Path $outputRoot 'retrolarium-studio.exe')"
