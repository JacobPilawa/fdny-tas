# Trigger-box overlay: loop status

Owner request (2026-10-22): practice-only overlay, 50%-opacity boxes over every
`RCS_BoundsTrigger` in the loaded map, read from live memory each level (no BSP
pre-scan, no coordinate file). Blue = rescue/escape zone, red = closed final exit,
green = open final exit; other triggers hidden, a toggle shows them grey.

| Step | State |
|---|---|
| 1 Spike (`core/probes/triggerprobe.cpp`, `core/tests/probes/probe_trigger*.yaml`) | **done**: works; verdict in README.md |
| 2 addresses.json entries | **done**: 9 entries, `signatures: all OK` (addr_verified PASS) |
| 3 Feature module | **done**: `src/features/triggers.cpp`, gfx world boxes, binds::BindDefault; tests/triggers.yaml (first version) PASS 21/21 |
| 4 Suite test | **done**: tests/triggers.yaml PASS (run 2026-10-22-025643), red -> green shown; evidence `feature/` |
| 5 Suite, docs, version, commit | **done**: suite 28/31 (2 VM fps, 1 pre-existing race shown on 0.16.1; suite_0170.md); QUICKSTART, DECISIONS D24-D30, PROGRESS it25, CLAUDE.md known-environmental, VERSION 0.17.0; committed |

## Log

