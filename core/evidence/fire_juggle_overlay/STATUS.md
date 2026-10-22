# Fire-juggling overlay (fd_juggle): loop status

Owner request (2026-10-22): a practice-only overlay, `fd_juggle 1`. It shows a small
ASCII arrow (8 directions, relative to the view) and the horizontal distance to
where a carried survivor would land if the player passed out now. That landing point
is T = (S.x - 30, S.y, S.z), where S is the safe spot at PlayerRescueEnt+0x3400.

- The arrow shows only while the game's own write gate is closed:
  locked = flinch || burn >= 0.1 || O2 <= 0.5 || state == 6.
  Airborne alone doesn't count (owner decision).
- Not locked: "-".
- Within 16 u: "o".
- " ^" / " v" when |dy| > 48.
- No background; centred horizontally, top 30 px below the screen centre; kept
  while passed out.

Background: KB `fire-juggling`, `core/evidence/fire_juggling/`.

| Step | State |
|---|---|
| 0 Preconditions (core_loads) | **done**: PASS (session on console) |
| 1 addresses.json entries | **done**: player.f_safe / f_burn / f_flinch / f_o2; build clean (126 entries), addr_verified PASS |
| 2 Core: textdraw centre anchor + no-background style | **done**: `CENTER` anchor + `STYLE_TEXT_ONLY` (D31); text_panels / text_alpha / hud_player PASS |
| 3 Feature src/features/juggle.cpp | **done**: feature + Debug hooks; tests/fire_juggle.yaml (first version) PASS; glyph sheet `glyphs.png` for owner review |
| 4 Suite test tests/fire_juggle.yaml | **done**: real fire + pass-out, harness-side direction check; PASS (run 2026-10-22-173943, `it4/`) |
| 5 Suite, docs, version, commit, owner report | next |

## Log

- it1: git pull (up to date).
  - Step 0: core_loads PASS (run 2026-10-22-170613); the session is Active on the
    console.
  - Step 1: four field entries after player.f_yaw. None existed before; checked by
    grepping the rva and the names o2/burn. Existing entries reused: player.f_pos
    0x4C, f_ground 0x12C, f_state 0x18C, f_yaw 0x3434. The ghidra/note fields cite
    the decompile and the live evidence.
  - `build.ps1`: both configs clean. addr_verified PASS (run 2026-10-22-170705).
    Field entries have no sig and are never `Usable`, so the feature's kNeeds must
    list player.object (signed) only.
- Next (it2, step 2): textdraw has anchors TOP_LEFT / TOP_RIGHT / BOTTOM_LEFT only, and
  every panel draws a background (slot 0 at fd_text_alpha). Add a centre anchor
  (horizontal centre, dy from the vertical centre) and a per-panel "no background"
  option (background alpha 0, text only). Existing panels stay unchanged:
  text_panels and text_alpha must pass. DECISIONS entry.
- it2 (step 2, done):
  - textdraw now has `Anchor CENTER`: x = (view w - panel w) / 2 + dx, y = view h / 2 + dy.
  - `Style STYLE_TEXT_ONLY`: the AlphaSurface with background slot 0 at alpha 0. The
    font is non-antialiased, so only glyph pixels remain. There is no opaque fallback:
    without a world it is not drawn.
  - D31 in DECISIONS.md. Build clean (both configs).
  - Regression runs PASS: text_panels (2026-10-22-171056), text_alpha (-171235),
    hud_player (-171326).
  - Panels in use: 3 of 8 (watermark, glitch, hud_player), so there is room for juggle.
- Next (it3, step 3): src/features/juggle.cpp per the goal.
  - cvar fd_juggle; practice-only; kNeeds {player_object}. Field entries never count
    as Usable.
  - Panel: CENTER, 0, 30, STYLE_TEXT_ONLY.
  - Debug pipe `juggle`.
  - Glyph screenshot: if needed, a probe test in tests/probes/ that loads bank, burns
    via the step-4 hook, and turns the view; or a Debug-only command that forces each
    direction for the screenshot.
