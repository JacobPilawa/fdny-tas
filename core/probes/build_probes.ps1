# build_probes.ps1 -- one-off research probes (not part of FDNYCore).
#   .\build_probes.ps1
# Output: core\build\probes\zz_alphaprobe.asi  (alpha-panel spike, see alphaprobe.cpp)
# Same toolchain and freestanding flags as core\build.ps1; reuses core\src\rt.cpp
# and the vendored MinHook unchanged.

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$core = Split-Path -Parent $here
$zig = (Get-ChildItem (Join-Path $core '..\environment\local\tools\zig*\zig.exe') -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
if (-not $zig) { $zig = (Get-Command zig -ErrorAction SilentlyContinue).Source }
if (-not $zig) { throw "zig not found: in environment\local\tools\zig*\ or on PATH" }
$winInc = Join-Path (Split-Path -Parent $zig) 'lib\libc\include\any-windows-any'

$outDir = Join-Path $core 'build\probes'
$objDir = Join-Path $outDir 'obj'
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

$mhDir = Join-Path $core 'third_party\minhook\src'
$objs = @()
foreach ($s in @('hook.c', 'buffer.c', 'trampoline.c', 'hde\hde32.c') | ForEach-Object { Join-Path $mhDir $_ }) {
    $o = Join-Path $objDir ([IO.Path]::GetFileNameWithoutExtension($s) + '.o')
    & $zig cc -target x86-windows-gnu -c -O2 -fno-stack-protector -fno-builtin -fno-sanitize=all -isystem $winInc -Wno-unused-command-line-argument -o $o $s
    if ($LASTEXITCODE -ne 0) { throw "MinHook build failed on $s" }
    $objs += $o
}

$out = Join-Path $outDir 'zz_alphaprobe.asi'
& $zig c++ -target x86-windows-gnu -shared -std=c++17 -O2 -fno-rtti -fno-exceptions -fno-stack-protector -fno-builtin `
    -fno-threadsafe-statics -nostdlib -Wall -Wextra -Wno-nullability-completeness -Wno-unused-command-line-argument `
    -isystem $winInc '-Wl,--image-base,0x3E000000' '-Wl,--dynamicbase' -o $out `
    (Join-Path $here 'alphaprobe.cpp') (Join-Path $core 'src\rt.cpp') @objs -lkernel32 -luser32 -lgdi32
if ($LASTEXITCODE -ne 0) { throw "alphaprobe build failed" }
"built probes\zz_alphaprobe.asi  $([math]::Round((Get-Item $out).Length / 1KB, 1)) KB"

# zz_triggerprobe.asi: trigger-box overlay spike (see triggerprobe.cpp)
$out = Join-Path $outDir 'zz_triggerprobe.asi'
& $zig c++ -target x86-windows-gnu -shared -std=c++17 -O2 -fno-rtti -fno-exceptions -fno-stack-protector -fno-builtin `
    -fno-threadsafe-statics -nostdlib -Wall -Wextra -Wno-nullability-completeness -Wno-unused-command-line-argument `
    -isystem $winInc '-Wl,--image-base,0x3E200000' '-Wl,--dynamicbase' -o $out `
    (Join-Path $here 'triggerprobe.cpp') (Join-Path $core 'src\rt.cpp') @objs -lkernel32 -luser32 -lgdi32
if ($LASTEXITCODE -ne 0) { throw "triggerprobe build failed" }
"built probes\zz_triggerprobe.asi  $([math]::Round((Get-Item $out).Length / 1KB, 1)) KB"

# zz_juggleprobe.asi: fire-juggling frame-order spike (see juggleprobe.cpp)
$out = Join-Path $outDir 'zz_juggleprobe.asi'
& $zig c++ -target x86-windows-gnu -shared -std=c++17 -O2 -fno-rtti -fno-exceptions -fno-stack-protector -fno-builtin `
    -fno-threadsafe-statics -nostdlib -Wall -Wextra -Wno-nullability-completeness -Wno-unused-command-line-argument `
    -isystem $winInc '-Wl,--image-base,0x3E400000' '-Wl,--dynamicbase' -o $out `
    (Join-Path $here 'juggleprobe.cpp') (Join-Path $core 'src\rt.cpp') @objs -lkernel32 -luser32 -lgdi32
if ($LASTEXITCODE -ne 0) { throw "juggleprobe build failed" }
"built probes\zz_juggleprobe.asi  $([math]::Round((Get-Item $out).Length / 1KB, 1)) KB"
# zz_pauseprobe.asi: pause + free camera spike (see pauseprobe.cpp)
$out = Join-Path $outDir 'zz_pauseprobe.asi'
& $zig c++ -target x86-windows-gnu -shared -std=c++17 -O2 -fno-rtti -fno-exceptions -fno-stack-protector -fno-builtin `
    -fno-threadsafe-statics -nostdlib -Wall -Wextra -Wno-nullability-completeness -Wno-unused-command-line-argument `
    -isystem $winInc '-Wl,--image-base,0x3E600000' '-Wl,--dynamicbase' -o $out `
    (Join-Path $here 'pauseprobe.cpp') (Join-Path $core 'src\rt.cpp') @objs -lkernel32 -luser32 -lgdi32
if ($LASTEXITCODE -ne 0) { throw "pauseprobe build failed" }
"built probes\zz_pauseprobe.asi  $([math]::Round((Get-Item $out).Length / 1KB, 1)) KB"

# zz_demoprobe.asi: demos / ghosts determinism spike (see demoprobe.cpp)
$out = Join-Path $outDir 'zz_demoprobe.asi'
& $zig c++ -target x86-windows-gnu -shared -std=c++17 -O2 -fno-rtti -fno-exceptions -fno-stack-protector -fno-builtin `
    -fno-threadsafe-statics -nostdlib -Wall -Wextra -Wno-nullability-completeness -Wno-unused-command-line-argument `
    -isystem $winInc '-Wl,--image-base,0x3E800000' '-Wl,--dynamicbase' -o $out `
    (Join-Path $here 'demoprobe.cpp') (Join-Path $core 'src\rt.cpp') @objs -lkernel32 -luser32 -lgdi32
if ($LASTEXITCODE -ne 0) { throw "demoprobe build failed" }
"built probes\zz_demoprobe.asi  $([math]::Round((Get-Item $out).Length / 1KB, 1)) KB"
