# run.ps1 -- the unit tests (D47): no game, seconds, also run by CI (.github/workflows/unit.yml).
#   .\core\unit\run.ps1              build core\build\unit\fdcore_unit.exe, run it, then the Python tests
#   .\core\unit\run.ps1 -NoPython    the C++ tests only
# The C++ side compiles core sources for the host with FDCORE_UNIT (stubs.cpp stands in for
# the services they call); the Python side is every core\unit\test_*.py.
param([switch]$NoPython)

# Native tools report through exit codes; their stderr (compiler notes, unittest's progress)
# must not abort the script, which Windows PowerShell 5.1 does under 'Stop'.
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$core = Split-Path -Parent $here
. (Join-Path $core '..\environment\config.ps1')
$zig = Join-Path $ZigInstallDir 'zig.exe'
if (-not (Test-Path $zig)) {
    $zig = (Get-ChildItem (Split-Path $ZigInstallDir) -Recurse -Filter zig.exe -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
}
if (-not $zig) { throw "zig.exe not found -- run environment\scripts\get_zig.ps1 + install_zig.ps1" }
if (-not (Test-Path $PythonExe)) { $PythonExe = (Get-Command python -ErrorAction SilentlyContinue).Source }
if (-not $PythonExe) { throw "Python not found" }

# The generated address table header (build.ps1 normally writes it).
& $PythonExe -I (Join-Path $core 'tools\addrgen.py') header (Join-Path $core 'src\gen\addresses_gen.h')
if ($LASTEXITCODE -ne 0) { throw "addrgen header failed" }

# Core sources under test: pure logic, no game calls (each listed on purpose).
$under = @('src\addr.cpp', 'src\features\glitch_analysis.cpp', 'src\features\glitch_bsp.cpp',
           'src\features\glitch_replay.cpp') | ForEach-Object { Join-Path $core $_ }
$tests = Get-ChildItem $here -Filter *.cpp | ForEach-Object FullName

$outDir = Join-Path $core 'build\unit'
New-Item -ItemType Directory -Force $outDir | Out-Null
$exe = Join-Path $outDir 'fdcore_unit.exe'
# x86 like the game (pointer size, struct layout); the host's C runtime is fine here.
$args = @('c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O1', '-g', '-fno-sanitize=all',
          '-Wall', '-Wextra', '-Werror', '-Wno-nullability-completeness', '-Wno-missing-field-initializers',
          '-DFDCORE_UNIT=1', '-I', (Join-Path $core 'src'), '-o', $exe) + $under + $tests + @('-lkernel32', '-luser32')
& $zig @args
if ($LASTEXITCODE -ne 0) { throw "unit build failed ($LASTEXITCODE)" }
"built $exe"

& $exe
$cppFailed = $LASTEXITCODE -ne 0

$pyFailed = $false
if (-not $NoPython) {
    $env:FDCORE_UNIT_EXE = $exe
    $maps = Join-Path $core '..\FDNY SP 1.1\Data\Maps'
    if (-not $env:FDNY_MAPS -and (Test-Path $maps)) { $env:FDNY_MAPS = (Resolve-Path $maps).Path }
    "python unit tests (maps: $(if ($env:FDNY_MAPS) { $env:FDNY_MAPS } else { 'none' }))"
    & $PythonExe -m unittest discover -s $here -p 'test_*.py' -v
    $pyFailed = $LASTEXITCODE -ne 0
}

if ($cppFailed -or $pyFailed) { throw "unit tests failed (C++: $(-not $cppFailed), Python: $(-not $pyFailed) passed)" }
"unit tests: all passed"
