# build.ps1 -- build FDNYCore.asi (32-bit PE DLL) with the bundled zig toolchain.
#   .\build.ps1                    # Debug and Release
#   .\build.ps1 -Config Release
# Output: core\build\<Config>\FDNYCore.asi
# Freestanding like asi\build.ps1: no CRT, imports kernel32/user32/bcrypt only.

param([ValidateSet('Debug', 'Release', 'Both')] [string]$Config = 'Both')

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here '..\environment\config.ps1')
$zig = Join-Path $ZigInstallDir 'zig.exe'
if (-not (Test-Path $zig)) {
    $zig = (Get-ChildItem (Split-Path $ZigInstallDir) -Recurse -Filter zig.exe -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
}
if (-not $zig) { throw "zig.exe not found -- run environment\scripts\get_zig.ps1 + install_zig.ps1" }
$winInc = Join-Path (Split-Path -Parent $zig) 'lib\libc\include\any-windows-any'

$sources = Get-ChildItem (Join-Path $here 'src') -Filter *.cpp | ForEach-Object FullName

# Distinct preferred base: Genesis.dll and the other .asi files sit at
# 0x10000000 / 0x3C000000. Relocatable regardless (--dynamicbase).
$common = @(
    'c++', '-target', 'x86-windows-gnu', '-shared', '-std=c++17',
    '-fno-rtti', '-fno-exceptions', '-fno-stack-protector', '-fno-builtin',
    '-nostdlib', '-Wall', '-Wextra', '-Werror', '-Wno-nullability-completeness',
    '-Wno-unused-command-line-argument',   # zig injects -fno-rtlib-defaultlib itself
    '-isystem', $winInc,
    '-Wl,--image-base,0x3D000000', '-Wl,--dynamicbase'
)
$perConfig = @{
    # zig enables UBSan at -O0; there's no sanitizer runtime in a no-CRT DLL.
    Debug   = @('-O0', '-g', '-fno-sanitize=all', '-DFDCORE_DEBUG=1')
    Release = @('-O2')
}
$configs = if ($Config -eq 'Both') { 'Debug', 'Release' } else { @($Config) }

foreach ($c in $configs) {
    $outDir = Join-Path $here "build\$c"
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $out = Join-Path $outDir 'FDNYCore.asi'
    $args = $common + $perConfig[$c] + @('-o', $out) + $sources + @('-lkernel32', '-luser32', '-lbcrypt')
    & $zig @args
    if ($LASTEXITCODE -ne 0) { throw "$c build failed ($LASTEXITCODE)" }

    $b = [System.IO.File]::ReadAllBytes($out)
    $pe = [BitConverter]::ToInt32($b, 0x3C)
    $mach = [BitConverter]::ToUInt16($b, $pe + 4)
    "built $c\FDNYCore.asi  $([math]::Round($b.Length / 1KB, 1)) KB  machine=0x$('{0:X}' -f $mach)"
    if ($mach -ne 0x14C) { throw "$c build is not x86 (machine 0x$('{0:X}' -f $mach))" }
}
