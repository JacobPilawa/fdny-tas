# FDNY Firefighter autosplitters (LiveSplit ASL)

| File | Run type | Reset |
|---|---|---|
| `FDNY_ILs.asl` | individual levels | automatic, on return to the menu |
| `FDNY_rescue%.asl` | rescue% (every non-hose level) | manual |

**Install:** in LiveSplit, Edit Layout -> + -> Control -> Scriptable Auto Splitter, and pick
the file. Both need "Game Time" comparison for the load removal.

## v2.1 (2026-10-23): what changed and why

The full investigation is in `core/evidence/autosplitter/README.md`.

| Event | v1 | v2.1 |
|---|---|---|
| Stage done (oil rig A/B/C, boat A, subway A) | `mapName` changed to the next stage while `gameState` = 1: on a level started from the menu that is after the next stage has loaded | `pendingMap` (GameSession+0x660) becomes the next stage: written by the exit touch itself (`finalexitloadmap`), on that frame. Fallback: the in-game load starting (gameState 1 -> 2) with that name already there (a retried stage) |
| Level won | each map's "end sound" slot 0 -> nonzero | **unchanged** (v2 briefly used the mission result instead; v2.1 went back). Checked against the game's win flag (PlayerRescueEnt+0x3438): the sound follows the win by 0-2 game frames on every level, and stays 0 on its own map until then |
| Start | `gameState` = 1 | the first gameplay frame (`gameState` 3 -> 1) |
| Load removal | split flag, `gameState` 2 / 3 | `gameState` 2 / 3, and from a win until the next level is playing |

## Memory used

All paths are module-relative LiveSplit paths (exe base 0x400000):

| Value | Path | Meaning |
|---|---|---|
| `gameState` | `0x102AAC, 0x6F0` | 0 menu, 2 loading, 3 level start, 1 playing |
| `mapName` | `0x102AAC, 0x688, 0x0, 0xC` | the current level's map name |
| `pendingMap` | `0x102AAC, 0x660, 0xC` | the map the last in-game level change went to |
| `*LevelEndSoundPlayed` | `0x103A78, 0x8, 0x0, <slot>, 0x0` | a level's end sound: 0 until it plays (slots: bank 0x158, bar 0xFC, gas 0xF0, mbase 0xC8, training 0x104, boat 0xBC, oil rig D 0xC4, subway 0x144). Sound handles: they change during other levels, so each is read only on its own map |

## Checking a change

`asl_replay.py` replays the old and new logic over a probe trace
(`core/probes/splitprobe.py`):

```
python -I tools/autosplitter/asl_replay.py core/evidence/autosplitter/split_chain.log --menu-start --from 44.19
```

It runs the logic once per change and once per LiveSplit tick (60 Hz) between changes.

The logic in `asl_replay.py` mirrors the .asl files: change both together.
