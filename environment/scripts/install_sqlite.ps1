# Extracts the downloaded sqlite3 archive into $SqliteInstallDir.
# Like Packer's zip, this one has no top-level version folder.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$zipFile = Join-Path $DownloadDir $SqliteAssetName
if (-not (Test-Path $zipFile)) {
    throw "sqlite3 archive not found: $zipFile. Run get_sqlite.ps1 first."
}

Expand-ToolZip -ZipPath $zipFile -DestDir $SqliteInstallDir

Write-Host "sqlite3 installed: $(Join-Path $SqliteInstallDir 'sqlite3.exe')"
