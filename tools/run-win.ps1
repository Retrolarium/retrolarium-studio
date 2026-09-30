[CmdletBinding()]
param(
    [string]$Executable,
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

Start-Process -FilePath (Resolve-Path -LiteralPath $Executable).Path `
    -ArgumentList "--theme", $Theme -WorkingDirectory $projectRoot
