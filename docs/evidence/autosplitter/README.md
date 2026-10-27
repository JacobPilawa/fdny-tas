# Autosplitter: splitting staged levels (2026-10-23)

**Owner report:** with the IL / rescue% splitters, a run on a staged level (boat, subway, oil
rig) split late: the timer ran through the load and split when the next stage was loaded.
Single-stage levels were fine. Owner lead: oil_rig_A's `S_Script01` runs
`touch: tell player (finalexitloadmap oil_rig_b)`.

**Verdict: stage splits rewritten** (`livesplit/FDNY_ILs.asl`, `FDNY_rescue%.asl`, v2.1;
checked by the known-good test in `livesplit/test/`, results below):
- **Stage done:** the next map's name appearing in the game session (GameSession+0x660),
  on the exit-touch frame.
- **Level won:** the owner's v1 end-sound slots, kept. v2 had switched to the mission result
  (PlayerRescueEnt+0x3438); the owner asked to keep the sound method if it is accurate, and
  it is: 0-2 game frames after the win flag on every level (table below).

## How the game does it (read-only Ghidra, then live)

- **The exit trigger's touch:** it runs `finalexitloadmap <next>`, handled by
  `PlayerRescueEnt::Verb_FinalExitLoadMap` (0x450A70).
  - Victim gate: all accounted for.
  - Then `GameSession::RequestLevelChange` (0x0042A260): `+0x68C = 1` (pending flag), the
    next map name into the String at `+0x660`, rrf path at `+0x668`.
  - No win sound, fade or results screen: that is why staged levels have no "end sound" to
    watch.
- **Next RunFrame:** clears +0x68C first, then `GameSession::LoadLevel(this+0x660)`.
  - LoadLevel sets `gameState` (+0x6F0) = 2 and points `+0x688` (current level) at that
    pending block.
  - Then 2 -> 3 -> 1 when play resumes.
  - +0x68C is set for less than one frame: LiveSplit can miss it. +0x660 stays until the
    next request.
- **Winning a level:** `finalexit` (exit levels) or `UpdateZonesAndWin` (exit-less levels).
  The result +0x3438 goes 0 -> 1 on the win frame; the level's end sound follows one or two
  frames later.
- **The "end sound played" values:** sound handles in per-level slots. They also change
  during unrelated levels (the bank slot moved mid-mission on bar, gas and military base;
  the oil rig D and subway slots fill at oil rig A's level start), so the scripts' map-name
  guards are required. On its own map each slot stayed 0 from gameplay start to the win.

| Level | Win flag -> end sound (game clock) |
|---|---|
| bank, boat B | same frame |
| oil rig D, military base, training | 17 ms (1 frame) |
| subway B | same game frame (probe saw it 18 ms later) |
| gas station, bar | 28-32 ms (1-2 frames) |

## Why the old script split late

- `mapName` is read through `+0x688`.
- A level started from the menu has `+0x688` at the menu's TLevelInfo, so on the first stage
  `mapName` only changes when the load starts. Then `gameState` is 2, and the old split needs
  `gameState == 1`.
- So it split when stage B finished loading: in real time 0.45-0.49 s after the load
  started (`livesplit_test/v1_il_oilrig`). In Game Time (paused during the load) that is
  the moment the load started, which on a menu-started stage was one frame (16 ms) after
  the touch.
- **Correction (v2.1):** v2 also said the old script then held the timer paused through the
  whole next stage. That was wrong: the replay only evaluated the script when a value
  changed, so it missed the old script's flag reset on the next tick. With the replay ticking
  at LiveSplit's 15 ms, the old timer resumes one tick after the split; the live test agrees.
- From the console (`loadmap`) `+0x688` already points at the pending block, so `mapName`
  changes on the touch frame. That is why it isn't visible in every test.

## Live runs (VM, probe `core/probes/splitprobe.py`)

The probe reads exactly what LiveSplit reads (the ASL's DeepPointer paths) as fast as it
can, and logs every change.

**Genuine exits:**
- The probe sets delivered = total, a probe-only test input, so the game's own
  `UpdateZonesAndWin` opens the exit.
- The test steps into the exit trigger. Its touch runs the map's own verb.
- Wins on exit-less levels use the game's dev key F8 (`DBG_WIN_LEVEL`: the same
  `UpdateZonesAndWin`).

| Run | What happened at the stage exit / win |
|---|---|
| `split_chain.log` | oil rig A -> B -> C -> D and boat A -> B: `pendingMap` = next stage on the touch sample; `gameState` 1 -> 2 about 0.11 s later, 2 -> 3, 3 -> 1 (load about 0.45 s on the VM). Boat B final exit: result 0 -> 1 on the touch |
| `split_subway.log` | subway A -> B the same way (FinalExit box); subway B won (F8): result 0 -> 1 |
| `split_oilrigd.log` | oil rig D won (F8): result 0 -> 1 |
| `split_single.log` | bank: result 0 -> 1 on the exit touch, then the sound slot |
| `split_win_{bar_r,gas_r,mbase,training}.log` | result 0 -> 1 on the win frame, the sound slot 16-32 ms later |
| `split_oilrig.log` | the first attempt: `runrescuebot` doesn't open the oil-rig exit (no claims), and the sound slots fill at level load |

## Replay: old vs new logic over the traces (`replay.txt`)

`livesplit/asl_replay.py` runs a model of both scripts' logic tick by tick.
- `--menu-start` models the first stage started from the menu. The traces' levels came from
  the console; the model holds `mapName` until the load starts, as the code above does.
- At the time the menu start of a staged level couldn't be reproduced on the VM (the
  harness career slot starts Training). The test below does it for real, with test saves.

