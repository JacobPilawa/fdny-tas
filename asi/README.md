# FDNYVictimSpawns.asi

Pin **F.D.N.Y. Firefighter: American Hero** victim spawn points per level.

Native, in-process port of `tools/fdny_spawns.py` — loaded by the game through
the same **Ultimate ASI Loader** (`dinput8.dll`) that runs
`FDNYFirefighterAmericanHeroFOVFix.asi`. No separate process, no memory-editing
tool to run alongside the game.

---

## What it does

Every level, the game's `VictimRescueEnt::SpawnAtSpot` picks **one** of each
victim's authored spawn points at random. This plugin forces the choice.

It installs a single inline hook inside `SpawnAtSpot` at `0x0045D717` —
*after* the game has copied the RNG-chosen spot's coordinates and set the
victim's facing, but *before* the game's own `SnapToFloor` call. For each
victim it overwrites the position with the spot **you** picked; execution then
falls through into `SnapToFloor`, so the forced victim is floor-snapped by the
game itself. Exact placement, first load, every load.

The RNG stream is never touched — the game still draws its index; the plugin
just substitutes coordinates a few instructions later.

## Install

Drop both files in the game folder next to `dinput8.dll` /
`FDNYFirefighter.exe`:

```
FDNYVictimSpawns.asi
FDNYVictimSpawns.ini
```

That's it. On launch it writes `FDNYVictimSpawns.log` beside them.

## Config — `FDNYVictimSpawns.ini`

```ini
[Settings]
Enabled  = true
StopRoam = true          ; also freeze pinned victims to idle so they can't wander

[Spawns]
; one key per map = spot indices in .bsp spawn order (list position = the victim).
;   0      = the victim's editor origin
;   1..N   = its RCS_VictimAltSpot markers, in .bsp order
;   -1 / x = leave that victim on the game's random pick
training = 0, 0
bar_r    = 0, -1, 0, 0, 2, 0
oil_rig_d = 2, 0, 0
```

Which index is which victim (and where each spot is) — see
`tools/victims_reference.md` and the per-map images in `tools/spawn_maps/`.
The shipped `.ini` lists every campaign map with all victims pinned to `0`
(their editor origin) as a starting point.

## Notes / limits

- **Facing** of a forced victim still reflects the RNG-picked spot's angle
  (the game sets facing before the hook fires). Position is exact.
- Ordinal tracking (which victim is which) resets on a >2 s gap between spawns,
  which is every level (re)load — same-level restarts included.
- Maps with no `RCS_VictimRescue` victims (`city_h`, `const_h`, `dock_h`,
  `oil_rig_h`) have nothing to pin; their `.ini` lines are inert.
- If the plugin sees a game build whose bytes at `0x0045D717` don't match, it
  logs a mismatch and installs nothing (safe no-op).

## Build

```powershell
.\build.ps1
```

Needs the bundled Zig toolchain (`environment\scripts\get_zig.ps1` then
`install_zig.ps1`). `build.ps1` invokes `zig c++ -target x86-windows-gnu` to
cross-compile a freestanding 32-bit PE32 DLL that imports only `kernel32` and
`user32` — no CRT, no MSVC/MinGW/SDK. Source: `FDNYVictimSpawns.cpp` (single
file, ~12 KB output).
