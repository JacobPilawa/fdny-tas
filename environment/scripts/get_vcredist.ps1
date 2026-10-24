# Downloads the Visual C++ 2015-2022 x86 redistributable (per
# environment/config.ps1) into environment/local/downloads/. VM-only: the
# original alttabfix.asi needs it to load.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$outFile = Join-Path $DownloadDir $VcRedistAssetName
Get-RepoFile -Url $VcRedistDownloadUrl -OutFile $outFile

Write-Host "VC++ redistributable ready: $outFile"
