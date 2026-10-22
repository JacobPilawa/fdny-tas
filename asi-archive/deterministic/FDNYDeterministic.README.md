# FDNYDeterministic.asi

Deterministic victims for **F.D.N.Y. Firefighter: American Hero**:

1. **No RescueBot rescues on any difficulty.** The AI rescue bot is *on by
   default*. On the two easier difficulties it silently claims up to
   ~40% / 25% of a level's victims at random times; on the hardest its cap is 0.
2. **Pinned victim spawns.** Each victim spawns at the authored spot you choose
   instead of a random one. This is the merged successor of
   `FDNYVictimSpawns.asi`.

Loaded by **Ultimate ASI Loader** (`dinput8.dll`), the same way as
`FDNYFirefighterAmericanHeroFOVFix.asi`.

## Install

Drop both files in the game folder next to `dinput8.dll` / `FDNYFirefighter.exe`:

```
FDNYDeterministic.asi
FDNYDeterministic.ini
```

**Remove `FDNYVictimSpawns.asi`** if it's there. Both plugins hook the same
spot; whichever loads second refuses to hook, and the log will warn you. On
launch the plugin writes `FDNYDeterministic.log` beside itself.

## Config: `FDNYDeterministic.ini`

```ini
[RescueBot]
Enabled = false      ; false = no bot rescues on any difficulty, radio chatter muted

[StaticVictimSpawns]
Enabled = true

[Spawns]
; one key per map = spot indices in .bsp spawn order (list position = the victim).
;   0 = editor origin, 1..N = RCS_VictimAltSpot markers, -1 / x = random pick
; slots = letters in the map images: 1st value = victim A, 2nd = B, ...
bar_r = 0, -1, 0, 0, 2, 0
```

- Hose maps (`city_h`, `const_h`, `dock_h`, `oil_rig_h`) have no
  `RCS_VictimRescue` victims, so they aren't listed.
- An old `FDNYVictimSpawns.ini` still works. If there's no
  `FDNYDeterministic.ini` it is read instead, and its `[Settings] Enabled` key
  maps to `[StaticVictimSpawns]` (its `StopRoam` key is ignored).
- With no ini at all you get the defaults: bot off, chatter muted, nothing
  pinned.
- Which index is which victim/spot: `tools/victims_reference.md` and
  `tools/spawn_maps/`.

## How it works

**RescueBot:** two 1-byte code patches, no hooks:

| Site | Patch | Effect |
|---|---|---|
| `RescueBot::TryStartRescue` 0x00455230 | `6A` → `C3` (`ret`) | It is the only caller of `BeginRescue`. Both of its callers ignore the return: `Think`'s difficulty-capped timer (which the dev F6 key also uses) and the `runrescuebot` verb. No bot claim can happen. |
| `RescueBot::Construct` 0x004514F0 `mov byte [esi+0x18],1` | imm8 @ 0x004514F3 `01` → `00` | Mutes the chatter. `enabled = 0` is the state `training.bsp`'s `setrescuebot 0` ships with: `Think` early-outs, so the chatter stops too. |

With the bot gone, both win gates reduce to *you delivered every victim*.
The mission timer, O2, burn and difficulty scaling are untouched.

**Spawns:** one inline hook in `VictimRescueEnt::SpawnAtSpot` at 0x0045D717.
It runs after the game has copied the RNG-chosen spot, but before its
`SnapToFloor` call, and swaps in the configured spot's coordinates. The game
then floor-snaps the victim itself. The RNG stream is never touched.
Facing still follows the RNG pick. Victim ordinals reset on a >2 s gap
between spawns, which happens on every level load.

**Safety:**
- Every patch site is signature-checked first. The RescueBot patches are
  all-or-nothing: on a different game build, nothing is written.
- Already-patched bytes are accepted, so a second copy of the plugin is harmless.
- The two features fail independently.
- Everything is applied in `DllMain`, before the first level is constructed.

## Status

- **Verified (test host):** loading, config parsing (new ini, legacy ini, no
  ini), and every patch landing on the real game bytes mapped at 0x00400000.
  Also verified (v0.1): the bot left alone when not disabled, the wrong-build
  abort, and the second-copy idempotency.
- **v0.2 changes:** `[RescueBot] Disable` became `Enabled` (default false),
  `MuteChatter` is no longer configurable (always applied with the disable),
  `[VictimSpawns]` became `[StaticVictimSpawns]`, and `StopRoam` was removed
  because it didn't work.
- **Not verified in-game:** the game doesn't run on the VM. To test, play
  `bank` on the easiest difficulty and stay away from victims you haven't
  found for more than 2 min. You should see no "RESCUEBOT:" subtitle, and you
  should have to carry everyone out. Also check that pinned victims appear
  where the ini says (the log shows `FORCED spot` lines).

## Build

```powershell
.\build.ps1                         # FDNYDeterministic.asi
.\build.ps1 -Name FDNYVictimSpawns  # the old spawns-only plugin
```

Uses the bundled Zig toolchain to build a freestanding 32-bit PE32 DLL that
imports only `kernel32` + `user32`. Background and evidence:
`PLAN-norescuebot-merge.md`, plus research.db KB `rescuebot`.
