# fd_juggle 0.18.1: observed, not predicted

Owner ruling (2026-10-22): remove the conditions from the overlay. Show `-` when the game
saved the spot this frame and the arrow otherwise, so that players work out the
conditions themselves and unknown ones become visible (DECISIONS D32).

**Rule.** Following means this frame's spot (+0x3400) equals the player's position at the
end of the previous frame. Think's write copies that position: the spike measured the
saved spot as the pre-move position. Anything else counts as frozen. A write to anywhere
other than the last position is logged as `juggle: spot written elsewhere`.

## What the live runs showed (bank, RescueBot off)

| Situation | Observed | Source |
|---|---|---|
| Walking on clean ground, 266 u | No freeze on any frame (locks +0) | obs4, `assert_moved` + `locks since` |
| A jump (JUMP = right mouse button) | Frozen for the whole airborne span (about 0.75 s, `ground 0`), then 1-2 frame blips on landing with `ground 1` | `obs4_juggle_log.txt` 20:33:47.5 - 48.6 |
| Teleport by the test hook (y -47, then dropping 0.87 u to the floor) | 1-2 frame blips while settling | same log, after each `setpos` |
| O2 set to 0.10 | Frozen until O2 refills to **0.50** (about 4 s at 0.1/s), then following. This matches the decompiled `O2 > 0.5` condition | `obsfix1_o2_refill_log.txt` 21:17:56.5 -> 21:18:03.9 (`o2 50/100`) |
| Real fire, then clean ground | Frozen at the fire arrival point until the burn meter cooled, then following | obs4 |
| Pass-out | The spot stays where it was. The display follows the observed rule (`assert_juggle locked: auto`) | obs4 |
| Respawn | One `spot written elsewhere` (the checkpoint mirror in ResetLocomotionState), then following | obs4 |

The landing blips with `ground 1` were invisible under the 0.18.0 conditions model: a
condition on the on-ground byte read at frame end doesn't match what Think saw.

## Test

`tests/fire_juggle.yaml`, rewritten for the observed rule:
- **walk:** no freeze;
- **jump:** a freeze;
- **glyphs:** at 60 u, with O2 holding the spot, the player stands 30 u off it;
- **fire:** frozen, then following;
- **pass-out and respawn:** `elsewhere` >= 1.

Suite s0181: 31/32. fire_juggle failed on VM timing: a frame took ~0.9 s, so the test read
the pipe before the `fd_dbg_setpos` hook applied (check 26 showed the previous yaw's
direction).

Fixes:
- The test now waits for the hook's own log line (`wait_log since_console`, a new harness
  option) and polls for the delayed freeze (`wait_pipe`, new, 2 s limit).
- `assert_juggle locked: auto` waits up to 2 s for a still player.
- One intermediate attempt waited 10 s for a `frozen` line that never came, because the
  state was already frozen. O2 refilled and the game saved again; that attempt is how the
  O2 0.5 threshold above was measured.

After the fixes: 3/3 PASS (`*-fire_juggle-obsfixb1..3`).

Screenshots:
- `glyph_yaw000_right.png`
- `fire_out_locked.png`
- `passed_out.png`
- `respawned.png`

_The logs and run output named above were removed on 2026-10-27 (repository tidy-up); git history keeps them._
