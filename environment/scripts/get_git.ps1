# Downloads PortableGit (per environment/config.ps1) into environment/local/downloads/.
# Self-extracting 7z exe, not a zip — install_git.ps1 runs it to extract.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$outFile = Join-Path $DownloadDir $GitAssetName
Get-RepoFile -Url $GitDownloadUrl -OutFile $outFile

Write-Host "PortableGit installer ready: $outFile"
