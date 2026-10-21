# Downloads the ffmpeg static build (per environment/config.ps1) into
# environment/local/downloads/. VM-only test tool: core/harness uses its
# gdigrab input to record test runs.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$outFile = Join-Path $DownloadDir $FfmpegAssetName
Get-RepoFile -Url $FfmpegDownloadUrl -OutFile $outFile

Write-Host "ffmpeg archive ready: $outFile"
