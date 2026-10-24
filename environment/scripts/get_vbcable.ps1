# Downloads the VB-CABLE virtual audio driver pack (per environment/config.ps1)
# into environment/local/downloads/. VM-only test dependency: gives the game's
# FMOD an audio endpoint on a VM with no sound hardware.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$outFile = Join-Path $DownloadDir $VbCableAssetName
Get-RepoFile -Url $VbCableDownloadUrl -OutFile $outFile

Write-Host "VB-CABLE driver pack ready: $outFile"
