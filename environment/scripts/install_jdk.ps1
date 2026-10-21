# Extracts the downloaded JDK archive into $JdkInstallDir.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$zipFile = Join-Path $DownloadDir $JdkAssetName
if (-not (Test-Path $zipFile)) {
    throw "JDK archive not found: $zipFile. Run get_jdk.ps1 first."
}

Expand-ToolZip -ZipPath $zipFile -DestDir $JdkInstallDir

# The archive contains a single top-level jdk-<version> folder.
$extracted = Get-ChildItem $JdkInstallDir -Directory | Select-Object -First 1
Write-Host "JDK installed: $($extracted.FullName)"
