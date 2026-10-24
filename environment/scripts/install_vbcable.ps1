# Installs the VB-CABLE virtual audio driver silently and enables the Windows
# audio services (they're Manual/Stopped on Server 2022). Run get_vbcable.ps1
# first. VM-only test dependency.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$zipFile = Join-Path $DownloadDir $VbCableAssetName
if (-not (Test-Path $zipFile)) {
    throw "VB-CABLE pack not found: $zipFile. Run get_vbcable.ps1 first."
}
Expand-ToolZip -ZipPath $zipFile -DestDir $VbCableInstallDir

foreach ($svc in 'AudioEndpointBuilder', 'Audiosrv') {
    Set-Service -Name $svc -StartupType Automatic
    Start-Service -Name $svc
}

# -i install, -h hidden (no UI). GUI-subsystem exe, so wait explicitly.
$setup = Join-Path $VbCableInstallDir 'VBCABLE_Setup_x64.exe'
$proc  = Start-Process -FilePath $setup -ArgumentList '-i', '-h' -Wait -PassThru
Write-Host "VB-CABLE setup exit code: $($proc.ExitCode)"
