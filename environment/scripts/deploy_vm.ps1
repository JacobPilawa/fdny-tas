# environment/scripts/deploy_vm.ps1
#
# One-shot entry point: build, import, and start the VM. Needs an elevated
# PowerShell (Hyper-V VM creation requires admin or Hyper-V Administrators
# group membership) — this script does not elevate itself, it just checks
# and fails clearly if it isn't.
#
# Replaces any existing VM named $VmName — this is meant to produce a
# fresh, known-good VM each time, not preserve one across reruns. If you
# want to keep a previous build around, import a copy from
# $PackerOutputDir before running this again (see environment/packer/README.md).

. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot '..\config.ps1')

$isElevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isElevated) {
    throw 'This needs an elevated PowerShell (Hyper-V VM creation requires admin rights). Relaunch your terminal as Administrator and try again.'
}

$existingVm = Get-VM -Name $VmName -ErrorAction SilentlyContinue
if ($existingVm) {
    Write-Host "Removing existing VM '$VmName' ($($existingVm.State))..."
    if ($existingVm.State -ne 'Off') {
        Stop-VM -Name $VmName -TurnOff -Force
    }
    Remove-VM -Name $VmName -Force
}

Write-Host '=== Building VM (Packer) ==='
& (Join-Path $PSScriptRoot 'build_vm.ps1')

Write-Host '=== Importing built VM ==='
$vmcx = Get-ChildItem (Join-Path $PackerOutputDir 'Virtual Machines\*.vmcx') -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $vmcx) {
    throw "No .vmcx found under $PackerOutputDir\Virtual Machines - build did not produce an artifact."
}
Import-VM -Path $vmcx.FullName | Out-Null

Write-Host '=== Starting VM ==='
Start-VM -Name $VmName

Write-Host '=== Waiting for the VM to come up (PowerShell Direct) ==='
$credential = New-Object System.Management.Automation.PSCredential(
    'Administrator',
    (ConvertTo-SecureString $VmAdminPassword -AsPlainText -Force)
)

$ready = $false
for ($i = 0; $i -lt 20; $i++) {
    Start-Sleep -Seconds 15
    try {
        Invoke-Command -VMName $VmName -Credential $credential -ScriptBlock { $true } -ErrorAction Stop | Out-Null
        $ready = $true
        break
    } catch {
        Write-Host "  not ready yet ($($i + 1)/20)..."
    }
}
if (-not $ready) {
    throw "VM started but didn't become reachable via PowerShell Direct within 5 minutes. Check it manually via vmconnect."
}

Write-Host '=== Verifying the build ==='
Invoke-Command -VMName $VmName -Credential $credential -ScriptBlock {
    $checks = [ordered]@{
        'Ghidra install'       = Test-Path 'C:\fdny-decomp\environment\local\tools\ghidra'
        'JDK install'          = Test-Path 'C:\fdny-decomp\environment\local\tools\jdk'
        'Claude Code install'  = Test-Path "$env:USERPROFILE\.local\bin\claude.exe"
        'Claude on PATH'       = [bool](Get-Command claude -ErrorAction SilentlyContinue)
        'Desktop shortcuts'    = (Test-Path 'C:\Users\Public\Desktop\Ghidra.lnk') -and (Test-Path 'C:\Users\Public\Desktop\Claude Code.lnk')
        'plan.md present'      = Test-Path 'C:\fdny-decomp\plan.md'
        'CLAUDE.md present'    = Test-Path 'C:\fdny-decomp\CLAUDE.md'
        'RESEARCH.md present'  = Test-Path 'C:\fdny-decomp\RESEARCH.md'
    }

    foreach ($check in $checks.GetEnumerator()) {
        $status = if ($check.Value) { 'OK' } else { 'MISSING' }
        Write-Host "  [$status] $($check.Key)"
    }

    if ($checks.Values -contains $false) {
        throw 'One or more verification checks failed — see above.'
    }
}

Write-Host ''
Write-Host '=== VM ready ==='
Write-Host 'Remaining manual steps: set $GameExePath in environment/config.ps1 on the VM, and log into Claude Code once (claude prompts for auth on first run).'
