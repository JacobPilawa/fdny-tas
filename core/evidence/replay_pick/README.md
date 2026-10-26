# Choosing replays without file names (D51)

**What ran:** `tests/replay_pick.yaml`. The recordings folder is set to `fdtest_recordings` and holds two files:
- the reference `mbase` run (pid 204, run 1, 33.8 s);
- a stub run 2 of the same game on `bank`, so run 1 counts as finished.

**Results (PASS):**
- **V** with nothing loaded logged `best mbase_..._pid204_run0001.jsonl, latest mbase_..._pid204_run0001.jsonl`. It loaded 1 recording and played it.
- **`fd_replay`** opened the picker with 1 row and paused the game (`pause.active = 1`). The row reads `1 [x] 10-23 21:11  33.8 s  finished  best  latest`.
  - Key 1 unloaded the run (loaded = 0), and key 1 again loaded it (loaded = 1).
  - V closed the picker, the game resumed (`pause.active = 0`), and the run played.
- **`fd_replay pid204_run0001`** loaded the file by part of its name.
- **`fd_replay_ghost 1`**, then reloading `mbase`: `ghost mbase_..._run0001.jsonl (best, 33847 ms) from 1047 ms`. It was loaded and playing.

**Unit tests (`replay_index`):**
- names: map, stamp, pid and run, including a map name with underscores; a non-recorder name is kept, with no map;
- the reference file's last 64 KB: duration 33847, complete;
- finished, best, latest and match on a six-file set:
  - a restart on the same map is not finished;
  - a file still being written is not latest;
  - the best is finished even when an unfinished run is shorter;
  - match ignores case.

**Regression found and fixed:**
- **Symptom:** `keys_default` failed 3 times in 3 runs with the replay at module order 85. It showed slow frames, a missed P key, then a window that hung at startup during the recorder's init.
- **Checks:** `main` passed. A build without the replay's player subscription passed.
- **Fix:** with the replay at order 110, after the recorder, it passed 3 times in 3. Measured in the same build: 57 fps in Training.

**Other tests:**
- `replay`, `recorder`, `draw_layers` and `docs_registry` pass.
- `replay` and `replay_pick` pass in one shared session, after both clear loaded recordings.

**Unit:** 81 C++ checks and 24 Python tests pass.
