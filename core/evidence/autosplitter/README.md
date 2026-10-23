# Autosplitter: splitting staged levels (2026-10-23)

**Owner report:** with the IL / rescue% splitters, a run on a staged level (boat, subway, oil
rig) split late: the timer ran through the load and split when the next stage was loaded.
Single-stage levels were fine. Owner lead: oil_rig_A's `S_Script01` runs
`touch: tell player (finalexitloadmap oil_rig_b)`.

**Verdict: rewritten** (`tools/autosplitter/FDNY_ILs.asl`, `FDNY_rescue%.asl`, v2):
- **Stage done:** the next map's name appearing in the game session (GameSession+0x660),
  on the exit-touch frame.
- **Level won:** the mission result (PlayerRescueEnt+0x3438) turning to 1, on the win frame,
  for every level.

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
- **The old "end sound played" values:** sound handles in per-level slots. They also change
  during unrelated levels: the bank slot moved mid-mission on bar, gas and military base.
  The old script relied on its map-name guards.

## Why the old script split late

- `mapName` is read through `+0x688`.
- A level started from the menu has `+0x688` at the menu's TLevelInfo, so on the first stage
  `mapName` only changes when the load starts. Then `gameState` is 2, and the old split needs
  `gameState == 1`.
- So it split when stage B finished loading. It then also kept `hasSplit` set, which held
  the timer paused through **the whole next stage**, until the next exit touch (replay below).
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
| `split_win_{bar_r,gas_r,mbase,training}.log` | result 0 -> 1 on the win frame, the sound slot 17-33 ms later |
| `split_oilrig.log` | the first attempt: `runrescuebot` doesn't open the oil-rig exit (no claims), and the sound slots fill at level load |

## Replay: old vs new logic over the traces (`replay.txt`)

`tools/autosplitter/asl_replay.py` runs both scripts' logic tick by tick.
- `--menu-start` models the first stage started from the menu. The traces' levels came from
  the console; the model holds `mapName` until the load starts, as the code above does.
- The menu start of a staged level can't be reproduced on the VM: the harness career slot
  starts Training.

| | Old (menu start) | New |
|---|---|---|
| oil rig A -> B | split at 77.71 (stage B loaded); timer then paused until 119.92 (all of stage B untimed) | split at 77.16 (touch); paused 77.27-77.71 (the load) only |
| B -> C, C -> D, boat A -> B | the same late split + an untimed stage | on the touch, load-only pauses |
| boat B / subway B / oil rig D / single levels | sound-slot split (1-2 frames after the win) | the win frame |

## Side finding: the harness career save

Winning levels writes progress into the career slot the harness setups use ("Hotrod
Stein", at Training). After these probes `load_training` started other missions.
- Restored from the tracked `core/backup/Saves`.
- The harness now snapshots `Saves\` before every test and restores it after (it prints
  which files the game rewrote).
- No committed test result ran on the moved save.
