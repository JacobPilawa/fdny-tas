# 0.19.0 release: full suite

Debug build of `release-0.19.0` (VERSION 0.19.0), 2026-10-24, on the VM.

**36 of 38 PASS.** The two failures are the documented slow-VM frame-rate thresholds
(`core/CLAUDE.md`, "Done means", known environmental):

| Test | Suite run | Rerun | `main` at 3c5aa54 (before D43/D44) |
|---|---|---|---|
| `glitch_box_cost` | +216 / +225 ticks, want >= 250 | +207 / +213 | +219 / +221 |
| `text_panels` | +208 / +194 frames, want >= 300 | +205 / +196 | +213 / +204 |

They fail by the same margin on the earlier source, so the cause is the VM running slow
(the thresholds assume ~57 fps; `core/CLAUDE.md` records ~35 fps on a slow VM), not this
release. The frame rate wasn't measured separately in this run.

Every other test passes, including the new `core_single_instance` and the reworked
`speedrun_static_spawns`. On the earlier `main` run, `state_snapshot` (known race) and
`draw_layers` (one start-up hang, PASS on rerun) also failed; both pass here.

**Follow-up (owner, 2026-10-24):** the two tests were redesigned so they no longer depend
on the display's refresh rate (D46, `evidence/fps_independent_tests/`). Rerun on the same
32 Hz display, both PASS, which makes the 0.19.0 suite 38/38.

Files: `suite_0.19.0.txt` (this run), `suite_main_3c5aa54.txt` (the earlier `main` run).

_The logs and run output named above were removed on 2026-10-27 (repository tidy-up); git history keeps them._
