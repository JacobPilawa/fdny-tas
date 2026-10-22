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
| 2 Core: textdraw centre anchor + no-background style | next |
| 3 Feature src/features/juggle.cpp | |
| 4 Suite test tests/fire_juggle.yaml | |
| 5 Suite, docs, version, commit, owner report | |

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
