# Pause + free camera (fd_pause): loop status

Owner request (2026-10-22): a practice-only pause with a free-flying camera.
- `fd_pause` toggles. While paused, GameSession::TickSubsystems is skipped and the game
  clock is paused (GameClock::Pause / Resume, as on alt-tab). Rendering and the FDNYCore
  overlays continue.
- Free camera: FDNYCore owns a position + yaw/pitch and sets the render camera
  (ROCRender+0x170) each paused frame, inside the core `camera` service. The player
  entity never moves.
  - Speed `fd_freecam_speed` 400 u/s × real frame time; Shift ×4, Ctrl ×0.25.
  - Mouse `fd_freecam_sens`; pitch clamped.
- Game input blocked while paused. Resume = stop overriding the camera.
- Practice-only; unpause on level load, menu, `fd_profile run`; view-relative overlays hidden
  while paused (core service, R2). Default key proposed in DECISIONS.
- Frame advance: out of scope (separate future feature).

| Step | State |
|---|---|
| 0 Preconditions (core_loads) | **done**: PASS in suite s0181 (2026-10-22) |
| 1 Spike: Ghidra + live probes, verdict | **done**: feasible; the override goes at geEngine_RenderWorld (README.md) |
| 2 addresses.json entries | **done**: 10 entries, 137 total; addr_verified PASS |
| 3 Core: sim-pause service + camera override | **done**: `pause` service, camera override, textdraw HideWhilePaused; pause_service PASS 2/2; regressions PASS |
| 4 Feature src/features/freecam.cpp | pending |
| 5 Suite test tests/freecam.yaml | pending |
| 6 Suite, docs, version, commit, owner report | pending |

## Log

- it1: git pull (up to date). The game is busy with the fd_juggle 0.18.1 suite (separate
  topic, separate commit), so this iteration does the static part of step 1 only. Three
  read-only haiku Ghidra lookups:
  - (a) TickSubsystems' four subsystem slots, and any state or animation advanced outside
    it (xrefs to g_frameDeltaTime / g_gameTimeMs reached from RenderFrame);
  - (b) the writers of the render camera transform (geCamera_SetWorldSpaceXForm /
    SetWorldSpaceVisXForm import xrefs);
  - (c) GameControls::PollFrame: DirectInput cooperative levels, where mouse deltas and
    keys land, how the input-block byte works, and the default key list (for a free key).
  - (a) result:
    - Call site: CALL TickSubsystems at 0x0042af0a (`e8 11 e6 ff ff`). It returns to
      0x0042af0f, and RenderFrame is called at 0x0042af19.
    - Slots +0x08/+0x50/+0x98 share one generic entity-list sweep (0x0049c0e0): update
      each node via the container's slot 1, then reap dead nodes. The per-node updaters
      (0x49c410/0x49c3d0/0x49c3e0) are not in the DB as functions.
    - The +0xe0 slot (0x0049bd20) is unresolved.
    - **Simulation on the render side:** `Sprite::Render` → `ParticleNode::AdvanceSpawn`
      advances emitter keyframes from `g_gameTimeMs`, and the colour/alpha ramps read it
      too. Pausing the game clock (Pause/Resume) should therefore freeze these as well.
      The live probe must check it.
    - `ParticleNode::IntegrateMotion` (reads `g_frameDeltaTime`) is called only from
      simulation-side owners (FireFx::Update, PlayerHose::Think, AshSim::Think, ...).
    - **Gap:** RenderFrame's per-world draw is a virtual call, invisible to static
      analysis. The live probe compares state before and after a pause, which covers it
      empirically.
  - (c) result:
    - **Polling:** GameControls::PollFrame (0x00439330) runs before the simulation and
      calls DirectInputSource::Poll (0x004394a0): DrainMouseBuffer (0x00437AB0) +
      DrainKeyboardBuffer (0x00437C80), buffered GetDeviceData, then
      `SetCursorPos(200,200)` every frame.
    - **Cooperative level:** both devices use 0x0A = NONEXCLUSIVE | BACKGROUND.
      Ordinary Win32 key reads (GetAsyncKeyState) are not blocked. The Windows cursor
      is useless as a mouse source, because the game re-centres it every frame.
    - **Mouse deltas:** the game's own per-frame sums at GameControls+0x18 (X) and
      +0x1c (Y), zeroed at the start of each drain. Polling isn't gated, so they keep
      updating while paused. **Plan: the free cam reads these.**
    - **Keys:** a 3-byte record per DIK at GameControls+0x26 + 3*DIK (changed, pressed),
      with the held latch at +0x28 + 3*DIK.
    - **Input block:** the byte at GameControls+0x330 (controls.block_input) makes only
      IsHeld / WasPressed / WasReleased return 0 (0x00437430 / 0x004374C0). Polling and
      GetAxis (mouse look) are not gated. Holding the byte while paused therefore keeps
      actions away from the game; mouse look is irrelevant, since Think doesn't run.
    - **Default keys:** W A S D C, 1-3, Ctrl, Shift, Space, the arrows, LMB (ACTION),
      RMB (JUMP), plus F2-F4 and F6-F8/F11 in dev mode.
    - **Pause key:** FDNYCore already uses T (triggers), F9 / F10 / F12 (collision
      overlay) and F5 / F6 (teleporter suggestion), so the proposal is **P**. Free cam
      up/down: **E / Q**. Shift and Ctrl double as the game's USE / CROUCH, which is
      harmless while input is blocked.
  - (d) GameClock::Pause / Resume (0x0042BF20 / 0x0042BF40):
    - ABI: `this` = the active subtimer in ECX, no stack arguments, plain RET. The game
      picks `this` as 0x502AD8 if the selector byte 0x502AF8 is set, else 0x502AB8.
    - Pause sets the paused byte (+0x1c) and a timeGetTime stamp (+0xc). Resume adds the
      paused span to the base (+0x8).
    - While paused, ComputeFrameDelta (0x0042BF70) skips its whole body. So:
      - elapsed (+0x44) stops, which also stops the render-side particle keyframes;
      - dt (+0x4c) keeps its last value. That's harmless, because the simulation isn't
        running.
      - The 60 FPS Sleep cap is skipped too, so a paused game renders uncapped. That's
        to be measured, and fd_pause may need its own cap.
    - On resume, the first dt is roughly one normal frame (the clamp is 0.1 s).
  - Probe written: `probes/pauseprobe.cpp` -> `zz_pauseprobe.asi` (image base 0x3E600000,
    in `build_probes.ps1`). It hooks TickSubsystems, RenderFrame and the Genesis
    SetWorldSpaceXForm export. Keys: P pause, K clock pause too, O camera override
    (+150 y). Test: `tests/probes/probe_pause.yaml`. Waiting for the game to be free.
  - (b) result, partial:
    - The exe imports `geCamera_SetWorldSpaceXForm` (IAT 0x004dc158) but not the Vis
      variant.
    - Its only user is a thunk method at 0x00491c60:
      `SetWorldSpaceXForm([this+0x5c], &this[+0x10])`. That is slot 0 of a vtable at
      0x004e3b50, near the string "ROC\Renderer\ActorInfo.cpp". It is reached only by
      virtual dispatch, so no static caller was found.
    - `geCamera_GetWorldSpaceXForm` (IAT 0x004dc0b0) has one caller, 0x0043fd40, a
      line-of-sight helper that reads ROCRender+0x170's position.
    - Open: who sets the ROCRender+0x170 camera each frame.
    - Plan: answer it live, not statically. The probe hooks the Genesis export
      `geCamera_SetWorldSpaceXForm` and logs, per frame, the camera pointer, the return
      address and the xform, with and without TickSubsystems running.
