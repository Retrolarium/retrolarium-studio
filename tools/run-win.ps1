[CmdletBinding()]
param(
    [string]$Executable,
    # A project (.rlj) to open; without one the studio starts empty.
    [string]$Project,
    [ValidateSet("dark", "light")]
    [string]$Theme = "dark"
)

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Executable) {
    $Executable = Join-Path $projectRoot "build\win-amd64\retrolarium-studio.exe"
}
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Executable not found: $Executable. Run tools\build-win.ps1 first."
}

$arguments = @("--theme", $Theme)
if ($Project) {
    $arguments = @("`"$((Resolve-Path -LiteralPath $Project).Path)`"") + $arguments
}
Start-Process -FilePath (Resolve-Path -LiteralPath $Executable).Path `
    -ArgumentList $arguments -WorkingDirectory $projectRoot
