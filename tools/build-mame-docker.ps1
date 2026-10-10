[CmdletBinding()]
param(
    [string]$Checkout,
    [string]$Out,
    [int]$Jobs = 0
)

# Builds the MAME the studio ships in Docker, with no MSYS2 on the host: the
# image from tools\mame\Dockerfile cross-compiles the "consoles" subtarget
# (NES/Famicom, Vs. System, SNES and Mega Drive drivers) of a MAME checkout at
# the commit tools\mame\build.sh names, unchanged, and writes consoles.exe.
#
#   -Checkout  the MAME checkout (default mame_testing\mame); only read, the
#              build happens in a clone kept in the Docker volume
#              retrolarium-mame-build, so a second run rebuilds what changed
#   -Out       where consoles.exe goes (default: the checkout, where
#              tools\package-win.ps1 takes it from with the license texts)
#   -Jobs      parallel compile jobs (default: every processor Docker has)
#
# tools\build-mame.sh builds the same subtarget natively in MSYS2.

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$repositoryRoot = Split-Path $projectRoot -Parent
if (-not $Checkout) { $Checkout = Join-Path $repositoryRoot "mame_testing\mame" }
$Checkout = (Resolve-Path -LiteralPath $Checkout).Path
if (-not $Out) { $Out = $Checkout }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path -LiteralPath $Out).Path

$image = "retrolarium-mame-build"
# Docker reports progress on stderr, which Windows PowerShell turns into errors
# when the output is redirected; only the exit code says whether it failed.
function Invoke-Docker {
    $ErrorActionPreference = "Continue"
    & docker @args
    if ($LASTEXITCODE -ne 0) { throw "docker $($args[0]) failed with exit code $LASTEXITCODE" }
}

Invoke-Docker build -t $image (Join-Path $PSScriptRoot "mame")

$arguments = @("run", "--rm",
    "-v", "${Checkout}:/source:ro",
    "-v", "retrolarium-mame-build:/build",
    "-v", "${Out}:/out")
if ($Jobs -gt 0) { $arguments += @("-e", "JOBS=$Jobs") }
Invoke-Docker @arguments $image
Write-Host "Wrote $(Join-Path $Out 'consoles.exe')"
