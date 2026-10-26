# Replay lines: cost and look before and after D50

**What ran:**
- **Recording:** the reference `mbase` recording (1391 samples, 33.8 s), played to the end at 64x with `fd_replay_xray 1`.
- **Views:** the same two each time.
  - Overview: the whole trace from above (free camera).
  - Player: the player's own view at the start.
- **Cost:** the Debug timer on world-line drawing (`gfx::WorldLineCost`, replay pipe `us` / `mean_us`), as the mean over 8 s.
- **Probe:** `replay_cost.yaml`, a scratch probe (not in the suite): it loads, positions the camera, then reads the pipe twice.

**Results:**

| View | Strips drawn, before → after | CPU per frame, before → after |
|---|---|---|
| Overview | 2665 → 525 | 3351 µs → 554 µs (6.0x less) |
| Player | 296 → 87 | 660–747 µs → 471–481 µs |

- **Points:** the line goes through 497 of the 1391 samples (Douglas-Peucker at 0.5 units, plus where it turns thin and the half-time sample).
- **Player view:** what's left there is mostly the fixed per-frame work: three lines (world, glow, x-ray copy) over 497 points, and large strips near the eye.
- **Frame rate:** frames with the trace drawn are 0.997 of frames with it cleared, the same before and after (`tests/replay.yaml`). The display's refresh cap hides a 3.4 ms cost (D46), which is why the timer was added.

**Look** (`player_before.png`, `player_after.png`, same view):
- **Before:** a 3 px line at every distance.
- **After:**
  - **Width:** 8 units held to 3–8 px, so it is wider close up.
  - **Glow and joins:** the glow sits around the line, and corners are mitered, with no notches or doubled-alpha beads.
  - **X-ray:** where stairs hide the trace, the part behind them is dimmed.
  - **Ticks and jumps:** white ticks mark each second, and jumps are drawn at half width.
- **Bug found and fixed:** the first build drew the glow before the line, and in the world layer the glow's depth hid the line. The line is now drawn first.

**Tests:**
- **Unit:** 70 checks pass, including `replay_simplify` (the dropped samples stay within 0.5 units, forced points are kept) and `replay_air_flags`. All 24 Python tests pass.
- **Game:** `replay` (new: ticks = 33; the line runs through 497 samples), `keys_default`, `draw_layers` and `docs_registry` all PASS.
- **Test change needing the owner's OK:** `replay`'s `segs` floor went from 100 to 50. The same trace now takes 87 strips in that view.

**Verdict:** the CPU cost of the trace is 6x lower over the whole trace, and the line reads better: it shows depth, it doesn't build up at the joins, and it shows hidden parts, time and jumps.
