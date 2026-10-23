# Builds AslHost.exe against a LiveSplit install, into environment\local\tools\livesplit
# (a copy of LiveSplit with the downloaded-file block removed: .NET refuses to load
# blocked DLLs, and the game folder's copy is left untouched).
#
#   .\build_host.ps1 [-LiveSplit <folder with LiveSplit.exe>]
param(
    [string]$LiveSplit = (Join-Path $PSScriptRoot "..\..\FDNY SP 1.1\Speedrunning\livesplit")
)
$ErrorActionPreference = "Stop"
$Local = Join-Path $PSScriptRoot "..\..\environment\local\tools\livesplit"
$Csc = "$env:WINDIR\Microsoft.NET\Framework\v4.0.30319\csc.exe"

New-Item -ItemType Directory -Force $Local | Out-Null
Copy-Item (Join-Path $LiveSplit "*") $Local -Recurse -Force
Get-ChildItem $Local -Recurse -File | Unblock-File

$refs = @(
    (Join-Path $Local "LiveSplit.Core.dll"),
    (Join-Path $Local "Components\LiveSplit.ScriptableAutoSplit.dll"),
    "System.Windows.Forms.dll", "System.Drawing.dll", "Microsoft.CSharp.dll", "System.Core.dll"
) | ForEach-Object { "/r:`"$_`"" }
$out = Join-Path $Local "AslHost.exe"
# the paths have spaces ("live split", "FDNY SP 1.1"): quote each argument for csc
& $Csc /nologo /target:exe /platform:x86 "/out:`"$out`"" @refs "`"$(Join-Path $PSScriptRoot 'AslHost.cs')`""
if ($LASTEXITCODE) { throw "csc failed ($LASTEXITCODE)" }
Write-Host "built $(Join-Path $Local 'AslHost.exe')"
