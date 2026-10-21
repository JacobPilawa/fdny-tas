# environment/config.ps1
#
# Central location for all user-tweakable parameters. get_*/install_*
# scripts dot-source this file rather than hardcoding versions or paths.

# --- Game ---

$GameExePath = 'C:\fdny-decomp\FDNY SP 1.1\FDNYFirefighter.exe'   # TODO: point this at the game executable

# --- Shared paths ---
# environment/local/ is entirely machine-generated and gitignored: nothing
# under it is source. All scripts (portable get_*/install_* and the
# machine-oriented ones) live flat in environment/scripts/.

$DownloadDir = Join-Path $PSScriptRoot 'local\downloads'
$ToolsDir    = Join-Path $PSScriptRoot 'local\tools'

# --- Ghidra ---
# GhidrAssist ships builds pinned to specific Ghidra versions, so Ghidra's
# version is pinned here too rather than tracking "latest".

$GhidraVersion     = '12.1'
$GhidraBuildDate   = '20260513'
$GhidraAssetName   = "ghidra_${GhidraVersion}_PUBLIC_${GhidraBuildDate}.zip"
$GhidraDownloadUrl = "https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_${GhidraVersion}_build/${GhidraAssetName}"
$GhidraInstallDir  = Join-Path $ToolsDir 'ghidra'

# --- JDK ---
# Ghidra needs a JDK on PATH/JAVA_HOME to launch at all; it does not bundle one.
# Tracking Adoptium's rolling "latest" endpoint is fine here — unlike
# Ghidra/GhidrAssist, JDK 21 point releases aren't version-coupled to Ghidra.

$JdkDownloadUrl = 'https://api.adoptium.net/v3/binary/latest/21/ga/windows/x64/jdk/hotspot/normal/eclipse?project=jdk'
$JdkAssetName   = 'temurin21-jdk-windows-x64.zip'
$JdkInstallDir  = Join-Path $ToolsDir 'jdk'

# --- GhidrAssist ---

$GhidrAssistVersion     = '2.2.0'
$GhidrAssistAssetName   = 'ghidra_12.1_PUBLIC_20260530_GhidrAssist.zip'
$GhidrAssistDownloadUrl = "https://github.com/symgraph/GhidrAssist/releases/download/${GhidrAssistVersion}/${GhidrAssistAssetName}"

# --- GhidrAssistMCP ---
# Separate extension from GhidrAssist: runs an MCP server inside Ghidra so an
# external MCP client (Claude Code) can call Ghidra's RE functions as tools.

$GhidrAssistMcpVersion     = '2.11.0'
$GhidrAssistMcpAssetName   = 'ghidra_12.1_PUBLIC_20260802_GhidrAssistMCP.zip'
$GhidrAssistMcpDownloadUrl = "https://github.com/symgraph/GhidrAssistMCP/releases/download/${GhidrAssistMcpVersion}/${GhidrAssistMcpAssetName}"
$GhidrAssistMcpHost        = 'localhost'
$GhidrAssistMcpPort        = 8080
$GhidrAssistMcpSseUrl      = "http://${GhidrAssistMcpHost}:${GhidrAssistMcpPort}/sse"
$ClaudeMcpServerName       = 'ghidrassistmcp'
$GhidrAssistPluginClass    = 'ghidrassist.GhidrAssistPlugin'
$GhidrAssistMcpPluginClass = 'ghidrassistmcp.GhidrAssistMCPPlugin'

# --- Ghidra user settings ---
# Where Ghidra keeps per-user state: installed (enabled) extensions and tool
# configs (which plugins are on/off per tool, e.g. CodeBrowser). Verified by
# diffing this file before/after enabling GhidrAssist/GhidrAssistMCP by hand
# in the GUI (2026-08-26) — enabling a plugin there just adds an
# `<INCLUDE CLASS="..."/>` line, so environment/scripts/enable_ghidrassist.ps1
# does the same thing headlessly instead.

$GhidraUserSettingsDir  = Join-Path $env:APPDATA "ghidra\ghidra_${GhidraVersion}_PUBLIC"
$GhidraUserExtensionsDir = Join-Path $GhidraUserSettingsDir 'Extensions'
$GhidraToolConfigFile    = Join-Path $GhidraUserSettingsDir 'tools\_code_browser.tcd'

# --- Claude Code ---
# Native Windows installer. Downloaded in Phase 1 but not run until a later
# phase — GhidrAssistMCP is what Claude Code (already installed on this dev
# machine) connects to, not vice versa.

$ClaudeInstallScriptUrl = 'https://claude.ai/install.ps1'
$ClaudeInstallerName    = 'claude-install.ps1'

# --- SQLite ---
# Phase 3's research-state store. Command-line shell only, no bespoke
# data-access layer — see plan.md.

$SqliteVersion    = '3530400'
$SqliteAssetName  = "sqlite-tools-win-x64-${SqliteVersion}.zip"
$SqliteDownloadUrl = "https://www.sqlite.org/2026/${SqliteAssetName}"
$SqliteInstallDir = Join-Path $ToolsDir 'sqlite'
$SqliteExe        = Join-Path $SqliteInstallDir 'sqlite3.exe'

