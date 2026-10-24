# FDNYCore release notes

Hand-written: what changed in each version. Downloads are the GitHub releases (the
Releases section of the repository page); each one's zip carries the key sheet generated
for that build. The release workflow copies a version's `## <version>` section below into
its release notes (`core/CLAUDE.md`, "Done means").

## 0.19.0

**Download note:** the GitHub release labelled v0.18.1 (2026-10-24) was built from `main` after
most of this work had landed, so it already contained everything up to the recorder. 0.19.0 is
the first build whose version says so, and it adds the 2026-10-24 fixes.

**New:**
- pause + free camera (`fd_pause`, key P);
- fd_juggle observing the safe spot;
- letter keys for every feature (no F keys);
- trajectory recorder (by Jacob Pilawa): player and victim paths as JSONL files in
  `Recordings` beside the `.asi`. Off by default: `fd_record 1` turns it on
  (`fd_record_victims`, `fd_record_directory`).

**Owner fixes (2026-10-22):**
- free-camera mouse at half the sensitivity, left/right no longer swapped;
- FDNYCore.cfg no longer filled with the default binds;
- G turns the warp HUD on visibly;
- one name, the **warp HUD** (`fd_hud_warp`; the old `fd_hud_glitch` names still work);
- T cycles the trigger boxes: off, on, on + x-ray, off;
- trigger boxes no longer lose rectangles or show through walls (drawn right after the world);
- player HUD: x / y / z columns, stance standing / crouched / bugged, no screen line;
- watermark pinned to the bottom-left corner;
- fire-juggling arrow: the frozen spot's coordinates above it.

**Key sheet (2026-10-23):**
- the key sheet is generated from the source on every build: every default key, feature key,
  feature, variable (default, values, saved, practice-only) and command. It's the repository's
  front page, and each release's zip has the sheet for that build;
- `bindlist` also lists the keys features read themselves (free camera W/A/S/D/E/Q,
  warp HUD B/Z/X, O to free the cursor);
- `exec` shows in `cmdlist`;
- help texts give every variable's values (`0 = ...` for each on/off setting, ranges, what the default means).

**Fixes (2026-10-24):**
- only one copy of the core runs: a second `FDNYCore*.asi` in the game folder stays inert,
  writes one line to `FDNYCore.log` naming the first, and shows on the watermark's `asi:` list;
- the watermark no longer lists the running core as another mod when its file is named
  `FDNYCore-<version>.asi`;
- the victim spawn hook has a single owner inside the core (no change in behaviour: static
  spawns pin as before, and the recorder sees every spawn).
