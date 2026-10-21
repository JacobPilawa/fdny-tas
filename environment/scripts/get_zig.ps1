# Downloads the Zig toolchain (per environment/config.ps1) into
# environment/local/downloads/.  Zig's `c++` frontend is used by asi\build.ps1
# to cross-compile the native FDNYVictimSpawns.asi (32-bit PE32 plugin).

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$outFile = Join-Path $DownloadDir $ZigAssetName
Get-RepoFile -Url $ZigDownloadUrl -OutFile $outFile

Write-Host "Zig archive ready: $outFile"
