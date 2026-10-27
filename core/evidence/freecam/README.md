# Pause + free camera: spike (2026-10-22)

**Verdict: feasible, no blocker.** One design change: the camera override has to be
applied at `geEngine_RenderWorld`, on the camera passed to it, not when the frame starts.

How it was run:
- Four read-only haiku Ghidra lookups.
- One probe, `probes/pauseprobe.cpp` -> `zz_pauseprobe.asi` (image base 0x3E600000, built by
  `build_probes.ps1`), driven by `tests/probes/probe_pause.yaml` on bank with the RescueBot
  off.
- **Round A:** skip the simulation with the clock running.
- **Round B:** skip it with the clock paused (`GameClock::Pause`).
- Logs: `p1`-`p5_pauseprobe.log`.
  - `p1` is void: the probe watched letter keys, and the harness typed them in console
    commands (`loadmap` paused it mid-load). The probe now uses F5/F6/F7.

## Static (Ghidra)

- **Frame:** `GameSession::RunFrame` calls GameClock::Tick, then the input poll, then
  `CALL TickSubsystems` at 0x0042af0a (returns to 0x0042af0f), then `RenderFrame` at
  0x0042af19.
- **TickSubsystems:** three of its four slots are one generic entity-list sweep
  (0x0049c0e0); the fourth (0x0049bd20) is unresolved. It also drains the spawn and
  callback queues and runs `ROCSoundSystem::Frame`.
- **Render-side time:** `Sprite::Render` -> `ParticleNode::AdvanceSpawn` advances emitter
  keyframes from `g_gameTimeMs` inside the render. Pausing the clock stops that too.
- **Clock:** `GameClock::Pause` / `Resume` (0x0042BF20 / 0x0042BF40):
  - ABI: `this` = the active subtimer in ECX (0x502AD8 if the byte 0x502AF8 is set, else
    0x502AB8), no stack arguments, plain RET.
  - While paused, `ComputeFrameDelta` skips its body: elapsed stops, dt keeps its last
    value, and the 60 FPS Sleep cap is skipped.
- **Input:** DirectInput is NONEXCLUSIVE | BACKGROUND (0x0A), buffered.
  - Mouse: per-frame sums at GameControls+0x18 / +0x1c.
  - Keys: a 3-byte record per DIK at +0x26.
  - Block byte +0x330: only the action queries (IsHeld / WasPressed) return 0; polling
    goes on.
  - `PollFrame` calls `SetCursorPos(200,200)` every frame.
- **Default keys:** W A S D C, 1-3, Ctrl, Shift, Space, arrows, LMB (ACTION), RMB (JUMP);
  F2-F4 and F6-F8/F11 in dev mode only.
- **Camera setters:** the exe imports `geCamera_SetWorldSpaceXForm` (not the Vis variant).
  Its only user is the thunk 0x00491c60 (vtable 0x004e3b50, `ROC\Renderer\ActorInfo.cpp`).

## Live (probe, run p5 unless noted)

| Question | Result |
|---|---|
| Rendering while TickSubsystems is skipped | Continues: 580 frames in round A (~57 fps), 474 in round B; no crash; the FDNYCore watermark/HUD still draw |
| Player frozen | pos (-697, 1196) on every sample; W held while paused didn't move it (input block) |
| Victims frozen | identical to 0.01 u over 5 s in both rounds, including the roaming one (11.02, -611.38) |
| Mission timer | 44960.5 for the whole pause, both rounds; after resume -2.07 in 2 s |
| Game clock | round A: runs (+5.2 s); round B: **frozen** at 81739 ms. First dt after resume: 0.017 (normal) |
| Mouse while paused | the game's sums see it: +30 X per frame for a 300 px move, (-30, 10) for (-300, 100) |
| Who sets the view camera | one exe call per frame, return address **0x0049217E**, on the render camera ([ROCRender+0x170]), plus Genesis-internal calls (0x1003ce86 / ced5 / b80c / b876 / f19d / f20c). **None** while paused |
| Override at RenderFrame entry (p2-p4) | Reads back (y 170.2) but is **gone by the world render**: the screen is unchanged and the next frame reads the old transform. Something rewrites the camera inside the render without the exported setter |
| Override at geEngine_RenderWorld (p5) | **Works:** the camera passed in (= the render camera) is set +150 y and the view rises (`p5_a_override.png`: sky over the bank front, no viewmodel) |
| Snap back | resume -> the player's view again (`p5_b_resumed.png`) |
| Frame rate with the clock paused | no Sleep cap (ComputeFrameDelta is skipped). ~50 fps here only because rendering is slow on the VM; **the feature needs its own cap** |

Not measured in the spike (left to the feature test, which reads them through the pipe):
fire state, burn and O2.

## Design consequences for steps 2-4

- **Pause:** a core service owns a `TickSubsystems` hook (skip while paused) and calls
  `GameClock::Pause` / `Resume` on the active subtimer.
- **Frame cap:** the paused frames get a ~60 fps Sleep cap from the service.
- **Camera override:** the `camera` service exposes it, and gfx's existing
  `geEngine_RenderWorld` detour (gfx owns that hook) applies it to the passed camera just
  before the world render. That is a core-to-core call; no second hook.
- **Free cam look:** from GameControls+0x18 / +0x1c. Movement keys via GetAsyncKeyState
  (no console / menu open).
- **Input:** the block byte (`controls.block_input`) is held while paused.
- **Keys:**
  - Pause: **P**. P isn't a game key, and the console's typing mode already blocks binds.
  - Free cam up / down: **E / Q**.
  - The probe's F5/F6/F7 were for the spike only.

_The logs, screenshots and run output named above were removed on 2026-10-27 (repository tidy-up); git history keeps them._

## Found while building the feature

- **A `loadmap` while paused deadlocked.** During a load, RenderFrame (and so the frame events)
  stops, and the load waits for the skipped simulation step. The pause service's
  TickSubsystems detour checks `game.state` and resumes (D34, "Ending a pause").
- **The console works while paused.** Enter is handled synchronously by `Console::OnKeyDown`
  0x00428C50 from WM_KEYDOWN, ahead of the input block.
- **The game camera bobs about 2 u** with the player standing still, so `tests/freecam.yaml`'s
  snap-back check allows 5 u.
