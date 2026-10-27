# Recorder

The recorder feature writes `fdny-trajectory-v1` JSONL files to `Recordings/`
beside FDNYCore.asi. `fd_record`, `fd_record_victims` and `fd_record_directory`
control capture, victim tracking and the output directory. Settings are archived;
changing a setting closes the current file and applies on the next Player update.
Parent directories must exist; the recorder creates the final directory.

## Recording contract

- Each Player initialization starts an attempt, including same-map restarts.
  Late initialization starts on the first observed Player update. Files close
  on the next initialization, configuration change or normal process exit.
- The record types are `start`, `sample`, `victim_sample`, `victim_end` and `end`.
  The header includes the stage, UTC timestamp, schema, recorder/core versions,
  coordinate conventions, sampling point and time/input basis.
- Player samples follow the original root update. Positions use Genesis3D units,
  Y up. `t_ms` is elapsed wall-clock time; pauses leave gaps. Samples include
  position, velocity, facing, pitch, raw state, ground contact, tool slot,
  collision bounds, frame delta and sampled virtual keys.
- Key-mask bits, from zero: W A S D Up Left Down Right C Ctrl Space Shift left
  mouse right mouse 1 2 3. Keys are sampled only while the game has focus.
- Victims have run-local lifetime IDs, names, position, facing, raw state and
  bounds. Capture is limited to 20 Hz plus spawn/state changes and 128 lifetimes.
  Destruction ends a track. Player updates also sample live registered victims.
- Returning to the menu does not close an attempt. Files are buffered in 64 KiB,
  flushed about once per second while callbacks arrive and at normal closure.
  Interrupted files are readable through the last saved complete line.
- Output errors disable the current attempt; the next initialization retries.
  Unavailable victim observers leave Player capture enabled. A standalone
  `FDNYRecorder.asi` disables the feature to avoid duplicate capture.

## Checks

- Debug and Release compilation, architecture checks and documentation generation.
- Training recording and trajectory playback confirmed. Generated files are
  accepted by the existing trajectory-viewer parser.
- The harness JSONL validator accepts completed recordings and rejects missing
  closing records, invalid sample sequences, invalid bounds, nonfinite positions
  and invalid victim lifetimes.

`tests/game/recorder.yaml` uses the existing game harness to check automatic capture,
same-stage restarts, recording opt-out, output-error recovery and the generated
JSONL. The runner checks the files named in the current test log, validates
sample sequences, timestamps, vectors, bounds, victim lifetimes and closing
counts, and copies the recordings into the test results directory.

Run it using the standard test command:

```powershell
python -I tools/harness/run_test.py tests/game/recorder.yaml
```
