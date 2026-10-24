# hooks_toggle crash (Debug core), 2026-10-24

Found by the full suite run against PR #2 (the recorder). The recorder was not the cause.

**Symptom:** `tests/hooks_toggle.yaml` turns the frame hook group off and on 50 times during
a level, and the game died with an unhandled exception every time:
- on the PR build (3 of 3);
- on `fdny-bxt` without the recorder.

The core's crash log:

```
crashlog: exception c0000005 at ... (FDNYCore.asi+ba84); data 00000001
crashlog: eax=00000001 ...    stack: Genesis.dll+4524e / +39b7d / +39c38 / +3ec82 (a render frame)
```

**Bisect** (Debug build + `hooks_toggle`, real game):

| Commit | Result |
|---|---|
| 88f7cfc FDNYCore 0.18.1 | PASS PASS (last suite pass: 0.18.0, `fire_juggle_overlay/suite_0180.md`) |
| f83784f owner fixes | PASS |
| 3550c56 trigger boxes drawn right after the world (D39) | PASS |
| **549b1fe named draw layers, gfx owns the draw order (D40)** | **FAIL** (first bad) |
| a23134c (fdny-bxt head) | FAIL |

**Cause:**
- The faulting instruction is in `gfx::DrawBoxes`, reading `box.min[0]` of a box list at
  address 0x1. A box-list pointer had been overwritten.
- D40 added the Debug-only layer-order tracker `MarkLayer`. It appends "world", "overlay"
  and "hud" to `g_order[32]` with an unbounded `lstrcatA`, and `FrameBegin` resets the buffer.
- With the frame hooks off, `FrameBegin` doesn't run, but gfx's `RenderWorld` detour (its own
  group, still on) keeps calling `MarkLayer("world")` every frame.
- After a few frames the string runs past its 32 bytes into the neighbouring globals: the box
  lists `DrawBoxes` reads.
- Release builds have no `MarkLayer`. The shipped `.asi` is not affected.

**Fix** (`gfx.cpp` `MarkLayer`): append only when the name fits. The order string is a
Debug-only diagnostic (the `layers` pipe, `tests/draw_layers.yaml`).

**After the fix:** `hooks_toggle` PASS x3; `draw_layers` PASS (the recorded layer order is
unchanged).