- it1 (continued): the s0181 suite finished (core_loads PASS -> step 0 done). fd_juggle
  0.18.1 was committed separately (88f7cfc).
- Step 1 live, probe runs p1-p5 (`README.md` has the table):
  - p1 void: letter keys typed by the harness toggled the probe; it now uses F5/F6/F7.
  - p2: the simulation, victims and mission timer are frozen. The game's mouse sums work
    while paused.
  - p2-p4: the override set at RenderFrame entry reads back, but the world render doesn't
    use it; the camera is rewritten inside the render.
  - p5: the override at geEngine_RenderWorld works and snaps back on resume. With
    GameClock::Pause the clock freezes and the first dt after resume is normal. There is
    no frame cap while the clock is paused.
  - **Verdict: feasible.**
- Next (it2, step 2): addresses.json entries:
  - session.tick_subsystems 0x00429520;
  - clock.pause / clock.resume 0x0042BF20 / 0x0042BF40;
  - clock.f_selector (+0x40, choosing subtimer +0x20);
  - controls.f_mouse_x / f_mouse_y (GameControls +0x18 / +0x1c);
  - genesis.camera_get_world_xform / camera_set_world_xform (exports).
  Check existing entries first (clock.*, controls.block_input, genesis.render_world).
- it2 (step 2, done): git pull (up to date). Ten addresses.json entries; their 16-byte
  signatures were read from the game files on disk (read-only):
  - code: `sim.tick_subsystems` (0x29520), `clock.pause` (0x2BF20), `clock.resume`
    (0x2BF40);
  - clock data (g_gameClock root and signature): `clock.game_timer` (+0),
    `clock.real_timer` (+0x20), `clock.selector` (+0x40, u8);
  - input (g_everGame root, the block_input signature): `controls.mouse_x` / `mouse_y`
    (GameControls +0x18 / +0x1C, i32);
  - Genesis exports: `genesis.camera_set_world_xform` (0x1990: copies the xform to
    camera+0xD0), `genesis.camera_get_world_xform` (0x1A40: `lea eax,[ecx+0xD0]; ret`).
  - Each has a ghidra/note citing the spike.
  - Existing entries reused: `controls.block_input`, `clock.dt` / `elapsed_ms`,
    `genesis.render_world` (gfx's hook).
  - Build clean, both configs. addr_verified PASS ("signatures: all OK", run
    2026-10-22-215221).
- **Noted for step 3:** `glitch.tick` (an inline hook at 0x0042AF0F, the return from
  TickSubsystems) still fires on paused frames, because it sits in RunFrame and not in the
  skipped call.
  - The collision overlay counts it as "one simulated frame", so while paused it would
    record frames in which nothing moved.
  - The pause service will publish its paused state; glitch should skip its tick then.
    That's a core-service read, not a dependency on a feature.
- Next (it3, step 3): core services.
  - New service `pause`:
    - owns the sim.tick_subsystems hook; `pause::Set(bool)` / `pause::Active()`;
    - clock Pause/Resume on the active subtimer;
    - holds the input block while active (restoring the console's own use);
    - a ~60 fps Sleep cap while active;
    - auto-unpause on level load, menu and the run profile (`profile` asks the service);
    - "view-relative overlays hidden": textdraw panels flagged view-relative hide while
      active.
  - camera: `camera::SetWorldOverride(const float xform[12])` / `ClearWorldOverride()`,
    applied by gfx's RenderWorld detour through `camera::ApplyWorldOverride(cam)`, plus
    `camera::LastWorldXForm()` for the start pose.
  - DECISIONS entries. Regression: fov_sets_camera, fov_parity_reference, text_panels,
    text_alpha, triggers, glitch_records.
- it3 (step 3, done):
  - **`src/pause.{h,cpp}`** (new core service, started in core.cpp after patches):
    - owns the `sim.tick_subsystems` hook (group "pause"), enabled only while paused;
    - `Set(on, why)` / `Active()` / `PausedFrames()`;
    - GameClock::Pause / Resume on the active subtimer, re-paused each frame if the
      game's alt-tab Resume cleared it (new field entry `clock.f_paused` +0x1C, 138
      entries);
    - `controls.block_input` held while paused, released on resume unless the console
      is open;
    - a ~60 fps QPC + Sleep(1) cap (the 1/60 s tick count uses 32-bit division; D1 has
      no 64-bit helpers);
    - ends itself on a level load, on game state != 1 (frame begin), and **from its own
      TickSubsystems detour**.
  - **camera:** `SetWorldOverride` / `WorldOverrideActive` / `LastWorldXForm`, applied
    in `BeforeWorldRender(camera)`, which gfx's RenderWorld detour calls (core to core).
    It uses the Genesis Set/GetWorldSpaceXForm exports.
  - **textdraw:** `HideWhilePaused(panel)`. fd_juggle's arrow panel sets it (D35).
  - Debug pipes (R6): `pause on|off` and `camera_override <dy>|off`, applied on the game
    thread.
  - **Bug found and fixed:** a game `loadmap` typed while paused deadlocked.
    - While loading, RenderFrame (and so the frame events) stops.
    - The load waits for the skipped simulation step.
    - Fix: the detour checks `game.state` and resumes.
    - Run fc3 failed this way, reproduced in fc3b; fc3c and later pass.
  - **Console while paused works:** FDNYCore lines and `echo` ran; console.state stayed
    0. Enter is handled synchronously by Console::OnKeyDown 0x00428C50 from WM_KEYDOWN
    (Ghidra; `tests/probes/probe_pause_console.yaml` run pc1).
  - **tests/pause_service.yaml:**
    - paused 4 s: victims, mission timer, clock and player position unchanged
      (exactly); the hook on, the block on, the clock paused;
    - the camera override lifts the view 150 u (screenshot);
    - on resume: no clock jump, the hook off, the block off;
    - a loadmap while paused resumes ("level change").
    - PASS 2/2 (fc3d1, fc3d2).
  - Regressions PASS (`fc3reg`): core_loads, fov_sets_camera, fov_parity_reference,
    text_panels, text_alpha, triggers, hooks_frame, hooks_toggle, glitch_records,
    fire_juggle.
  - DECISIONS D34 (pause + camera override design, rejected options, glitch tick side
    effect) and D35 (view-relative panels).
- Next (it4, step 4): `src/features/freecam.cpp`.
  - `fd_pause` cvar (practice-only, default 0, default key P): its onChange calls
    pause::Set. Profile run resets it to 0, which resumes.
  - Free camera:
    - start = `camera::LastWorldXForm`; yaw/pitch derived from the rotation;
    - movement each paused frame from GetAsyncKeyState W/A/S/D/E/Q with Shift/Ctrl, only
      when `binds::KeysLive()`;
    - mouse from `controls.mouse_x` / `mouse_y`;
    - `fd_freecam_speed` 400 and `fd_freecam_sens`;
    - real time from QPC.
  - The geXForm3d axis convention (which row is forward) must be checked against the
    measured yaw convention.
  - Debug pipe `freecam`.

