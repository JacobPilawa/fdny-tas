# glitch_box_cost and text_panels without a frame-rate threshold (D46)

## Why they failed

Both tests asserted an absolute count over 6 s: at least 250 simulated ticks and 300
frames. `history.txt` lists every run of them since 2026-10-21. Each lands near one of two
values, never between:

- about **345** (57 fps): PASS;
- about **205** (34 fps): FAIL.

The same builds landed on both. 0.18.x passed at 16:41 on 10-21 and failed at 22:25 the
same day, with no core change in between.

On 2026-10-24 the desktop session was a remote display session. Its adapter, Microsoft
Remote Display Adapter, reported a **32 Hz** refresh, and the game locks to the refresh
rate. `glitch_box_cost` failed with the boxes hidden too (+213, against +216 shown), so
the features weren't the cause.

## The redesign

The owner asked whether the tests had to depend on frame rate. They don't.

- **glitch_box_cost**
  - ticks with the boxes shown >= 0.9 x ticks with them hidden (B), in 6 s windows back
    to back, so the refresh rate cancels out. The old geEngine_Printf boxes would score
    0.24 (84 / 346);
  - the game-thread box build (`glitch` `draw_us_avg`, CPU time) <= 1000 us;
  - a floor of 60 ticks per window, to show the game is running at all.
- **text_panels**
  - frames with the warp HUD and the player HUD on >= 0.9 x frames with them off (back
    to back);
  - the panels are drawn (`textdraw <name>` `drawn=1`);
  - a floor of 60 frames per window, in Training and in Bank.
  - Unchanged: B hides the boxes, the panels survive the load into Bank, and the
    screenshots.

There's a new Debug pipe, `textdraw_cost` (`frames`, `us_total`, `us_max`), with the HUD
layer's time per frame. Both tests **record** it, but neither asserts it. It measured
about 2.5 ms per frame in Training at 34 fps, with no frame-rate cost (ratios 0.95 to
1.0). It includes `geEngine_FlushPipeline`, which can wait on the GPU, so the display
moves it too.

A new harness step, `assert_sample_ratio`, divides one sampled counter's change by
another's.

**Limit:** while the refresh rate caps the game, a cost that fits in the frame's spare
time doesn't lower the frame count, so the ratios catch only costs larger than that slack.
The Printf regression (to 14 fps) is far larger.

## Runs, 2026-10-24, Debug, on the 32 Hz remote display

| Test | Result | Numbers |
|---|---|---|
| `glitch_box_cost` | PASS | shown/hidden 205 / 207 = 0.990; draw_us_avg 60 |
| `text_panels` | PASS | on/off 202 / 212 = 0.953 |

The first run, which had a 2000 us budget on `textdraw_cost`, failed only that budget: 2544
and 2662 us per frame in Training, 2019 in Bank. Its other checks passed, and that budget
was then dropped as display-dependent.

Files: `history.txt`, `glitch_box_cost_result.json`, `text_panels_result.json`.
