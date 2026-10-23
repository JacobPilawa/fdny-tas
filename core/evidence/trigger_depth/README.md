# Trigger boxes: holes and boxes through walls (2026-10-23)

**Owner report, oil_rig_A:**
- (1) In the Warehouse (-921.7 766 247, crouched), the closed exit box showed in the
  lower right quarter, through a wall, and stopped along a line under the warp HUD.
- (2) Just outside the helipad rescue box (-712.9 838.4 -927.8), the blue box lost large
  screen-aligned rectangles, and the floor hid it in one place but not the next.

**Verdict: fixed** (D39). The depth-tested boxes are now drawn right after
geEngine_RenderWorld, not at the frame's late step.

## What the runs showed

Probe tests: `tests/probes/probe_trigger_depth*.yaml`,
`probe_trigger_hud_clip*.yaml`.

| Run | What it showed |
|---|---|
| `before_td1/`, `before_sheet.png` | The old path (queued polys at the late step, D27): the blue box loses screen-aligned rectangles, even over open sky |
| `experiment_modes_sheet.png` (experiment switch, since removed) | Immediate polys with real z at the late step: the **same** holes. Half the z: most holes filled, so they are depth failures. Drawn right after the world render (mode 4, right column): **no holes** |
| x-ray (no depth test) | the geometry is right: the box covers all it should |
| `after_d39/`, `after_d39_sheet.png` | The new path, 12 view directions: no holes; the box is cut only where world geometry is in front (the floor, buildings, the derrick) |
| `spot1_d39b/` | Spot (1): x-ray shows the exit box as a column at the building's corner, behind both wall faces. Depth-tested it is now fully hidden. The red the owner saw was the bug |

**Cause:**
- The game draws more after the world: sprites, particles, its own HUD.
- That writes depth over whole screen-aligned rectangles.
- Boxes depth-tested after it lost those rectangles, and could also show through walls where
  the depth buffer no longer held the world.
- The line under the warp HUD was a box edge near the screen's vertical middle; the HUD
  itself doesn't cut anything (`probe_trigger_hud_clip`, HUD on and off identical).

**Side effect, wanted:** the game's own HUD is now drawn over the boxes. A box you stand in
no longer tints it (an old known gap).

**Tests after the change:** triggers, text_panels, text_alpha, freecam: PASS.
