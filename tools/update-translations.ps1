[CmdletBinding()]
param(
    [string]$Image = "qt5-win-amd64-gpl-enabled:latest",
    # Interface languages to start: a .ts for each comes in with every text
    # unfinished.
    [string[]]$Add = @()
)

# Refreshes resources\i18n\*.ts from the sources with the build image's
# lupdate: new texts come in unfinished, texts no longer in the code go.
# rlTr() is read as translate(); see src\sdk\i18n.hpp.

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$mount = $projectRoot -replace "\\", "/"
$existing = Get-ChildItem -LiteralPath (Join-Path $projectRoot "resources\i18n") -Filter "*.ts" |
    ForEach-Object { $_.BaseName -replace "^retrolarium_studio_", "" }
foreach ($language in @($existing) + $Add | Sort-Object -Unique) {
    $ts = "retrolarium_studio_$language.ts"
    # English is the source text: its file holds only the plural forms.
    $pluralOnly = if ($language -eq "en") { "-pluralonly " } else { "" }
    $script = "cd /studio && /usr/local/Qt-5.15/bin/lupdate -tr-function-alias translate+=rlTr $pluralOnly" +
              "-locations none -no-obsolete -target-language $language src plugins installer/src " +
              "-ts resources/i18n/$ts"
    docker run --rm --entrypoint sh --mount "type=bind,source=$mount,target=/studio" $Image -c $script
    if ($LASTEXITCODE -ne 0) {
        throw "lupdate failed for $ts with exit code $LASTEXITCODE"
    }
}
