# Extracts the downloaded ffmpeg archive into $FfmpegInstallDir and checks
# that the ffmpeg.exe path defined in config.ps1 exists.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$zipFile = Join-Path $DownloadDir $FfmpegAssetName
if (-not (Test-Path $zipFile)) {
    throw "ffmpeg archive not found: $zipFile. Run get_ffmpeg.ps1 first."
}

Expand-ToolZip -ZipPath $zipFile -DestDir $FfmpegInstallDir

# The archive contains a single top-level folder with bin\ffmpeg.exe inside.
if (-not (Test-Path $FfmpegExe)) {
    throw "ffmpeg.exe not found at expected path: $FfmpegExe"
}
Write-Host "ffmpeg.exe: $FfmpegExe"
