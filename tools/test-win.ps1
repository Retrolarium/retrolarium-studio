[CmdletBinding()]
param(
    [string]$Bin,
    # The folder holding the game repositories (flicky_src, ...), for tests
    # that read the projects the kit prepared there.
    [string]$GameSources
)

# Runs the test programs the plugins register (tests.json, written by the
# install step of build-win.ps1). Fixture paths are relative to
# retrolarium_studio.

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Bin) {
    $Bin = Join-Path $projectRoot "build\win-amd64"
}
if (-not $GameSources) { $GameSources = $env:RETROLARIUM_GAME_SOURCES }
if (-not $GameSources) { $GameSources = Join-Path $projectRoot "..\..\..\oranguthang" }
$manifest = Join-Path $Bin "tests.json"
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    throw "No tests.json in $($Bin): build the studio first (make build-retrolarium-studio)."
}

$failed = @()
# Windows PowerShell's ConvertFrom-Json hands an array on as one object.
$tests = @((Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json) | ForEach-Object { $_ })
foreach ($test in $tests) {
    $arguments = @($test.arguments | ForEach-Object {
        if ($_ -like "games:*") { Join-Path $GameSources $_.Substring(6) } else { Join-Path $projectRoot $_ }
    })
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
