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

## Round 2-3: the follow-up questions, answered live

A second probe hooks the game in-process (`core/probes/juggleprobe.cpp` ->
`zz_juggleprobe.asi`: MinHook, register-preserving detours on RunFrame, Think,
TakeFireDamage, the fire touch, Verb_Reset, DropAtPlayer, the cool-down and
AddHandler). It logs, per frame, the order of events with the player fields at each
event. Drivers: `firejuggle_probe2.py`, `firejuggle_probe3*.py`.

**Frame order: Think, then fire touches, then damage, every frame.**
`RunFrame -> Think -> fire touch callbacks -> TakeFireDamage` (the queued 0x7531 is
delivered after Think in the same frame). Walking into a fire with W (no teleport,
frames 2217-2221):

| Frame | Think sees pos | Saved spot after Think | Damage at pos | Burn |
| --- | --- | --- | --- | --- |
| 2219 | -71.0 | -71.0 | none | 0 |
| 2220 | -59.7 | **-59.7** | -39.3 (first touch) | 0 -> 0.004 |
| 2221 | -39.3 | -59.7 (flinch set: no save) | -28.0 | 0.009 |

Think saves the position **before** that frame's movement, and the damage comes after
the move. So the spot is where you stood at the start of the last frame without fire
contact, one frame of movement (about 11 u at a run) outside the burning area. That
is literally "the last spot before you took damage".

**Crouchbug: confirmed.** Duck (state 4, box top +0x88 = 25), pass out, respawn:
state 3 (standing) with box top **still 25**. It moved 175 u in 0.5 s with W, i.e.
standing speed. C once: state 4, top 25. C twice: state 3, top 85, fixed.
`ResetLocomotionState` sets the state to 3 and never writes +0x88.

**Herodown: confirmed.** Difficulty field `[[0x00502AAC]+0x6E4]+0x2C` (1 on this
save) set to 2 for one pass-out. Afterwards there was a 7th victim: type "herodown",
name "POFighter3", at **(90, 221.1, -1200)**, i.e. the safe spot +30 X. Verb_Reset
spawns it at the player position after its +30 X nudge. The level total `+0x3528`
went 6 -> 7. On hardest difficulty, each fire pass-out (up to 3 downed firefighters)
adds a survivor at the juggled spot, who then has to be rescued.

**Actors do not catch the drop.** Safe spot 120 u above another victim: the survivor
landed on the floor (same height as that victim's feet), not on them. This matches
the sweep's contents 0x43 skipping the actor bit 0x4 (Genesis `0x100372a0`).

**The 48 u lift goes through thin floors.** A safe spot 39 u *under* an upper floor's
surface (y 177 under 216) put the survivor **on top of** that floor (y 217.1). The
sweep starts 48 u above the spot. A spot up to 48 u below a surface lands on it.

**Liftable props: partly.** `BModelEnt_Liftable` (vtable 0x004E0DF8, kind 4, centre
`+0xD0`) are BSP sub-models, not actors. Five drops with the spot 100 u above a
liftable and a control 150 u to the side:
- **2 of 5** stood the survivor 86 u above the floor (y 47.1 vs control -38.9). That
  fits the top of an object ~86 u tall centred on that liftable's origin;
- **3 of 5** landed on the floor.

Not separated: whether the 2 hits were the liftable or furniture under it. The
decisive test is to carry the object away and repeat.

**Faster cooling (0x7530): no source reached the player on bank.** The registrations
come from three places:
- `MiscEmitterFXImpl::OnTouch` 0x00446030: an `RCS_MiscEmitter` of type 2, sent to
  kind 1 (player) or 6 (fire) within 200 u and 45 degrees;
- the extinguisher's douse (0x0046229c);
- the hose douse (0x00464b60).

The cool-down (0x0044bc00) and AddHandler hooks **never fired** during the whole
session. Bank has two type-2 emitters (positions `impl+0x28`: (-1041, 395, -700) and
(-314, 383, -714), near the ceiling). Burnt and held at, under and around both, the
meter cooled at the plain **0.098-0.100/s**. Under the first one it rose 0.28/s, but
from fire nodes below it (all damage was via the fire message). In normal play the
only cooling observed is the passive 0.1/s. Still untested: the extinguisher / hose
aimed at yourself, and type-2 emitters on other maps.

## What this means for runners
- **Locking the spot.** After any fire hit, the spot stays frozen until the meter
  cools below 0.1, at 0.1 per second outside fire. The meter goes up by
  `1.2 * severity` per second in fire; passing out needs 1.0. Hopping between fires
  ("juggling") keeps it above 0.1 indefinitely while you reposition.
- **What gets saved is your position at the start of the last clean grounded
  frame.** Walking in, that is one frame short of the first fire contact. Airborne
  frames and O2 <= 0.5 also freeze it. A teleport that lands you in a fire saves the
  arrival point: the on-ground byte is still set from before the teleport, so Think
  saves it before the touch pass hurts you.
- **The fire doesn't need to be near the rescue trigger.** Set the spot near the
  trigger, then take the damage anywhere.
- **Out of bounds.** From an out-of-bounds safe spot the survivor is stood on the first
  world brush below it, within about 950 u (the sweep starts 48 u above the spot).
  - With playable floor below, it lands in play space.
  - Actors (victims; likely any geActor) do not catch it.
  - BSP sub-models such as liftables may (2 of 5 tests, not yet separated from
    furniture).
  - With nothing below, it goes to world origin.
- **Re-pickup trap.** Pickup is automatic on contact. A survivor dropped within reach
  of the respawn checkpoint is picked up again as soon as you respawn (seen live).
- **Crouchbug: yes.** Pass out while crouched and you respawn standing with the
  crouched box (confirmed live, above). Crouch twice to clear it.
- **Hardest difficulty:** each fire pass-out also leaves a herodown survivor at the
  juggled spot (+30 X), up to 3, and each raises the level's victim total.

## Untested / open
- Whether a liftable itself catches the drop (carry it away and repeat).
- A source of 0x7530 that reaches the player (extinguisher / hose on yourself;
  type-2 emitters on maps other than bank).
- A real out-of-bounds safe spot (the probes wrote `+0x3400` directly).
