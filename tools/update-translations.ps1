[CmdletBinding()]
param(
    [string]$Image = "qt5-win-amd64-gpl-enabled:latest"
)

# Refreshes resources\i18n\*.ts from the sources with the build image's
# lupdate: new texts come in unfinished, texts no longer in the code go.
# rlTr() is read as translate(); see src\sdk\i18n.hpp.

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$mount = $projectRoot -replace "\\", "/"
$translations = Get-ChildItem -LiteralPath (Join-Path $projectRoot "resources\i18n") -Filter "*.ts"
foreach ($ts in $translations) {
    $language = $ts.BaseName -replace "^retrolarium_studio_", ""
    $script = "cd /studio && /usr/local/Qt-5.15/bin/lupdate -tr-function-alias translate+=rlTr " +
              "-locations none -no-obsolete -target-language $language src plugins " +
              "-ts resources/i18n/$($ts.Name)"
    docker run --rm --entrypoint sh --mount "type=bind,source=$mount,target=/studio" $Image -c $script
    if ($LASTEXITCODE -ne 0) {
        throw "lupdate failed for $($ts.Name) with exit code $LASTEXITCODE"
    }
}
