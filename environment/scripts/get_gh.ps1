# Downloads the GitHub CLI (per environment/config.ps1) into environment/local/downloads/
# and checks its SHA-256 against the release's published checksums file.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$outFile = Join-Path $DownloadDir $GhAssetName
$sumFile = Join-Path $DownloadDir "gh_${GhVersion}_checksums.txt"
Get-RepoFile -Url $GhDownloadUrl -OutFile $outFile
Get-RepoFile -Url $GhChecksumsUrl -OutFile $sumFile

$line = Get-Content $sumFile | Where-Object { $_ -match "\s$([regex]::Escape($GhAssetName))$" } | Select-Object -First 1
if (-not $line) { throw "No checksum for $GhAssetName in $sumFile" }
$want = ($line -split '\s+')[0].ToLower()
$got = (Get-FileHash $outFile -Algorithm SHA256).Hash.ToLower()
if ($got -ne $want) {
    Remove-Item $outFile -Force
    throw "SHA-256 mismatch for ${GhAssetName}: got $got, want $want (download removed)"
}

Write-Host "GitHub CLI archive ready: $outFile (sha256 $got, verified)"
