# Fire juggling -- where a dropped survivor lands after a fire pass-out

**Runner description (Discord):** if you pass out from fire while carrying a survivor,
the survivor appears "at the exact coordinates of the last spot you were before you
took damage". Taking damage from several fires in a row keeps those coordinates
locked, hence "juggling". Runners use it to put a survivor next to a rescue trigger,
and as a way to get crouchbug. **Verdict: EXISTS, STABLE.** The mechanism below is
from the decompiled code and was confirmed live on bank (2026-10-22,
`core/evidence/fire_juggling/`).

## The mechanism

**1. A "last safe spot" at `PlayerRescueEnt+0x3400`.** `PlayerRescueEnt::Think`
(0x00452570) copies the player position (`+0x4c`) into `+0x3400/04/08` on every frame
where **all** of these hold:
- no fire flinch flag this frame (`+0x353c..+0x353f` all 0; `TakeFireDamage` sets them,
  Think clears them at the end of each frame);
- the burn meter `+0x3538`, after cooling by `0.1 * dt`, is **below 0.1**;
- the on-ground byte `+0x12c` is set;
- O2 `+0x3450` > 0.5;
- not passed out (state `+0x18c` != 6) and the mission timer is still running.

The meter only cools on frames with no flinch flag (the cooling is in the same
short-circuit chain), at 0.1 per second.

**2. Burning.** A `FireRescueEnt` (kind 6, centre at `+0x7c/+0x80/+0x84`) queues message
0x7531 to an entity touching it (touch callback 0x004483a0, vtable slot 0x12; mode 5 =
doused fires ignore it). `PlayerRescueEnt::HandleMessage` (0x00453a70) passes it to
`TakeFireDamage` (0x00451640): flinch flag, `+0x3538 += dt * 1.2 * severity`, and
`PassOut(1)` once the meter is over 1.0. Torch / flame emitters
(`MiscEmitterSoundImpl::OnTouch` 0x00445490) call `TakeFireDamage` directly.
`PassOut` (0x00450fb0) only sets state 6 and a timestamp; it does not move anything.
Message 0x7530 cools the meter at 3x speed (0x0044bc00); its sender was not found.

**3. The drop.** `UpdatePassedOut` (0x00451080) fades for 4000 ms, then calls
`Verb_Reset(0)` (0x0044ff10). In order, Verb_Reset:
1. copies the safe spot `+0x3400` into the player position;
2. if carrying (`+0x3534`), calls `VictimRescueEnt::DropAtPlayer` (0x0045c1a0), its only
   caller. That copies the player position (`vtable+0x18` returns `this+0x24`, so
   `+0x28` there is `+0x4c`) into the victim, then `SnapToFloor`;
3. moves the victim -30 on X (and the player +30);
4. then `ResetLocomotionState` (0x0044cd10) puts the player on the real respawn
   checkpoint `+0x534` (which only `Verb_Escape` moves), zeroes the burn meter and
   mirrors the checkpoint into `+0x3400`.

So the survivor lands on the safe spot, not where you passed out and not at the
respawn checkpoint. (Older notes call `+0x3400` "the rolling checkpoint"; the respawn
itself uses `+0x534`.)

**4. The floor search.** `ModelEntity::SnapToFloor` (0x0049da00): lift the point 48 u,
then sweep a box 1000 u straight down (`ROCRender::TraceToFloor`, contents 0x43) and
stand the survivor on the first hit (+1 u). All three coordinates of the safe spot
are used; the "search" is only downward from 48 u above it. On a miss the zeroed
locals are committed: the survivor goes to world origin, (0, 1, 0), then -30 on X.

## Live confirmation (bank, RescueBot off, probe `core/probes/firejuggle_probe.py`)

| Test | Result |
| --- | --- |
| Safe spot follows the grounded player | `+0x3400` = position on every grounded frame with no burn. |
| Plain pass-out | Burn 0 -> 1.0 in about 2 s on a fire; state 6; 4 s later the survivor was at **safe spot -30 X**, on the floor (safe spot y 250 -> survivor y 217.1); the player respawned at the checkpoint. |
| **Juggle** | Burn to 0.45, step out onto clean ground for 2 s (meter 0.45 -> 0.28 at 1.75 s): the safe spot **did not move**. Back into the fire, pass out: the survivor landed on the **pre-fire** spot (-30 X), not the clean ground. |
| Control | Burn to 0.31, step out: the safe spot jumped to the new ground after **2.12 s**, the frame the meter fell below 0.1 (0.098), i.e. 0.1 per second. |
| Floor search | Safe spot written 600 u above the spawn while passed out: the survivor landed on the floor below (y -52.9). |
| No floor | Safe spot written outside the map: the survivor went to **(-30, 1, 0)** = the origin bug, -30 X. |

## What this means for runners
- **Locking the spot.** After any fire hit, the spot stays frozen until the meter
  cools below 0.1, at 0.1 per second outside fire. The meter goes up by
  `1.2 * severity` per second in fire; passing out needs 1.0. Hopping between fires
  ("juggling") keeps it above 0.1 indefinitely while you reposition.
- **What gets saved is the last *clean grounded* frame**, not literally "before
  damage". Airborne frames and O2 <= 0.5 also freeze it. A warp that lands you in a
  fire saves the arrival point if one clean grounded frame happens first; the probe's
  teleport did (the on-ground byte was still set from before the teleport), so the
  spot became the arrival point, 30 u above the fire's floor.
- **The fire doesn't need to be near the rescue trigger.** Set the spot near the
  trigger, then take the damage anywhere.
- **Out of bounds.** From an out-of-bounds safe spot the survivor is stood on the first
  solid surface (mask 0x43) within about 950 u below it. With playable floor below, it
  lands in play space. Anything solid in between catches it, so holding it out of
  bounds with an object below is consistent with the code (untested). With nothing
  below, it goes to world origin.
- **Re-pickup trap.** Pickup is automatic on contact. A survivor dropped within reach
  of the respawn checkpoint is picked up again as soon as you respawn (seen live).
- **Crouchbug.** The pass-out ends with a respawn through `ResetLocomotionState`; a
  checkpoint respawn while crouched is a known crouchbug entry (see `speedrun-tech`).
  It is not tested here.

## Untested / open
- Within one frame, does the fire message run before or after Think? This could
  shift the saved frame by one.
- The sender of 0x7530 (faster cooling); probably water.
- Hardest difficulty: `Verb_Reset` can also spawn a "herodown" survivor at the reset
  position.
- A real out-of-bounds safe spot (the probe wrote `+0x3400` directly).
