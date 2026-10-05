[CmdletBinding()]
param(
    [string]$Bin
)

# Runs the test programs the plugins register (tests.json, written by the
# install step of build-win.ps1). Fixture paths are relative to
# retrolarium_studio.

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Bin) {
    $Bin = Join-Path $projectRoot "build\win-amd64"
}
$manifest = Join-Path $Bin "tests.json"
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    throw "No tests.json in $($Bin): build the studio first (make build-retrolarium-studio)."
}

$failed = @()
# Windows PowerShell's ConvertFrom-Json hands an array on as one object.
$tests = @((Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json) | ForEach-Object { $_ })
foreach ($test in $tests) {
    $arguments = @($test.arguments | ForEach-Object { Join-Path $projectRoot $_ })
    Write-Host "== $($test.name)"
    & (Join-Path $Bin $test.program) @arguments
    if ($LASTEXITCODE -ne 0) {
        $failed += $test.name
    }
}

if ($failed.Count -gt 0) {
    throw "$($failed.Count) of $($tests.Count) tests failed: $($failed -join ', ')"
}
Write-Host "$($tests.Count) tests passed"
