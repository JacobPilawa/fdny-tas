# Downloads GhidrAssist (per environment/config.ps1) into environment/local/downloads/.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$outFile = Join-Path $DownloadDir $GhidrAssistAssetName
Get-RepoFile -Url $GhidrAssistDownloadUrl -OutFile $outFile

Write-Host "GhidrAssist archive ready: $outFile"
