# environment/scripts/common.ps1
#
# Shared helpers, dot-sourced by the get_*/install_* scripts.

function Get-RepoFile {
    param(
        [Parameter(Mandatory)] [string]$Url,
        [Parameter(Mandatory)] [string]$OutFile
    )

    if (Test-Path $OutFile) {
        Write-Host "Already downloaded: $OutFile"
        return
    }

    New-Item -ItemType Directory -Force -Path (Split-Path $OutFile) | Out-Null

    Write-Host "Downloading $Url"
    $ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is far slower with the progress bar on
    Invoke-WebRequest -Uri $Url -OutFile $OutFile -ErrorAction Stop
}

function Expand-ToolZip {
    param(
        [Parameter(Mandatory)] [string]$ZipPath,
        [Parameter(Mandatory)] [string]$DestDir
    )

    New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
    Expand-Archive -Path $ZipPath -DestinationPath $DestDir -Force
}

# Most of our archives (Ghidra, JDK) unpack to a single version-named folder.
function Get-FirstSubDir {
    param([Parameter(Mandatory)] [string]$Dir)

    Get-ChildItem $Dir -Directory | Select-Object -First 1
}

# Updates (or appends) a `$Name = 'Value'` assignment in a config.ps1-style file.
function Set-ConfigValue {
    param(
        [Parameter(Mandatory)] [string]$ConfigPath,
        [Parameter(Mandatory)] [string]$Name,
        [Parameter(Mandatory)] [string]$Value
    )

    $newLine = "`$$Name = '$Value'"
    $found   = $false

    $updated = foreach ($line in Get-Content $ConfigPath) {
        if ($line -match "^\`$$Name\s*=") {
            $found = $true
            $newLine
        } else {
            $line
        }
    }

    if (-not $found) {
        $updated += $newLine
    }

    Set-Content -Path $ConfigPath -Value $updated
}

# Enables a plugin class in a Ghidra .tcd tool config, the same way
# File > Configure > Configure Plugins does — verified by diffing
# _code_browser.tcd before/after enabling GhidrAssist/GhidrAssistMCP by
# hand (2026-08-26). Idempotent; creates the Miscellaneous PACKAGE block
# if the tool file doesn't have one yet.
function Add-ToolPluginInclude {
    param(
        [Parameter(Mandatory)] [string]$ToolFile,
        [Parameter(Mandatory)] [string]$ClassName
    )

    $lines = Get-Content $ToolFile
    if ($lines -match [regex]::Escape("CLASS=`"$ClassName`"")) {
        Write-Host "Already enabled: $ClassName"
        return
    }

    $includeLine  = "            <INCLUDE CLASS=`"$ClassName`" />"
    $hasMiscBlock = [bool]($lines -match '<PACKAGE NAME="Miscellaneous">')

    $updated = foreach ($line in $lines) {
        if (-not $hasMiscBlock -and $line -match '^\s*<ROOT_NODE') {
            '        <PACKAGE NAME="Miscellaneous">'
            $includeLine
            '        </PACKAGE>'
        }

        $line

        if ($hasMiscBlock -and $line -match '<PACKAGE NAME="Miscellaneous">') {
            $includeLine
        }
    }

    Set-Content -Path $ToolFile -Value $updated
    Write-Host "Enabled: $ClassName"
}
