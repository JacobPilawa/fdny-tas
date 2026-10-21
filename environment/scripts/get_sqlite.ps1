# Downloads the sqlite3 CLI (per environment/config.ps1) into environment/local/downloads/.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$outFile = Join-Path $DownloadDir $SqliteAssetName
Get-RepoFile -Url $SqliteDownloadUrl -OutFile $outFile

Write-Host "sqlite3 archive ready: $outFile"
