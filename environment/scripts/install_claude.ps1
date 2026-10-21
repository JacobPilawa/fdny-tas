# Runs the staged Claude Code installer. Not exercised in Phase 1.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$installer = Join-Path $DownloadDir $ClaudeInstallerName
if (-not (Test-Path $installer)) {
    throw "Claude installer not found: $installer. Run get_claude.ps1 first."
}

& $installer
