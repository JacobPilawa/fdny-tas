# Warp HUD: the game research behind it

Reverse-engineering write-ups for the stale-impact warp, done for the original Python
collision overlay (2026-10-16 to 10-18) and ported into the core's warp HUD
(`src/features/glitch*.cpp`, D38, D52). The overlay itself is gone (git history keeps it);
these findings are what the HUD's hooks and analysis rest on, and `addresses.json` cites
them as evidence.

| File | What |
|---|---|
| `HOOK_SITES.md` | `geWorld_Collision`, the impact/plane globals and every write to them, the player position commit, the per-frame tick hook, caller classification (section 6 = the table in `addresses.json`), the player pointer |
| `WARP_CONDITIONS.md` | When a stale impact turns into a warp |
| `STALE_HIT_GEOMETRY.md` | The geometry of the stale hit: what the slot holds and why it goes stale |
| `WRITERS_AND_ORDER.md` | Who writes the collision slots, in which order within a frame |
| `HOSE_AND_FRAME_ORDER.md` | The hose's collision calls and the frame order around them |

They cite `research.db` hypotheses (H-numbers) and KB entries; that database is local-only on
the VM (`research/research.db`).
