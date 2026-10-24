# Suite 0.18.0 (2026-10-22, Debug, tag s0180)

Suite run: 31 of 32 PASS (`core/runs/*-s0180`).

| Test | Result | Note |
|---|---|---|
| fire_juggle | FAIL, then fixed | `selfcheck = 1`: the test hook, not the feature (see below). After the fix: 3 of 3 PASS (`*-fire_juggle-fix1`). |
| everything else | PASS | Includes glitch_box_cost, text_panels and state_snapshot, which have failed on the slow VM before (CLAUDE.md "known environmental"). This run they passed. |

## The fire_juggle failure

From the log of `2026-10-22-175158-fire_juggle-s0180`:

```
juggle: setpos -697 -47 1196 (+yaw) (+o2): ok      <- O2 0.1: locked
cmd: fd_dbg_o2 1
juggle: o2 100/100: ok                             <- written from the console, mid-frame
juggle: safe spot moved while locked (-697 -48 1196 -> -697 -48 1196)
juggle: following
```

- **What happened:** `fd_dbg_o2` wrote O2 straight from the console command, partway
  through a frame. When that landed before the player's Think, Think saw O2 = 1 and
  correctly refreshed the safe spot (by a fraction of a unit). The overlay's last
  frame-end reading still said "locked (O2 0.1)", so the self-check counted it.
  Earlier runs had the command land after Think.
- **Fix:** `fd_dbg_o2` is now applied at frame end, before the gate is read, as
  `fd_dbg_setpos` already was. A test hook can no longer change the game's inputs
  between the overlay's reading and Think. The feature and the test are unchanged.
- **Reruns after the fix:** 3 of 3 PASS: selfcheck 0, outmiss 0, outframes 207 / 94 / 213.
