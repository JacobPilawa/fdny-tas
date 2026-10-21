# environment/scripts/config_ghidra.ps1
#
# Headlessly creates a Ghidra project imported from $GameExePath
# (environment/config.ps1), then writes the resulting project name back
# into config.ps1 via $GhidraProjectName.

$ConfigPath             = Join-Path $PSScriptRoot '..\config.ps1'
$AnalyzeHeadlessRelPath = 'support\analyzeHeadless.bat'

. (Join-Path $PSScriptRoot 'common.ps1')
. $ConfigPath

if (-not (Test-Path $GameExePath)) {
    throw "GameExePath does not point at a real file: $GameExePath. Set it in environment/config.ps1 first."
}

$ghidraHome = Get-FirstSubDir $GhidraInstallDir
if (-not $ghidraHome) {
    throw "Ghidra install not found under $GhidraInstallDir. Run install_ghidra.ps1 first."
}

$jdkHome = Get-FirstSubDir $JdkInstallDir
if (-not $jdkHome) {
    throw "JDK install not found under $JdkInstallDir. Run install_jdk.ps1 first."
}
$env:JAVA_HOME = $jdkHome.FullName

$projectName    = [System.IO.Path]::GetFileNameWithoutExtension($GameExePath)
$analyzeHeadless = Join-Path $ghidraHome.FullName $AnalyzeHeadlessRelPath

New-Item -ItemType Directory -Force -Path $GhidraProjectDir | Out-Null

& $analyzeHeadless $GhidraProjectDir $projectName -import $GameExePath
if ($LASTEXITCODE -ne 0) {
    throw "analyzeHeadless exited with code $LASTEXITCODE"
}

Set-ConfigValue -ConfigPath $ConfigPath -Name 'GhidraProjectName' -Value $projectName
Write-Host "Ghidra project ready: $GhidraProjectDir\$projectName"
