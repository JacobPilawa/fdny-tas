# live split: FDNY Firefighter autosplitters and their test

| File | Run type | Reset |
|---|---|---|
| `FDNY_ILs.asl` | individual levels | automatic, on return to the menu |
| `FDNY_rescue%.asl` | rescue% (every non-hose level) | manual |
| `test/` | the known-good test: real game, LiveSplit's own ASL engine | |
| `asl_replay.py` | a quick offline model of both scripts over recorded traces | |

**Install:** in LiveSplit, Edit Layout -> + -> Control -> Scriptable Auto Splitter, and pick
the file. Both need "Game Time" comparison for the load removal.

## v2.1 (2026-10-23): what changed and why

The full investigation is in `core/evidence/autosplitter/README.md`.

| Event | v1 | v2.1 |
|---|---|---|
| Stage done (oil rig A/B/C, boat A, subway A) | `mapName` changed to the next stage while `gameState` = 1: on a stage started from the menu (the first stage, or a RESTART) that is after the next stage has loaded, about 0.5 s late in real time (the test below) | `pendingMap` (GameSession+0x660) becomes the next stage: written by the exit touch itself (`finalexitloadmap`), on that frame. Fallback: the in-game load starting (gameState 1 -> 2) with that name already there (a RESTARTed stage) |
| Level won | each map's "end sound" slot 0 -> nonzero | **unchanged** (v2 briefly used the mission result instead; v2.1 went back). Checked against the game's win flag (PlayerRescueEnt+0x3438): the sound follows the win by 0-2 game frames on every level, and stays 0 on its own map until then |
| Start | `gameState` = 1 | the first gameplay frame (`gameState` 3 -> 1) |
| Load removal | split flag, `gameState` 2 / 3 | `gameState` 2 / 3, and from a win until the next level is playing |

## The known-good test (`test/`)

It plays scripted runs in the real game, runs the .asl with LiveSplit's own engine, and
checks every timer event against what the game itself did. No agent is involved: one
command, PASS or FAIL per case.

```
cd "live split\test"
.\build_host.ps1                                 # once (and after a LiveSplit update)
python -I run_asl_tests.py                       # every case
python -I run_asl_tests.py case_il_oilrig        # one case
python -I run_asl_tests.py --asl <file.asl>      # every case with another splitter
```

(Python is `environment\local\tools\python\python.exe` on the VM.)

**The parts:**
- `AslHost.cs` (built by `build_host.ps1` into `environment\local\tools\livesplit\`):
  - loads `LiveSplit.Core.dll` and the Scriptable Auto Splitter component from the
    LiveSplit in the game folder, and runs the .asl the way that component does:
    parse, startup, then `Update` at the script's refresh rate (15 ms by default);
  - logs every start, split, reset and Game Time pause/resume with the Game Time.
- `events.py` logs the game's own values beside it (gameState, map, mission result),
  on the same clock and with the game's frame clock. It also opens each level's exit
  3 s into play: delivered victims = total, so the game opens the exit itself.
- `cases/*.yaml` are FDNYCore harness scripts (`core/harness`; the Debug FDNYCore build):
  - menus to a mission through a test career save (`saves/next_*.rb`: Hotrod Stein's
    slot with the next mission set);
  - put the player in each exit trigger, whose touch runs the map's own exit verb;
  - exit-less finals (subway B, oil rig D) are won with the game's debug key F8, which
    runs the game's own win routine.
- `check.py` compares the two logs.

**The cases:**

| Case | Splitter | What happens | Expected |
|---|---|---|---|
| `case_il_bank` | IL | bank from the menu, won at the exit, results | start, 1 split, reset |
| `case_il_oilrig` | IL | oil rig from the menu: A -> B; Esc, RESTART (back to A: the menu, then A again); A -> B, B -> C, C -> D; D won; results | 2 starts, 5 splits (the second A -> B on the fallback), 2 resets |
| `case_il_subway` | IL | subway from the menu: A -> B; B won; results | start, 2 splits, reset |
| `case_rescue_bank_boat` | rescue% | the same save as bank: bank won, results, go!, boat A -> B, boat B won, results | start, 3 splits, no reset, paused between missions |

**The checks** (`check.py`):

| Check | Passes when |
|---|---|
| start | the timer starts within 0.1 s of the first gameplay frame (gameState 3 -> 1); IL: again after each menu visit |
| stage split | at most 0.5 s before that stage's load starts (the exit touch), or at most 0.05 s after it (the fallback) |
| win split | 0 to 70 ms of game clock (0-3 frames) after the game's win flag |
| split count | one split per stage end and win, and the number the case expects |
| Game Time | at every split, equal to the time actually played since the start (from each 3 -> 1 to that stage's load start or win), within 0.06 s + 0.02 s per stage |
| reset | IL: within 0.5 s of each menu visit during a run (a reset changes no time); rescue%: never |
| script errors | none |

**Results (2026-10-23, VM):** v2.1 passes all four cases. The splits are on the exit touch
or within 2 frames of the win, and Game Time is within 0.03 s of the time played.

Run as a control, the original v1 IL splitter fails `case_il_oilrig`:
- **Late stage splits.** The menu-started A -> B and the restarted A -> B split 0.49 s and
  0.45 s after the load started, after the next stage had loaded.
- **A script error in the menu.** `mapName` is null before a level loads. LiveSplit logs it
  every tick; it is harmless.

In Game Time the late split costs only the touch-to-load-start time, about one frame,
because the load itself is paused. Evidence: `core/evidence/autosplitter/livesplit_test/`.

**Limits:**
- **Needs the VM setup:** the game folder, the Debug FDNYCore build (the harness), and Python
  with pymem. On another PC it needs the same pieces.
- **Not checked against what a viewer sees.** The checks compare against the frame the game
  sets its own values: the touch, the win flag, the load start.
- **The scenario is fixed; the frame timing is not.** The limits leave room for that: the
  VM's frames were 16-33 ms.
- **Hose levels are not covered**, and neither are the other rescue maps' wins in this test.
  The end-sound timing for all eight wins is in `core/evidence/autosplitter/README.md`.

The probes in `test/probes/` found the menu flows and are not part of the test.

## Memory used

All paths are module-relative LiveSplit paths (exe base 0x400000):

| Value | Path | Meaning |
|---|---|---|
| `gameState` | `0x102AAC, 0x6F0` | 0 menu, 2 loading, 3 level start, 1 playing |
| `mapName` | `0x102AAC, 0x688, 0x0, 0xC` | the current level's map name |
| `pendingMap` | `0x102AAC, 0x660, 0xC` | the map the last in-game level change went to |
| `*LevelEndSoundPlayed` | `0x103A78, 0x8, 0x0, <slot>, 0x0` | a level's end sound: 0 until it plays (slots: bank 0x158, bar 0xFC, gas 0xF0, mbase 0xC8, training 0x104, boat 0xBC, oil rig D 0xC4, subway 0x144). Sound handles: they change during other levels, so each is read only on its own map |

## Offline replay

`asl_replay.py` replays a model of the old and new logic over a recorded trace
(`core/probes/splitprobe.py`), ticking at LiveSplit's 15 ms:

```
python -I "live split/asl_replay.py" core/evidence/autosplitter/split_chain.log --menu-start --from 44.19
```

It is a model of the .asl files, not the files themselves. Change both together. The
test above is the real check.
