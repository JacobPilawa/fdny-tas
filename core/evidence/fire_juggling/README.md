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

## Round 1

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

## Rounds 2-3 (the round-1 unknowns)

- **Static:** five more haiku Ghidra passes:
  - frame call chain;
  - 0x7530 senders (MiscEmitterFXImpl::OnTouch, extinguisher, hose douse);
  - Genesis `geWorld_Collision` content bits (0x4 = actors, skipped for 0x43);
  - herodown gate (`[[0x502AAC]+0x6E4]+0x2C == 2`, Verb_Reset arg 0);
  - stance/box fields (`+0x18C`, `+0x88`);
  - liftable and emitter vtables.
- **In-process hooks:** `probes/juggleprobe.cpp` -> `zz_juggleprobe.asi` (built by
  `build_probes.ps1`, image base 0x3E400000), installed by
  `tests/probes/probe_firejuggle2.yaml` / `probe_firejuggle3.yaml`.
  - All 8 MinHook hooks: `MH_OK`.
  - The raw logs are 28 MB+ and stay in `core/runs/` (not committed). The condensed
    per-frame view is `round2_frame_order.txt`.
- **Drivers:**
  - `firejuggle_probe2.py`: `round2_run1.log`;
  - `firejuggle_probe3.py`: `round3_run1.log`;
  - `firejuggle_probe3b.py`: `round3b_run1.log`, burn under the type-2 emitters;
  - a one-off hover check: `round3c_hover.log`, the player held at the emitters;
  - `firejuggle_probe3d.py`: `round3d_liftable.log`.

| Question | Result |
|---|---|
| Frame order | **Resolved.** RunFrame -> Think -> fire touches -> TakeFireDamage, every frame. Think saves the pre-move position, so the spot is the start of the last frame without contact (`round2_frame_order.txt`, episode 2220: saved -59.7, first damage at -39.3). Reset frames show DropAtPlayer entered with the player already on the safe spot. |
| Crouchbug | **Confirmed.** After a ducked pass-out: state 3, box top 25, 175 u in 0.5 s. C, C: top 85. |
| Herodown | **Confirmed.** With the difficulty field at 2: a 7th victim, "herodown" / "POFighter3", at the safe spot +30 X; total 6 -> 7. |
| Actor catch | **No.** The drop went through a victim to the floor. |
| 48 u lift | **Seen.** A spot 39 u under the upper floor put the survivor on top of it. |
| Liftable catch | **Partial.** 2 of 5 tests 86 u above the side control; 3 of 5 floor. Not separated from furniture. |
| 0x7530 source | **Not reached live.** The cool-down and AddHandler hooks never fired. The plain 0.1/s cooling held at, under and around both bank type-2 emitters. |

Not tested: moving a liftable away and repeating; the extinguisher or hose aimed at
yourself; type-2 emitters on other maps; a real out-of-bounds spot.
