# Downloads the CPython installer (per environment/config.ps1) into
# environment/local/downloads/.  Used by tools\*.py (pymem + tkinter).

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$outFile = Join-Path $DownloadDir $PythonAssetName
Get-RepoFile -Url $PythonDownloadUrl -OutFile $outFile

Write-Host "Python installer ready: $outFile"
