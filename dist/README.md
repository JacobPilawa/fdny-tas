# FDNYCore release build

`FDNYCore.asi`: the Release build, for playing. Install: copy it next to
`FDNYFirefighter.exe`. It loads through Ultimate ASI Loader (`dinput8.dll`), like the
other `.asi` plugins. Commands and keys: `core/QUICKSTART.md`.

| | |
|---|---|
| Version | pre-release of 0.19.0: the version string still reads 0.18.1 |
| Source | the commit that last changed this file (`git log -1 -- dist/FDNYCore.asi`) |
| SHA-256 | `5ccf89a1bc706b0d9b73564b6c22a7daa375b52cc6050b6dbcd24fa6059ae3d6` |
| Size | 192,000 bytes |

**Contents:**
- pause + free camera (`fd_pause`, key P);
- fd_juggle observing the safe spot;
- letter keys for every feature (no F keys).

**Owner fixes (2026-10-22):**
- free-camera mouse at half the sensitivity, left/right no longer swapped;
- FDNYCore.cfg no longer filled with the default binds;
- G turns the collision HUD on visibly.

**Status:** built and tested on the VM. The full 0.19.0 suite is paused at the owner's
request; the 0.19.0 release replaces this file.

A new copy lands here on every version bump (`core/CLAUDE.md`, "Done means").
