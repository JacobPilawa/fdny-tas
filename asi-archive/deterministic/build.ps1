# build.ps1 -- compile a freestanding .asi (32-bit PE DLL) with the bundled zig toolchain.
#   .\build.ps1                          # FDNYDeterministic.asi (default)
#   .\build.ps1 -Name FDNYVictimSpawns   # the older, spawns-only plugin
# Output: asi\<Name>.asi  (drop it + the .ini in the game folder next to dinput8.dll)

param([string]$Name = 'FDNYDeterministic')

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$zig  = (Get-ChildItem "C:\fdny-decomp\environment\local\tools" -Recurse -Filter zig.exe -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
if (-not $zig) { throw "zig.exe not found -- run environment\scripts\get_zig.ps1 + install_zig.ps1" }

$src = Join-Path $here "$Name.cpp"
$out = Join-Path $here "$Name.asi"
if (-not (Test-Path $src)) { throw "source not found: $src" }
$zdir = Split-Path -Parent $zig
$winInc = Join-Path $zdir 'lib\libc\include\any-windows-any'

$args = @(
    'c++', '-target', 'x86-windows-gnu',
    '-shared', '-O2', '-std=c++17',
    '-fno-rtti', '-fno-exceptions', '-fno-stack-protector', '-fno-builtin',
    '-nostdlib', '-Wno-nullability-completeness',
    "-isystem", $winInc,
    # distinct high base: Genesis.dll and the other .asi files all prefer
    # 0x10000000 with no relocs, so claim our own free region instead.
    # (FDNYDeterministic replaces FDNYVictimSpawns, so they share it.)
    '-Wl,--image-base,0x3C000000', '-Wl,--dynamicbase',
    '-o', $out, $src,
    '-lkernel32', '-luser32'
)
Write-Host "zig $($args -join ' ')"
& $zig @args
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

# drop zig link byproducts
foreach ($x in "$Name.lib", "$Name.pdb") {
    $bp = Join-Path $here $x
    if (Test-Path $bp) { [System.IO.File]::Delete($bp) }
}

# quick PE sanity
$b = [System.IO.File]::ReadAllBytes($out)
$pe = [BitConverter]::ToInt32($b, 0x3C)
$mach = [BitConverter]::ToUInt16($b, $pe + 4)
$isDll = [bool]([BitConverter]::ToUInt16($b, $pe + 22) -band 0x2000)
"built $([System.IO.Path]::GetFileName($out))  $([math]::Round((Get-Item $out).Length/1KB,1)) KB  machine=0x$('{0:X}' -f $mach) (want 14C)  dll=$isDll"