- it1: started. Known from research.db: BoundsTriggerEnt AABB at +0xe4 (6 floats, the
  game's own overlap test reads it), kind +0x18 (8 = final exit), enabled via
  `BoundsTrigger_SetEnabled` 0x00440f50, default touch -> "tell player escape".
  Genesis: `geCamera_Transform@12` / `geCamera_Project@12` (fastcall, out on stack;
  Project: Z = -cam.z, x = XC + S*x/Z, y = YC - S*y/Z, out.z = Z*ZScale).
  Alpha spike: textured immediate RenderPoly ORs in "no z-test, no z-write", so boxes
  probably show through walls; to verify.
- it1 (done): spike PASS on Training + bank. Triggers = vtable 0x004E1090 (all kind 8, so
  kind can't classify). Rescue = byte +0x164 (game's own "touch -> tell player escape"
  flag); exit = a `touch: tell player finalexit` script line (user data [e+0xFC]+0x10..);
  open = byte +0x165 (SetEnabled). Box world-space, min/max. One white 50% 4444 tile +
  vertex colour works. Flags 1 = x-ray; flags 0 + z = depth*ZScale = depth-tested.
  ~1 ms/frame for 25 boxes (probe uses RPM). Uncommitted: probe, build_probes.ps1, 2 probe
  tests, evidence (commit with the feature or separately at step 5).
- Next (it2, step 2): addresses.json entries: trigger vtable (sig), +0xe4 box, +0xFC user
  data + line offsets, +0x164, +0x165, geCamera_Transform/Project exports. Have a haiku
  subagent confirm what UpdateZonesAndWin's two 0x46ED70 tests pass (strings) so the exit
  rule mirrors the game's, and the +0x165 value mapping in SetEnabled.
- it2 (done): entries trigger.vtbl (sig at 0x00441CA8), trigger.box, trigger.userdata,
  trigdata.name, trigdata.lines, trigger.escape (+0x164), trigger.enabled (+0x165),
  genesis.camera_transform, genesis.camera_project (8-byte sig: the rest is an absolute
  address). Build clean; addr_verified PASS ("signatures: all OK").
  Ghidra (haiku): exit rule = the game's own: a `touch` binding whose command contains
  "tell player finalexit" or "tell player (finalexitloadmap" (0x0046ED70 is a has-binding
  test with substring match). SetEnabled stores its argument in +0x165 (gated on +0x164),
  so SetEnabled(1) -> 1 = open.
- Next (it3, step 3): the feature. Plan: gfx gains a world-polygon block (camera
  transform + near/view clip + project, one 50% white 64 px 4444 tile, vertex colour,
  queued depth-tested or x-ray), since only gfx may call RenderPoly (R5); then
  src/features/triggers.cpp: refresh the trigger list on level change (levelwatch) and on
  a slow timer (state in OnFrameEnd), draw in OnFrameEndLate. Classify: exit (touch line
  with the substrings above) red/green by +0x165; rescue = +0x164 and not exit (blue);
  else other (grey, hidden unless fd_triggers_all 1). Cvars fd_triggers (0/1),
  fd_triggers_all, fd_triggers_xray; a bind; practice-only. Debug pipe "triggers" for
  counts. Camera-inside boxes drawn at lower alpha.
- it3 (done): gfx `SubmitWorldBoxes` / `WorldBoxesReady` / `LastWorldBoxStats` (late step
  registered before textdraw; queued + flush, or x-ray immediate; per-alpha 64 px tiles);
  binds `BindDefault` (+ `unbind` lines for removed defaults); feature `triggers` (order 70,
  needs entities.head + trigger.vtbl -- field entries have no sig and never count as
  Usable): cvars fd_triggers / fd_triggers_all / fd_triggers_xray, commands
  fd_triggers_toggle (key T) / fd_triggers_list, practice-only, pipe `triggers`.
  DECISIONS D24-D29. tests/triggers.yaml PASS (run 2026-10-22-024004): Training 25/5/0,
  bank 14/1 rescue/1 exit shut, boxes 0/5/25/2/0 per mode, key T works.
  Bank geometry (fd_triggers_list): exit (-848 -64 1245)-(-512 104 1477), right behind the
  spawn (-697,-48,1196) -- 4-6 x 300 px mouse turns face it (sweep run
  2026-10-22-024210, probe test tests/probes/probe_trigger_sweep.yaml); rescue zone 'g'
  (-1918 -64 -1555)-(-1726 136 -1407) deep inside (visible x-ray in sweep s1).
- Next (it4, step 4): extend tests/triggers.yaml: face the exit (mouse_rel 1200) for a red
  screenshot, then deliver every victim so the game opens the exit -> assert the log line
  "triggers: exit '<null>' open", pipe open=1, green screenshot. Delivery: look at how
  speedrun_rescuebot / state tests finish a level (dev console `tell player escape` per
  carried victim needs carrying; maybe set victims.rescued / use Verb_FinalExit gate?).
  Must be the game's own SetEnabled(1) path (UpdateZonesAndWin), not a memory write to
  +0x165. Also a Release-config run.
- it4 (done): victims via `runrescuebot` (game script verb; the bot claims one victim per
  call, bypassing its enabled flag and cap). Probe tests/probes/probe_trigger_open.yaml:
  bank 6 victims, claimed 1..6, exit opened at 6 (log "triggers: exit '<null>' open").
  Note: rescued stays 0 -- bot claims are credited by omission; the exit opens through
  UpdateZonesAndWin's gate (delivered >= total - claimed) and SetEnabled(1), the game's path.
  Suite test now: spawn x-ray shot (blue rescue zone through the bank wall), walk back
  ('s' 0.5 s) into the exit box -> inside=1, polys>=1, red tint + the game's "You can't
  leave now!"; 6x runrescuebot -> open=1, green tint + "Exit through: Outside"; walk out
  + look back (green box, screenshot only). Mouse aiming dropped for asserts: not
  repeatable (a 900 px move pitched the view to the sky; 4x300 px landed ~90 deg short).
  **Bug found + fixed in gfx:** bank's world was allocated at Training's address, so
  pointer comparison never re-attached the box tile (handle 0, nothing drawn). Now a tile
  that settles without a driver handle is re-added (log "gfx: world box tile without a
  handle; re-adding"). Same latent bug in AlphaSurface (text panels would stay on the
  opaque fallback after such a level change): same fix applied there -- text_alpha /
  text_panels must pass in the step-5 suite run. Release run of triggers.yaml not
  possible (the pipe is Debug-only); the Release build compiles clean.
- Next (it5, step 5): full suite; QUICKSTART (fd_triggers*, key T, BindDefault for
  module authors, gfx world boxes), DECISIONS (D30 the same-address world re-attach),
  PROGRESS entry, VERSION 0.17.0, commit (probe + probe tests + evidence + feature;
  research.db correction note: kind 8 = every trigger), owner report, stop the loop.
