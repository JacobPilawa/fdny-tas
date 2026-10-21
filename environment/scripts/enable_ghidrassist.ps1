# environment/scripts/enable_ghidrassist.ps1
#
# Headlessly does what Ghidra's File > Install Extensions + Configure
# Plugins GUI actions do: unpacks GhidrAssist/GhidrAssistMCP into the user
# settings Extensions folder, then flips them on in the default CodeBrowser
# tool config. Verified by diffing _code_browser.tcd before/after doing
# this by hand in the GUI (2026-08-26) — see environment/config.ps1.
#
# Requires $GhidraToolConfigFile to already exist. It's created as a side
# effect of running Ghidra at all (headless or GUI) — config_ghidra.ps1
# covers that.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

if (-not (Test-Path $GhidraToolConfigFile)) {
    throw "$GhidraToolConfigFile not found. Run config_ghidra.ps1 (or launch Ghidra once) first."
}

Expand-ToolZip -ZipPath (Join-Path $DownloadDir $GhidrAssistAssetName) -DestDir $GhidraUserExtensionsDir
Expand-ToolZip -ZipPath (Join-Path $DownloadDir $GhidrAssistMcpAssetName) -DestDir $GhidraUserExtensionsDir

Add-ToolPluginInclude -ToolFile $GhidraToolConfigFile -ClassName $GhidrAssistPluginClass
Add-ToolPluginInclude -ToolFile $GhidraToolConfigFile -ClassName $GhidrAssistMcpPluginClass

Write-Host 'GhidrAssist + GhidrAssistMCP enabled. Restart Ghidra (if open) to pick up the change.'
