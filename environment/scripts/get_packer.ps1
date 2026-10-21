# Downloads Packer (per environment/config.ps1) into environment/local/downloads/.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$outFile = Join-Path $DownloadDir $PackerAssetName
Get-RepoFile -Url $PackerDownloadUrl -OutFile $outFile

Write-Host "Packer archive ready: $outFile"
