[CmdletBinding()]
param(
    [string]$Bin,
    # The folder holding the game repositories (flicky_src, ...), for tests
    # that read the projects the kit prepared there.
    [string]$GameSources
)

# Runs the test programs the plugins register (tests.json, written by the
# install step of build-win.ps1). Fixture paths are relative to
# retrolarium-studio; games:<repository>/... paths are in the game
# repositories, and a test whose game data is not there is skipped.

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Bin) {
    $Bin = Join-Path $projectRoot "build\win-amd64"
}
if (-not $GameSources) { $GameSources = $env:RETROLARIUM_GAME_SOURCES }
if (-not $GameSources) {
    # An oranguthang folder holding the game repositories, beside one of the
    # folders above the studio (a monorepo checkout or the product repos).
    $folder = Split-Path $projectRoot -Parent
    while ($folder -and -not $GameSources) {
        $candidate = Join-Path (Split-Path $folder -Parent) "oranguthang"
        if (Test-Path -LiteralPath $candidate -PathType Container) { $GameSources = $candidate }
        $parent = Split-Path $folder -Parent
        if ($parent -eq $folder) { break }
        $folder = $parent
    }
    # None found: the tests that need game data skip.
    if (-not $GameSources) { $GameSources = Join-Path $projectRoot "oranguthang-not-found" }
}
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
    $missing = @($test.arguments | Where-Object { $_ -like "games:*" } |
        ForEach-Object { Join-Path $GameSources $_.Substring(6) } |
        Where-Object { -not (Test-Path -LiteralPath $_) })
    if ($missing.Count -gt 0) {
        Write-Host "skipped: no $($missing[0])"
        continue
    }
    & (Join-Path $Bin $test.program) @arguments
    if ($LASTEXITCODE -ne 0) {
        $failed += $test.name
    }
}

# The studio carries no names or tables taken from games: the core's name
# guard checks the repository (the core checkout beside the studio, as the
# build uses it; skipped without Python or that checkout).
$coreModules = Join-Path (Split-Path $projectRoot -Parent) "retrolarium-core\python"
$python = Get-Command python -ErrorAction SilentlyContinue
if ($python -and (Test-Path -LiteralPath (Join-Path $coreModules "retrolarium_core\name_guard.py"))) {
    $savedPath = $env:PYTHONPATH
    $env:PYTHONPATH = $coreModules
    try {
        foreach ($check in @("check", "tables")) {
            Write-Host "== name guard: $check"
            & $python.Source -m retrolarium_core.name_guard $check $projectRoot
            if ($LASTEXITCODE -ne 0) { $failed += "name guard ($check)" }
        }
    } finally {
        $env:PYTHONPATH = $savedPath
    }
} else {
    Write-Host "== name guard: skipped (no Python or no retrolarium-core beside the studio)"
}

if ($failed.Count -gt 0) {
    throw "$($failed.Count) checks failed: $($failed -join ', ')"
}
Write-Host "$($tests.Count) tests and the name guard passed"
