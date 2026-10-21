# Downloads a Temurin JDK 21 (per environment/config.ps1) into environment/local/downloads/.
# Ghidra requires a JDK to launch and does not bundle one.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$outFile = Join-Path $DownloadDir $JdkAssetName
Get-RepoFile -Url $JdkDownloadUrl -OutFile $outFile

Write-Host "JDK archive ready: $outFile"
