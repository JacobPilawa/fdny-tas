# environment/scripts/run_ghidra.ps1
#
# Launches the GUI Ghidra install with JAVA_HOME pointed at the JDK
# provisioned by environment/scripts/install_jdk.ps1 — Ghidra doesn't
# bundle a JDK and none is set system-wide on this machine.
#
# -OpenProject: opens $GhidraProjectDir\$GhidraProjectName.gpr directly,
# skipping Ghidra's project picker.

param(
    [switch]$OpenProject
)

$GhidraRunRelPath = 'ghidraRun.bat'

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$ghidraHome = Get-FirstSubDir $GhidraInstallDir
if (-not $ghidraHome) {
    throw "Ghidra install not found under $GhidraInstallDir. Run install_ghidra.ps1 first."
}

$jdkHome = Get-FirstSubDir $JdkInstallDir
if (-not $jdkHome) {
    throw "JDK install not found under $JdkInstallDir. Run install_jdk.ps1 first."
}
$env:JAVA_HOME = $jdkHome.FullName

$ghidraRun = Join-Path $ghidraHome.FullName $GhidraRunRelPath

if ($OpenProject) {
    if (-not $GhidraProjectName) {
        throw 'GhidraProjectName is not set in config.ps1. Run config_ghidra.ps1 first.'
    }
    $projectFile = Join-Path $GhidraProjectDir "$GhidraProjectName.gpr"
    & $ghidraRun $projectFile
} else {
    & $ghidraRun
}
