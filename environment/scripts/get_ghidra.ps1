# Downloads Ghidra (per environment/config.ps1) into environment/local/downloads/.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$outFile = Join-Path $DownloadDir $GhidraAssetName
Get-RepoFile -Url $GhidraDownloadUrl -OutFile $outFile

Write-Host "Ghidra archive ready: $outFile"
