# Downloads the Claude Code native installer script into environment/local/downloads/.
# Staged for a later phase — not run as part of Phase 1.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$outFile = Join-Path $DownloadDir $ClaudeInstallerName
Get-RepoFile -Url $ClaudeInstallScriptUrl -OutFile $outFile

Write-Host "Claude Code installer ready: $outFile"