- it3 (step 3, done):
  - **Yaw convention, measured live** (`yaw_probe.log`, probes/yaw_probe.py):
    - W moves (sin yaw, cos yaw) in world (x, z): yaw 0 -> +Z, 90 -> +X, 180 -> -Z,
      270 -> -X.
    - D (strafe right) moves (-cos yaw, sin yaw).
  - **src/features/juggle.cpp**, order 75, kNeeds {player_object}:
    - cvar fd_juggle (default 0), practice-only;
    - textdraw panel "juggle": CENTER 0,30, STYLE_TEXT_ONLY, white bold;
    - OnFrameEnd: locked = flinch || burn >= 0.1 || O2 <= 0.5 || state 6; T = S - 30 X;
      horizontal distance; dy;
    - direction: 8 sectors without trig (tan 22.5 test) on (forward, right) from x87
      fsincos of the yaw;
    - "o" under 16 u, " ^" / " v" beyond +-48 u;
    - log lines "juggle: locked / following (...)".
  - **Self-check counter**: the spot moved on a frame after a *strictly* locked one
    (burn >= 0.11, since Think cools by 0.1*dt <= 0.01 before comparing). The end of a
    pass-out is exempt (ResetLocomotionState writes the checkpoint), and so is a new
    player object.
  - **Debug-only hooks (R6):**
    - `fd_dbg_setpos x y z [yaw [o2]]`, applied at frame end like fd_tp_load (D18);
    - `fd_dbg_o2 v`;
    - pipe `juggle`: enabled / valid / locked / dir / glyph / dist (tenths) / dy / safe /
      target / pos / burn (x1000) / o2 (x1000) / flinch / state / selfcheck. The key is
      "enabled", not "on": YAML reads `on` as true.
  - **Lessons:**
    - A console step costs ~3 s, because it is typed.
    - The spike's fire coordinates (144, 250, -1324) are not reliable: fire nodes move
      along their paths, so some stints did little or no damage (run 172616).
    - O2 refills ~0.1/s on clean ground, so O2 0.45 unlocked within 0.5 s. Each step now
      sets O2 0.1.
  - **tests/fire_juggle.yaml** (first version) PASS, run 2026-10-22-173220:
    - off by default; "-" at spawn;
    - O2 lock on clean ground (the spot freezes where the player stands, so T is exactly
      30 u to -X): 8 yaws x 45 deg each give the computed sector (0 right, 45 down-right,
      90 down, 135 down-left, 180 left, 225 up-left, 270 up, 315 up-right) and dist 300
      (tenths);
    - selfcheck 0; unlocked after O2 1;
    - refused under fd_profile run.
  - `glyphs.png`: crops of the 9 states, enlarged, **for owner review**. The text uses
    the shared 9 px font (fd_text_size); no outline.
- Next (it4, step 4): real fire.
  - Add a Debug hook that finds a burning fire node live: e.g. `fd_dbg_tofire`, which
    walks kind-6 entities and teleports into the nearest FireRescueEnt centre (+0x7C;
    needs an addresses.json field entry fire.center) whose touch damages, or tries nodes
    until the burn rises.
  - Then assert: locked after burning with S frozen; stepping out keeps the lock until
    burn < 0.1, then "-"; dir/dist vs the harness's own computation (pipe pos / target /
    yaw); the arrow through the pass-out (state 6); "-" after respawn; selfcheck 0.
- it4 (step 4, done):
  - addresses.json: `fire.center` (+0x7C), field entry, 127 entries.
  - juggle.cpp, Debug only:
    - `fd_dbg_tofire [burn]`: walks the kind-6 entities, teleports above each fire
      centre (+30 y) for up to 30 frames until the burn meter rises, burns to the
      target, then returns to the start. A target >= 1 stays until passed out. It
      moves to the next node after 90 frames without a rise.
    - Pipe keys `frozen` (locked and the spot unchanged since the lock began),
      `outframes` / `outmiss` (passed-out frames, and those without an arrow or "o").
  - Harness:
    - `wait_log {contains, timeout}`: waits for one more occurrence of a log line.
    - `assert_juggle {locked}`: reads pos, the safe spot and yaw from memory itself,
      computes T, the distance and the sector with the measured view axes, and checks
      the pipe's dir (1-degree edge tolerance), dist (+-1 u) and target.
  - tests/fire_juggle.yaml PASS, run 2026-10-22-173943 (evidence `it4/`):
    - spawn "-" (assert_juggle locked 0);
    - `fd_dbg_tofire 0.6` done after 2.0 s, found the node at (144, 220, -1324);
    - back at the spawn: locked, frozen, burn 0.598, arrow "down" 2647.3 u, matching
      the harness (angle 197.8);
    - after 2.5 s still locked and frozen;
    - after 6 more s: burn 0, "-", log "juggle: following";
    - `fd_dbg_tofire 2` passes out after 2.7 s: state 6, locked, arrow "right" 30.0 u
      (the player stands on the spot), matching the harness;
    - after 7 s: state 3, "-", outframes 207, outmiss 0, selfcheck 0;
    - the O2 glyph checks (8 sectors) and the run-profile refusal still PASS.
  - Screenshots: `it4/fire_out_locked.png` ("down" + "2647u ^"), `it4/passed_out.png`
    ("right" + "30u", in the burn tint), cooled_following, respawned, spawn_following.
- Next (it5, step 5):
  - full suite;
  - QUICKSTART: feature row, known gaps (9 px text without outline; the arrow aims
    at the spot -30 X before the floor search; jumping doesn't show the lock), and
    module-author notes for textdraw CENTER / STYLE_TEXT_ONLY;
  - DECISIONS: the lock gate = the game's own gate, airborne excluded; the self-check
    margin; the test hooks;
  - PROGRESS it entry; VERSION 0.18.0; commit, push, owner report, stop the loop.
