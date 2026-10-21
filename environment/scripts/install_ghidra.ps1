# Extracts the downloaded Ghidra archive into $GhidraInstallDir.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$zipFile = Join-Path $DownloadDir $GhidraAssetName
if (-not (Test-Path $zipFile)) {
    throw "Ghidra archive not found: $zipFile. Run get_ghidra.ps1 first."
}

Expand-ToolZip -ZipPath $zipFile -DestDir $GhidraInstallDir

# The archive contains a single top-level ghidra_<version>_PUBLIC folder.
$extracted = Get-ChildItem $GhidraInstallDir -Directory | Select-Object -First 1
Write-Host "Ghidra installed: $($extracted.FullName)"
