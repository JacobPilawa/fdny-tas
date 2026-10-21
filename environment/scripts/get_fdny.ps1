# Points at the FDNY game resources from speedrun.com and stages environment/fdny/.
#
# speedrun.com serves a Cloudflare JS challenge to scripted requests, so these
# can't be fetched with Invoke-WebRequest/curl — download them by hand.

$FdnyZipUrl   = 'https://www.speedrun.com/static/resource/ffs0c.zip?v=f1acaa9'
$FdnyZipName  = 'ffs0c.zip'
$FdnySaveUrl  = 'https://www.speedrun.com/static/resource/pp5b7.rb?v=0b65e20'
$FdnySaveName = 'pp5b7.rb'
$FdnyDir      = Join-Path (Split-Path $PSScriptRoot -Parent) 'fdny'

. (Join-Path $PSScriptRoot 'common.ps1')

$zipFile  = Join-Path $FdnyDir $FdnyZipName
$saveFile = Join-Path $FdnyDir $FdnySaveName

New-Item -ItemType Directory -Force -Path $FdnyDir | Out-Null

if (-not (Test-Path $zipFile) -or -not (Test-Path $saveFile)) {
    Write-Host 'Cloudflare blocks scripted downloads from speedrun.com.'
    Write-Host "Download these by hand and save into $FdnyDir :"
    Write-Host "  $FdnyZipUrl  -> $FdnyZipName"
    Write-Host "  $FdnySaveUrl -> $FdnySaveName"
    Write-Host 'Re-run this script once both files are in place.'
    return
}

$extractMarker = Join-Path $FdnyDir '.extracted'
if (-not (Test-Path $extractMarker)) {
    Expand-ToolZip -ZipPath $zipFile -DestDir $FdnyDir
    New-Item -ItemType File -Path $extractMarker | Out-Null
}

Write-Host "FDNY resources ready: $FdnyDir"
