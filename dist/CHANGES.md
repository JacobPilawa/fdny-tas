# FDNYCore release notes

`README.md` next to this file is the key sheet, generated from the source with the
`.asi` (`core/build.ps1 -Dist`). This file is the hand-written part: what changed.
A new copy of the `.asi` lands in `dist/` on every version bump (`core/CLAUDE.md`, "Done means").

## Pre-release of 0.19.0 (the version string still reads 0.18.1)

Built and tested on the VM. The full 0.19.0 suite is paused at the owner's request; the
0.19.0 release replaces this build.

**Contents:**
- pause + free camera (`fd_pause`, key P);
- fd_juggle observing the safe spot;
- letter keys for every feature (no F keys).

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
- `dist/README.md` is now generated from the source on every build: every default key,
  feature key, feature, variable (default, values, saved, practice-only) and command;
- `bindlist` also lists the keys features read themselves (free camera W/A/S/D/E/Q,
  warp HUD B/Z/X, O to free the cursor);
- `exec` shows in `cmdlist`;
- help texts give every variable's values (`0 = ...` for each on/off setting, ranges, what the default means).
