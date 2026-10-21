# Extracts the downloaded Packer archive into $PackerInstallDir.
# Unlike Ghidra/JDK, the zip has no top-level version folder — it's just
# packer.exe at the root.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$zipFile = Join-Path $DownloadDir $PackerAssetName
if (-not (Test-Path $zipFile)) {
    throw "Packer archive not found: $zipFile. Run get_packer.ps1 first."
}

Expand-ToolZip -ZipPath $zipFile -DestDir $PackerInstallDir

Write-Host "Packer installed: $(Join-Path $PackerInstallDir 'packer.exe')"