| | Old (menu start) | New (v2.1) |
|---|---|---|
| oil rig A -> B | split at 77.71 (stage B loaded; Game Time of the load start, 77.27) | split at 77.16 (touch); paused 77.27-77.71 (the load) |
| B -> C, C -> D | the same late split | on the touch |
| boat B / subway B / oil rig D / single levels | end-sound split | the same end-sound split, same tick |

The replay ticks at 15 ms between changes, like LiveSplit's ASL default.

## Known-good test: the real .asl files in the real game (`livesplit_test/`)

`livesplit/test/` (how it works: `livesplit/README.md`). LiveSplit's own ASL engine runs
the .asl beside scripted runs in the game, and `check.py` compares each timer event with
the game's own values. The menu starts use test career saves whose next mission is the
level (`livesplit/test/saves/`).

| Run | Splitter | Result |
|---|---|---|
| `v21_il_bank` | v2.1 IL | PASS: start +0.04 s; win split 0 ms after the win flag; Game Time -0.01 s vs played; reset on the results menu |
| `v21_il_oilrig` | v2.1 IL | PASS: A -> B 16 ms before the load (touch); Esc RESTART -> reset, new start; A -> B again on the fallback (0.012 s after the load started: pendingMap already read oil_rig_B); B -> C, C -> D on the touch; D won 27 ms (game clock) after the flag; Game Time within 0.03 s at every split |
| `v21_il_subway` | v2.1 IL | PASS: A -> B on the touch, B won 30 ms after the flag |
| `v21_rescue_bank_boat` | v2.1 rescue% | PASS: bank won, boat A -> B, boat B won; no reset through two results screens and a briefing; Game Time = time played across both missions (66.25 s vs 66.26 s) |
| `v1_il_oilrig` | v1 IL (control) | FAIL: the menu-started and the restarted A -> B split 0.49 s / 0.45 s after the load started; a script error every menu tick (`mapName` null before a level loads) |
| `v1_il_bank` | v1 IL (control) | FAIL only on the menu script error; the win split and reset are right |

Touch to load start: one frame (16 ms) on a stage the menu started; 0.11-0.16 s of real
time with the game clock not advancing on stages an exit loaded (B -> C, C -> D).

## Side finding: the harness career save

Winning levels writes progress into the career slot the harness setups use ("Hotrod
Stein", at Training). After these probes `load_training` started other missions.
- Restored from the tracked `backup/Saves`.
- The harness now snapshots `Saves\` before every test and restores it after (it prints
  which files the game rewrote).
- No committed test result ran on the moved save.

## v2.2 (2026-10-24): the level-change request byte

Owner, 2026-10-24: a detection that depends on which levels were played before isn't good
enough. Cases: from the menu, `pendingMap` keeps the last in-game target (oil rig C to
subway A: still `oil_rig_c`), so a stage whose next name is already there (a RESTART, or
subway A after an earlier A -> B) changes nothing on its exit touch.

**The byte.** `GameSession+0x68C` (`re_flag68c.py` lists every access in the exe):
- set to 1 only by `GameSession::RequestLevelChange` (0x0042A269);
- cleared by `GameSession::RunFrame` (0x0042AEB4) right before it calls `LoadLevel`;
- zeroed by `GameSession::Init`.

So it goes 0 -> 1 on every exit touch, whatever was played before, and `pendingMap` read
with it is always the fresh target. It stays set 0.012-0.152 s in the runs, and once 2.499 s.
That time is the rest of the touch frame: the game clock does not move (oil rig C, clock
104217 throughout the 2.5 s).

**mapName.** After an in-game load, `mapName`'s pointer (+0x688) points at the session's own
+0x660 block, so on a stage loaded in-game both names change together on the touch (owner's
observation, confirmed in `livesplit_test/v22pre_il_oilrig_stall/events.log`). On a stage
started from the menu, `mapName` changes at the load start.

**v2.2:**
- splits on the byte's 0 -> 1 with `pendingMap` = next stage;
- pauses while the byte is set;
- polls at 200 Hz.

Test results: `livesplit/README.md` "Results"; logs in `livesplit_test/v22_*`, the v2.1
control `v21_il_subway_restart`, and the stall run `v22pre_il_oilrig_stall`.

_The logs, screenshots and run output named above were removed on 2026-10-27 (repository tidy-up); git history keeps them._
