# Extracts the downloaded GitHub CLI archive into $GhInstallDir. Login is separate and
# done by the owner (see the GitHub CLI notes in environment/config.ps1).

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$zipFile = Join-Path $DownloadDir $GhAssetName
if (-not (Test-Path $zipFile)) {
    throw "GitHub CLI archive not found: $zipFile. Run get_gh.ps1 first."
}

Expand-ToolZip -ZipPath $zipFile -DestDir $GhInstallDir

if (-not (Test-Path $GhExe)) {
    # Older archives nested everything in a version-named folder.
    $found = Get-ChildItem $GhInstallDir -Recurse -Filter gh.exe | Select-Object -First 1
    if (-not $found) { throw "gh.exe not found under $GhInstallDir after extraction" }
    throw "gh.exe extracted to $($found.FullName), not ${GhExe}: update `$GhExe in config.ps1"
}

Write-Host "GitHub CLI installed: $GhExe ($(& $GhExe --version | Select-Object -First 1))"
