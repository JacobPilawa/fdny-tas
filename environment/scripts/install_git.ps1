# Silently self-extracts the downloaded PortableGit archive into
# $GitInstallDir. It's a 7z SFX exe, not a zip, so this runs it with
# -y (yes to all) and -o<dir> (extract target) instead of Expand-Archive.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$exeFile = Join-Path $DownloadDir $GitAssetName
if (-not (Test-Path $exeFile)) {
    throw "PortableGit installer not found: $exeFile. Run get_git.ps1 first."
}

New-Item -ItemType Directory -Force -Path $GitInstallDir | Out-Null
# GUI-subsystem exe: `& $exeFile` returns immediately without waiting
# (verified 2026-10-21 — left $GitInstallDir empty), so wait explicitly.
$proc = Start-Process -FilePath $exeFile -ArgumentList '-y', "-o$GitInstallDir" -Wait -PassThru
if ($proc.ExitCode -ne 0) {
    throw "PortableGit extraction failed (exit $($proc.ExitCode))"
}

Write-Host "Git installed: $(Join-Path $GitInstallDir 'bin\git.exe')"