# --- Research state (Phase 3/4) ---
# The canonical research datastore + its generated human-readable view.
# Lives at the repo root (not under environment/local/) because it's the
# project's actual output and travels back to the host by file copy.
# environment/scripts/init_research_db.ps1 creates the DB from
# research_schema.sql; environment/scripts/research_report.ps1 regenerates
# the HTML from the DB.

$ResearchDir      = Join-Path $PSScriptRoot '..\research'
$ResearchDbPath   = Join-Path $ResearchDir 'research.db'
$ResearchHtmlPath = Join-Path $ResearchDir 'research.html'
$ResearchSchema   = Join-Path $PSScriptRoot 'scripts\research_schema.sql'

# --- Git ---
# Not for cloning this repo (there's no remote access from the VM) — for
# the VM's own agent to `git init` inside C:\fdny-decomp and track its own
# changes to scripts/config/RESEARCH.md as it runs unattended. Portable,
# self-extracting 7z exe, not a zip.

$GitVersion       = '2.55.0.5'
$GitAssetName     = "PortableGit-${GitVersion}-64-bit.7z.exe"
$GitDownloadUrl   = "https://github.com/git-for-windows/git/releases/download/v2.55.0.windows.5/${GitAssetName}"
$GitInstallDir    = Join-Path $ToolsDir 'git'

# --- Ghidra project (written by environment/scripts/config_ghidra.ps1) ---

$GhidraProjectDir  = Join-Path $ToolsDir 'ghidra_projects'
$GhidraProjectName = 'FDNYFirefighter'

# --- Packer ---

$PackerVersion     = '1.16.0'
$PackerAssetName   = "packer_${PackerVersion}_windows_amd64.zip"
$PackerDownloadUrl = "https://releases.hashicorp.com/packer/${PackerVersion}/${PackerAssetName}"
$PackerInstallDir  = Join-Path $ToolsDir 'packer'

# --- Windows Server 2022 Evaluation ISO ---
# Microsoft's eval-center page for Server 2025 now gates the ISO behind a
# registration/lead-capture form (no stable direct link). The 2022 eval's
# older static link still works (verified 2026-08-26, HTTP 200, ~5GB) and is
# fine for this toolchain — Desktop Experience runs our RE tooling the same.

$WindowsIsoUrl      = 'https://software-static.download.prss.microsoft.com/sg/download/888969d5-f34g-4e03-ac9d-1f9786c66749/SERVER_EVAL_x64FRE_en-us.iso'
$WindowsIsoName     = 'SERVER_EVAL_x64FRE_en-us.iso'
$WindowsIsoChecksum = 'sha256:3e4fa6d8507b554856fc9ca6079cc402df11a8b79344871669f0251535255325'

# --- Packer / Hyper-V VM build ---
# Hyper-V's VMMS service can't attach ISOs/VHDs that live on a cloud-sync
# drive (this repo is on Google Drive) — "Access is denied" from
# Add-VMDvdDrive, confirmed 2026-08-26. Cache and build output have to sit
# on a real local NTFS volume.

$PackerCacheDir   = 'C:\packer-cache'
$PackerOutputDir  = 'C:\packer-output\fdny-decomp'
$PackerDir        = Join-Path $PSScriptRoot 'packer'
$VmSwitchName      = 'Default Switch'   # Hyper-V's built-in NAT switch
$VmName            = 'fdny-decomp'
$VmCpus            = 4
$VmMemoryMb        = 16384   # 8192 hit "insufficient memory for the JRE" during the guest's own Ghidra headless run
$VmDiskSizeMb      = 100000
$VmAdminPassword   = 'pass'   # must match autounattend.xml's AdministratorPassword/AutoLogon values

# --- Zig (C/C++ toolchain) ---
# Used by asi\build.ps1 to compile FDNYVictimSpawns.asi -- a native 32-bit
# plugin (Ultimate ASI Loader) that pins victim spawn points per level, the
# in-process equivalent of tools\fdny_spawns.py. `zig c++` cross-compiles a
# freestanding PE32 DLL (kernel32+user32 only), no MSVC/MinGW/SDK needed.

$ZigVersion     = '0.16.0'
$ZigAssetName   = "zig-x86_64-windows-$ZigVersion.zip"
$ZigDownloadUrl = "https://ziglang.org/download/$ZigVersion/zig-x86_64-windows-$ZigVersion.zip"
$ZigInstallDir  = Join-Path $ToolsDir 'zig'

# --- Python (tools\*.py: fdny_spawns.py, collision_overlay) ---
# Full CPython (NOT the embeddable zip -- it has no tkinter), installed
# per-directory and quietly under local\tools, no PATH/registry changes.
# pymem is pip-installed by install_python.ps1.

$PythonVersion      = '3.14.8'
$PythonAssetName    = "python-$PythonVersion-amd64.exe"
$PythonDownloadUrl  = "https://www.python.org/ftp/python/$PythonVersion/$PythonAssetName"
$PythonInstallDir   = Join-Path $ToolsDir 'python'
$PythonExe          = Join-Path $PythonInstallDir 'python.exe'
$PythonPipPackages  = @('pymem')
