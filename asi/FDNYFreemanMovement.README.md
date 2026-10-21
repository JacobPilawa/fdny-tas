# FDNYFreemanMovement.asi

Half-Life 1/2 player movement (bunnyhop, air-strafe, ducking, fall damage) in
**F.D.N.Y. Firefighter**, via [gaycoderprincess/FreemanAPI](https://github.com/gaycoderprincess/FreemanAPI).

> **v0.2 — single-physics in-Think splice.** Rewritten on a full RE map of
> FDNY's movement (see `research.html` KB article "FDNY player movement
> architecture"). Instead of running FreemanAPI *on top of* FDNY's movement and
> overwriting the result, v0.2 **splices FreemanAPI in where FDNY's own
> locomotion step runs**, so there is one physics system, not two.

## Install

Drop next to `dinput8.dll` / `FDNYFirefighter.exe`:

```
FDNYFreemanMovement.asi        <- the FDNY <-> FreemanAPI glue
FreemanAPI_gcp.dll            <- FreemanAPI itself (official v1.60 prebuilt: kernel32 + msvcrt only)
FreemanAPI_gcp.toml          <- FreemanAPI's tuning config
```

On launch it writes `FDNYFreemanMovement.log`.

## Use

- **Insert** toggles HL movement on/off, live. With it **off, the game is
  byte-for-byte vanilla** — the glue replays FDNY's exact original locomotion
  step. (F8 is FDNY's dev console — don't use it.)
- **`/`** dumps a state line to the log (position, velocity, 2D speed, yaw,
  pitch, onground).
- Movement keys come from **FDNY's own controls** (`MOVE_FORWARD` etc.), so
  they follow whatever the game is bound to. HL **sprint** has no FDNY
  equivalent and stays on **Shift** (read directly off the keyboard).
- Mouse-look is FDNY's; the glue reads your view angle from the entity each
  frame.

## How it works (v0.2)

- **Splice point.** Byte-patches `PlayerRescueEnt::Think` at
  `0x00453789`–`0x00453815` (141 bytes — the contiguous *integrate velocity →
  resolve against world → commit position* span, also the `state==7` entry).
  The patch is `pushad; pushfd; lea eax,[esi-0x140]; push eax; call v2_stub;
  add esp,4; popfd; popad` then NOP-fill to `0x00453816`. `ESI` is
  `entity+0x140` throughout `Think`, so `eax` = the entity base.
- **`v2_stub(entity)`:**
  - **HL path** (state 3 stand / 4 crouch, not spectator, not move-frozen,
    toggle on): keeps its own authoritative position/velocity (`g_pos`/`g_vel`,
    seeded from the entity and re-seeded on any >500u jump); folds FDNY's
    blast-impulse accumulator (`entity+0x5b8`) into velocity; calls
    `FreemanAPI_Process(dt)`; writes back `entity+0x4c` (position),
    `entity+0xfc` (velocity), `entity+0x90` (this-frame delta — the animation
    code after the span reads it), and a fresh `entity+0x12c` onground byte
    from a short down-trace.
  - **Everything else** (states 5 climb / 6 passed-out / 7 scripted,
    spectator, move-frozen, toggle off): replays the vanilla 4 steps exactly —
    `vel += impulse·dt`; `CALL BModelEnt::IntegrateVelocity_thunk 0x0043f680`;
    `CALL BModelEnt::DepenetrateFromWorld 0x0043f6d0` unless spectator;
    `pos += delta`.
- **View angle.** FDNY yaw/pitch (`entity+0x3434`/`+0x3430`, degrees, +pitch =
  up) → FreemanAPI's default rotate order (index 0 = YAW, index 1 = PITCH) as
  `array[0] = -yaw`, `array[1] = -pitch`, `array[2] = 0`. Derived closed-form
  from `Xform_BuildYawBasis` vs FreemanAPI's `AngleVectors` / `NyaMat4x4::Rotate`
  — no tuning knobs.
- **Origin.** FreemanAPI game-Y = FDNY entity-Y **+ 6** (feet = entity − 30;
  FreemanAPI's built-in HL2 hull centre-up = 36; 36 − 30 = 6).
- **Camera** needs no extra work — `UpdateLocomotionAndTools` re-derives the
  eye from `entity+0x24` (whose translation *is* the position) *after* the
  span, so writing `entity+0x4c` is sufficient.
- **Collision** bridges to Genesis3D `geWorld_Collision` (IAT `0x004dc0bc`,
  `__cdecl`, Contents `0x43`, CollideFlags `2`). World pointer:
  `roc = *(*(void**)0x00503a78 + 4)`; `tw = roc + (roc[0x160] ? 0xb4 : 0x08)`;
  `world = *(tw + 0x78)`.

## First run — what to check, in order

1. **Loads, no crash, log shows** `v0.2 (in-Think splice)`, `splice installed
   at 0x00453789 (141 bytes)`, `ready`. If the splice aborts, the log says why
   (unexpected bytes = wrong build / wrong exe).
2. **Toggle off = exact vanilla.** Insert twice, play normally — should feel
   identical to no mod.
3. **Toggle on: you stand on the floor** (no hover / no sink — the `+6` origin
   constant handles it) and **W moves you where you look, A/D strafe.** If
   forward/back and strafe are swapped or mirrored, it's a sign flip in
   `cb_getview` / `cb_side` — tell me which and I flip one line.
4. **Bhop / air-strafe** accelerates past FDNY's walk cap. Tune
   `sv_airaccelerate` / `sv_maxspeed` / friction in `FreemanAPI_gcp.toml`.
5. **Crouch, ladders, passed-out, mission-end** — ladders and scripted movers
   should behave exactly as vanilla (they fall through to FDNY's physics).

## Known limitations (v0.2)

- **Hull.** FreemanAPI's `SetPlayerBBoxes` rewrites the player hull from its
  own constants every `Process`, so v0.2 runs with FreemanAPI's HL2 hull
  (`±16`, `0..72`) rather than FDNY's real `85`/`25` heights. The `+6` origin
  constant absorbs the standing-height difference. Patching `pm_hullmaxs_hl2`
  in the DLL for FDNY's exact box is a later refinement.
- **No step-up.** FDNY has zero stair-stepping and its swept box stops at every
  lip. HL step traces (`PM_PlayerTraceDown`-based) aren't wired yet — small
  ledges will block you.
- **Jump sound** may still come from FDNY's `Verb_Jump` even though FreemanAPI
  owns the jump impulse (cosmetic).
- **Onground** for the animation state is a synthesised 4-unit down-trace, not
  FDNY's own resolve — walk/idle/fall animation should be right, edge cases may
  flicker.

## Build

```powershell
.\build_freemanmovement.ps1          # -> FDNYFreemanMovement.asi  (zig, kernel32+user32)
```

`FDNYFreemanMovement.cpp` is a single freestanding file. FreemanAPI source is
vendored under `freeman/` for the optional DLL rebuild
(`freeman/build_freeman.ps1`).
