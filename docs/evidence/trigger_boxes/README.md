# Trigger-box overlay: spike (2026-10-22)

Probe `core/probes/triggerprobe.cpp` → `zz_triggerprobe.asi`, built by
`core/probes/build_probes.ps1`. Tests `tests/game/probes/probe_trigger.yaml` (Training,
then bank) and `probe_trigger_bank.yaml`; both PASS (no crash, 0 error lines). Logs and
screenshots are in `spike/`. Working code is untouched.

**Verdict: the approach works.** Every trigger in a loaded map can be found, classified
and drawn from live memory, with 50% boxes, depth-tested or x-ray, at about 1 ms per frame.

## Findings

| Question | Answer | Evidence |
|---|---|---|
| Which entities are triggers? | vtable `0x004E1090` (`BoundsTriggerEnt::Construct` 0x00441CA8). Training: 25 of 70 entities; bank: 14 of 203. They are the only entities with a sane box at +0xe4. | `*_triggerprobe.log` "class vtbl" lines |
| Kind | **Every** BoundsTrigger has kind 8: the constructor writes it unconditionally (0x00441D2F). research.db's "8 = final-exit trigger" is wrong; kind can't separate exits. | Ghidra (subagent), dump |
| Box at +0xe4 | min xyz, max xyz (`0x004817A0` reads min +0/+4/+8, max +0xC/+0x10/+0x14). Filled in Spawn from the model's bounds (model+0x8C/+0x98, inset). | Ghidra |
| World space? | **Yes.** Drawn boxes line up with the level geometry (corridor walls, the doorway floor in `training_look_right.png`, the street in `bank_*`), and the player's spawn point lies inside the "Outside" and "Bank" zones in bank. | screenshots; "player ... inside" log lines |
| Script lines | User data `P = [e+0xFC]`: +4 name, +8 model, +0x10..+0x34 ten `char*` lines of the form `event: command` (`touch: tell player escape`, `makezone (Vault)`, ...). | dump "trig ... line" |
| Rescue zone | Lines `touch: tell player escape`. The game marks them itself: byte **+0x164** = 1 exactly on those triggers (the result of Spawn's bind of `touch` → `tell player escape`). Training 29, 32, 46, 47, 53; bank 58, 59. | dump |
| Final exit | A trigger with `touch: tell player finalexit` (bank 58: escape + finalexit). It also has +0x164 = 1. | dump |
| Open / closed | Byte **+0x165**: `BoundsTrigger_SetEnabled` writes it, gated on +0x164; `UpdateZonesAndWin` calls `SetEnabled(1)` on the exits once every victim is delivered. 0 on every trigger at level start. The 0→1 change is to be shown by the suite test. | Ghidra, dump |
| Other triggers | `makezone (<room>)` zone volumes (oxygen/fog areas, room names), hints, doors (`tell column open`), sounds. | dump |
| Translucency | A white 64×64 4444 tile at alpha 8/15 with vertex colours: the tint works (`training_m2_vertexcolour.png` = per-colour tiles `m3_colourtex.png`). One tile serves every colour. | screenshots |
| Hidden by walls? | **Both are possible.** Immediate polys (flags 1) skip the z-test: x-ray, boxes show through walls. Queued polys (flags 0) with z = depth × ZScale (`geCamera_Project`'s out.z) are depth-tested against the world: boxes behind walls are hidden (`bank_occl_*` vs `bank_xray_*`, `training_m3_queued_z_depth`). | screenshots |
| Inside a box | A box around the camera tints the whole view (`training_bank.png`: the spawn stands inside "Outside" and "Bank"). | screenshot |
| Cost | 25 boxes (150 faces, up to 19 visible): ~1.1–1.4 ms/frame. 500 boxes: ~5 ms. Probe reads every field with `ReadProcessMemory`, a syscall each, so the feature (direct reads) will cost less. VM ran at ~32–37 fps throughout, the usual slow VM. | "cost:" lines |

## Consequences for the feature

- Find triggers by vtable (an addresses.json entry with a signature), not by kind.
- Rescue zone = +0x164 set and no `finalexit` line. Exit = a `touch: tell player finalexit`
  (or `finalexitloadmap`) line; red while +0x165 = 0, green when 1. Everything else is
  "other" (grey, hidden by default).
- Draw through `gfx` with one 50% white tile and vertex colour, clipped to the near plane and
  the view; default depth-tested (queued), with an x-ray toggle.
- Boxes containing the camera are drawn too (it's how you see you're in a zone), at a lower
  alpha so stacked zones don't wash out the view.

## Found while building the feature

- **Opening the exit in a test:** the game's `runrescuebot` script verb makes the RescueBot
  claim one victim per call, bypassing its enabled flag and its cap. `rescued` stays 0 (bot
  claims count by omission); the exit opens through `UpdateZonesAndWin`'s gate (delivered >=
  total - claimed) and `SetEnabled(1)`, the game's own path (`tests/game/triggers.yaml`).
- **Mouse aiming isn't repeatable in tests:** a 900 px move pitched the view to the sky, and
  4 x 300 px landed about 90 degrees short. The test walks into the boxes instead.
- **A new level can reuse the old world's address:** bank's world was allocated at Training's,
  so a pointer comparison never re-attached gfx's box tile (D30).

_The logs, screenshots and run output named above were removed on 2026-10-27 (repository tidy-up); git history keeps them._
