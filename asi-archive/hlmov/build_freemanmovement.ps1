# build FDNYFreemanMovement.asi (32-bit glue between FreemanAPI_gcp.dll and FDNY)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$zig  = (Get-ChildItem "C:\fdny-decomp\environment\local\tools" -Recurse -Filter zig.exe | Select-Object -First 1).FullName
$winInc = Join-Path (Split-Path $zig) 'lib\libc\include\any-windows-any'
$out = Join-Path $here 'FDNYFreemanMovement.asi'
$src = Join-Path $here 'FDNYFreemanMovement.cpp'

$a = @(
    'c++', '-target', 'x86-windows-gnu',
    '-shared', '-O2', '-std=c++17', '-fno-rtti', '-fno-exceptions',
    '-nostdlib', '-Wno-everything',
    '-isystem', $winInc,
    '-Wl,--image-base,0x3D000000',
    '-o', $out, $src,
    '-lkernel32', '-luser32'
)
Write-Host "zig $($a -join ' ')`n"
& $zig @a
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
$b = [System.IO.File]::ReadAllBytes($out)
$pe = [BitConverter]::ToInt32($b,0x3C)
"built $([System.IO.Path]::GetFileName($out))  $([math]::Round($b.Length/1KB,1)) KB  machine=0x$('{0:X}' -f [BitConverter]::ToUInt16($b,$pe+4))"
$txt=[System.Text.Encoding]::ASCII.GetString($b)
"imports: " + (([regex]::Matches($txt,'(?i)\b[a-z0-9_\-]+\.dll') | ForEach-Object { $_.Value.ToLower() } | Sort-Object -Unique) -join ', ')
