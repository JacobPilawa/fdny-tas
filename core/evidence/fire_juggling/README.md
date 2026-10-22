# Fire juggling: spike (2026-10-22)

The question came from runners' Discord notes: when you pass out from fire while
carrying a survivor, where does the survivor land? The full write-up is research.db KB
article `fire-juggling`, with the same text in `kb_fire_juggling.md`.

**How it was run.**
- **Static pass:** read-only Ghidra lookups by a haiku subagent of `TakeFireDamage`,
  `PassOut`, `UpdatePassedOut`, `Verb_Reset`, `DropAtPlayer`, `SnapToFloor`, the
  writers of `+0x3400`, and the fire touch callback.
- **Live pass:** `core/probes/firejuggle_probe.py` (pymem, outside the game) ran beside
  `core/tests/probes/probe_firejuggle.yaml`. The test loads bank with `fd_rescuebot 0`
  and holds it open. The probe moves the player by writing the position and zeroing
  velocity, displacement and impulse, the way `fd_tp_load` does.
  - Phase D also writes `+0x3400` while the player is passed out. That write is a test
    input, not part of the mechanism.
- **Runs:**
  - `run1.log` and `run2.log`: no fire was found. The probe filtered on mode 4 and read
    fire positions at `+0x4c`. In fact all 85 to 88 fires read mode 0 at about 50 s,
    and a fire's centre is at `+0x7c`.
  - `run3.log`: the full result.

**Verdict: confirmed.** The survivor lands on the player's last safe spot, `+0x3400`,
minus 30 on X, standing on the floor below that spot.
- The spot is saved each frame the player is grounded, has O2 above 0.5, has no fire
  contact, and has a burn meter below 0.1.
- After a burn, the spot stays frozen until the meter cools below 0.1. The meter cools
  at 0.1 per second (measured: the spot moved after 2.12 s, starting from 0.307).
- Juggling keeps the spot frozen. If no floor is found below the spot, the survivor
  goes to (0, 1, 0), then -30 on X.

**run3 numbers (bank):**

| Phase | Safe spot at pass-out | Survivor after reset |
|---|---|---|
| A plain | (144.0, 250.0, -1324.0), the fire arrival point (one clean frame) | (114.0, 217.1, -1324.0) |
| B juggle | (114.0, 235.6, -1324.0), the pre-fire spot, kept through 2 s on clean ground | (84.0, 221.1, -1324.0) |
| C control | the spot moved to the clean ground at burn 0.098; back in the fire: (144.0, 250.0, -1324.0) | (114.0, 217.1, -1324.0) |
| D1, written: spawn + 600 up | (-697.0, 552.2, 1196.0) | (-727.0, -52.9, 1196.0), floor below; picked up again at respawn (state 5) |
| D2, written: outside the map | (29303.0, -47.8, 31196.0) | (-30.0, 1.0, 0.0), the origin bug |

Not tested: the order of the fire message and Think within a frame; the sender of
0x7530; hardest difficulty's herodown spawn; crouchbug; a real out-of-bounds spot.
