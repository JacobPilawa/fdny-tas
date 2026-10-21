# Installs the downloaded CPython quietly into $PythonInstallDir (tkinter
# included, no PATH or file-association changes), then pip-installs
# $PythonPipPackages.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$installer = Join-Path $DownloadDir $PythonAssetName
if (-not (Test-Path $installer)) {
    throw "Python installer not found: $installer. Run get_python.ps1 first."
}

$installerArgs = @(
    '/quiet', 'InstallAllUsers=0', "TargetDir=$PythonInstallDir",
    'PrependPath=0', 'AssociateFiles=0', 'Shortcuts=0', 'Include_launcher=0',
    'Include_test=0', 'Include_tcltk=1', 'Include_pip=1'
)
$p = Start-Process -FilePath $installer -ArgumentList $installerArgs -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "Python installer failed with exit code $($p.ExitCode)" }
if (-not (Test-Path $PythonExe)) { throw "python.exe not found after install: $PythonExe" }

& $PythonExe -m pip install --disable-pip-version-check @PythonPipPackages
if ($LASTEXITCODE -ne 0) { throw "pip install failed: $($PythonPipPackages -join ', ')" }

Write-Host "Python installed: $PythonExe"
