# environment/scripts/connect_claude_mcp.ps1
#
# Registers GhidrAssistMCP's SSE endpoint as a project-scoped MCP server with
# Claude Code (writes to .mcp.json, so it's shared with anyone who clones
# this repo) and reports whether Claude Code can currently reach it.
#
# GhidrAssistMCP only serves once Ghidra is actually open with a tool (e.g.
# CodeBrowser) running — enable_ghidrassist.ps1 flips the plugin on, but
# Ghidra itself still has to be launched (run_ghidra.ps1) and a project
# opened before the server starts.

. (Join-Path $PSScriptRoot '..\config.ps1')

claude mcp add --transport sse --scope project $ClaudeMcpServerName $GhidrAssistMcpSseUrl 2>&1 |
    ForEach-Object { Write-Host $_ }

Write-Host ''
Write-Host "Checking '$ClaudeMcpServerName' ($GhidrAssistMcpSseUrl)..."
claude mcp get $ClaudeMcpServerName
