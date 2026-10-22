# build_freeman.ps1 -- compile FreemanAPI_gcp.dll (32-bit) from the vendored FreemanAPI source with zig.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$zig  = (Get-ChildItem "C:\fdny-decomp\environment\local\tools" -Recurse -Filter zig.exe | Select-Object -First 1).FullName
if (-not $zig) { throw "zig.exe not found" }

$src = Join-Path $here 'FreemanAPI-main\main.cpp'
$out = Join-Path $here 'FreemanAPI_gcp.dll'

$a = @(
    'c++', '-target', 'x86-windows-gnu',
    '-shared', '-O2', '-std=c++20',
    '-DNYA_COMMON_NO_D3D', '-D_USE_MATH_DEFINES',
    '-include', 'cmath', '-include', 'numbers',     # hlmov.h/hl_math.h assume these + M_PI
    '-Wno-everything',                              # third-party headers, not our lint
    "-I", (Join-Path $here 'FreemanAPI-main'),
    "-I", (Join-Path $here 'FreemanAPI-main\include'),
    "-I", (Join-Path $here 'build\vendor'),         # nya_commonmath.h + toml++\toml.hpp
    '-o', $out, $src
)
Write-Host "zig $($a -join ' ')`n"
& $zig @a
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

$b = [System.IO.File]::ReadAllBytes($out)
$pe = [BitConverter]::ToInt32($b, 0x3C)
$mach = [BitConverter]::ToUInt16($b, $pe + 4)
$txt = [System.Text.Encoding]::ASCII.GetString($b)
$exp = ([regex]::Matches($txt, 'FreemanAPI_[A-Za-z_]+')).Value | Sort-Object -Unique
"built $([System.IO.Path]::GetFileName($out))  $([math]::Round($b.Length/1KB,1)) KB  machine=0x$('{0:X}' -f $mach)"
"exports seen in image: $($exp.Count)  (e.g. $($exp | Select-Object -First 6 -join ', '))"
