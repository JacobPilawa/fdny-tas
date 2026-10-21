# Downloads GhidrAssistMCP (per environment/config.ps1) into environment/local/downloads/.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$outFile = Join-Path $DownloadDir $GhidrAssistMcpAssetName
Get-RepoFile -Url $GhidrAssistMcpDownloadUrl -OutFile $outFile

Write-Host "GhidrAssistMCP archive ready: $outFile"
