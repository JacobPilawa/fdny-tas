# FDNY Firefighter autosplitters (LiveSplit ASL)

| File | Run type | Reset |
|---|---|---|
| `FDNY_ILs.asl` | individual levels | automatic, on return to the menu |
| `FDNY_rescue%.asl` | rescue% (every non-hose level) | manual |

**Install:** in LiveSplit, Edit Layout -> + -> Control -> Scriptable Auto Splitter, and pick
the file. Both need "Game Time" comparison for the load removal.

## v2 (2026-10-23): what changed and why

The full investigation is in `core/evidence/autosplitter/README.md`.

| Event | v1 | v2 |
|---|---|---|
| Stage done (oil rig A/B/C, boat A, subway A) | `mapName` changed to the next stage while `gameState` = 1: late on a level started from the menu (after the next stage loaded), and the next stage was then untimed | `pendingMap` (GameSession+0x660) becomes the next stage: written by the exit touch itself (`finalexitloadmap`), on that frame. Fallback: the in-game load starting (gameState 1 -> 2) with that name already there (a retried stage) |
| Level won | eight per-map "end sound" slots | the mission result (PlayerRescueEnt+0x3438) 0 -> 1, on the win frame, the same for every level |
| Start | `gameState` = 1 | the first gameplay frame (`gameState` 3 -> 1) |
| Load removal | split flag, `gameState` 2 / 3 | `gameState` 2 / 3, and from a win until the next level is playing |

## Memory used

All paths are module-relative LiveSplit paths (exe base 0x400000):

| Value | Path | Meaning |
|---|---|---|
| `gameState` | `0x102AAC, 0x6F0` | 0 menu, 2 loading, 3 level start, 1 playing |
| `mapName` | `0x102AAC, 0x688, 0x0, 0xC` | the current level's map name |
| `pendingMap` | `0x102AAC, 0x660, 0xC` | the map the last in-game level change went to |
| `missionResult` | `0x103A78, 0x30, 0x3438` | 0 playing, 1 won, 2 lost |

## Checking a change

`asl_replay.py` replays the old and new logic over a probe trace
(`core/probes/splitprobe.py`):

```
python -I tools/autosplitter/asl_replay.py core/evidence/autosplitter/split_chain.log --menu-start --from 44
```

The logic in `asl_replay.py` mirrors the .asl files: change both together.
