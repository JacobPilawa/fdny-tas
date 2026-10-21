# Extracts the downloaded Zig archive into $ZigInstallDir.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$zipFile = Join-Path $DownloadDir $ZigAssetName
if (-not (Test-Path $zipFile)) {
    throw "Zig archive not found: $zipFile. Run get_zig.ps1 first."
}

Expand-ToolZip -ZipPath $zipFile -DestDir $ZigInstallDir

# The archive contains a single top-level zig-x86_64-windows-<version> folder.
$extracted = Get-ChildItem $ZigInstallDir -Directory | Select-Object -First 1
Write-Host "Zig installed: $($extracted.FullName)"
Write-Host "zig.exe: $(Join-Path $extracted.FullName 'zig.exe')"
