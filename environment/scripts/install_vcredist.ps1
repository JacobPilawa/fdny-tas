# Silently installs the downloaded Visual C++ x86 redistributable. Run
# get_vcredist.ps1 first. Exit 3010 = installed, reboot recommended (the
# runtime DLLs are usable without one).

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$exeFile = Join-Path $DownloadDir $VcRedistAssetName
if (-not (Test-Path $exeFile)) {
    throw "VC++ redistributable not found: $exeFile. Run get_vcredist.ps1 first."
}

$proc = Start-Process -FilePath $exeFile -ArgumentList '/install', '/quiet', '/norestart' -Wait -PassThru
if ($proc.ExitCode -notin 0, 1638, 3010) {   # 1638 = a newer version is already installed
    throw "VC++ redistributable install failed (exit $($proc.ExitCode))"
}
Write-Host "VC++ redistributable installed (exit $($proc.ExitCode))"
