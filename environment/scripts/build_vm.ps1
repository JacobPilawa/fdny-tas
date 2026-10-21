# environment/scripts/build_vm.ps1
#
# Runs the Packer Hyper-V build using values from environment/config.ps1 as
# the single source of truth. Needs an elevated PowerShell (Hyper-V VM
# creation requires admin or Hyper-V Administrators group membership) — this
# script does not elevate itself.

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$packerExe = Join-Path $PackerInstallDir 'packer.exe'
if (-not (Test-Path $packerExe)) {
    throw "Packer not found: $packerExe. Run get_packer.ps1 / install_packer.ps1 first."
}

$env:PACKER_CACHE_DIR = $PackerCacheDir

Push-Location $PackerDir
try {
    & $packerExe init windows-server.pkr.hcl
    if ($LASTEXITCODE -ne 0) {
        throw "packer init failed with exit code $LASTEXITCODE"
    }

    & $packerExe build -force `
        -var "iso_url=$WindowsIsoUrl" `
        -var "iso_checksum=$WindowsIsoChecksum" `
        -var "vm_name=$VmName" `
        -var "switch_name=$VmSwitchName" `
        -var "cpus=$VmCpus" `
        -var "memory_mb=$VmMemoryMb" `
        -var "disk_size_mb=$VmDiskSizeMb" `
        -var "admin_password=$VmAdminPassword" `
        -var "output_directory=$PackerOutputDir" `
        windows-server.pkr.hcl
    if ($LASTEXITCODE -ne 0) {
        throw "packer build failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}
