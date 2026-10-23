# FDNYCore release build

`FDNYCore.asi`: the Release build, for playing. Install: copy it next to
`FDNYFirefighter.exe`. It loads through Ultimate ASI Loader (`dinput8.dll`), like the
other `.asi` plugins. Commands and keys: `core/QUICKSTART.md`.

| | |
|---|---|
| Version | pre-release of 0.19.0: the version string still reads 0.18.1 |
| Source | the commit that last changed this file (`git log -1 -- dist/FDNYCore.asi`) |
| SHA-256 | `8b70c354b980a1e04b73cb46d21f161ab3d777722004adf519cd22748527129c` |
| Size | 192,000 bytes |

**Contents:**
- pause + free camera (`fd_pause`, key P);
- fd_juggle observing the safe spot;
- letter keys for every feature (no F keys).

**Owner fixes (2026-10-22):**
- free-camera mouse at half the sensitivity, left/right no longer swapped;
- FDNYCore.cfg no longer filled with the default binds;
- G turns the warp HUD on visibly;
- one name, the **warp HUD** (`fd_hud_warp`; the old `fd_hud_glitch` names still work);
- T cycles the trigger boxes: off, on, on + x-ray, off.

**Status:** built and tested on the VM. The full 0.19.0 suite is paused at the owner's
request; the 0.19.0 release replaces this file.

A new copy lands here on every version bump (`core/CLAUDE.md`, "Done means").
