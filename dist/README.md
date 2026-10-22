# FDNYCore release build

`FDNYCore.asi`: the Release build, for playing. Install: copy it next to
`FDNYFirefighter.exe`. It loads through Ultimate ASI Loader (`dinput8.dll`), like the
other `.asi` plugins. Commands and keys: `core/QUICKSTART.md`.

| | |
|---|---|
| Version | pre-release of 0.19.0: the version string still reads 0.18.1 |
| Source commit | `1f5cd3f` (2026-10-22) |
| SHA-256 | `5f5bcee7fa0d7e94c3a6091eecb140ef98e541693839f3744f02e5d1762218cb` |
| Size | 192,000 bytes |

**Contents:**
- pause + free camera (`fd_pause`, key P);
- fd_juggle observing the safe spot;
- letter keys for every feature (no F keys).

**Status:** built and tested on the VM, but the full 0.19.0 test suite was still
running when this copy was made. The 0.19.0 release replaces this file.

A new copy lands here on every version bump (`core/CLAUDE.md`, "Done means").
