# FDNYCore progress

Spec: `core/LOOP.md`. One line per iteration in the log; findings,
assumptions and workarounds in their sections. Newest log entry last.

**Encoding rule:** edit this file (and every UTF-8 doc) with the Edit/Write
tools, never a PowerShell 5.1 `Get-Content`/`Set-Content` round trip. PS 5.1
reads UTF-8 without a BOM as ANSI and re-encodes every non-ASCII character
on write. This file got mojibake that way on 2026-10-21, and it is the likely
cause of RESEARCH.md growing to 465 MB.

## Status

| # | Item | State |
|---|---|---|
| 1 | Core skeleton + harness + foundation research | **DONE 2026-10-21**: live-tested, regression suite green, committed |
| — | Bind feasibility check (`bind <key> <command>`), owner request 2026-10-21 | not started (research before item 5) |
| 2 | Address table + version check | next |
| 3-12 | — | not started |
| 13 | Teleporter (save location / teleport, map-gated), owner request 2026-10-21 | not started (needs 5, 9) |

**Mid-item:** no.

## Environment facts (verified)

- 2026-10-21: Claude Code runs in interactive session 1 (`console`, Active), so GUI apps can be launched directly. `schtasks /it` is only needed from non-interactive shells.
- Exe SHA-256 `4b3b4dc1…d2fc3e`, Genesis.dll SHA-256 `5014bb87…6c4643` (full hashes in LOOP.md / `addresses.json`). Exe base 0x00400000, Genesis 0x10000000 in every run so far.
- The game folder already has:
  - Ultimate ASI Loader (`dinput8.dll`)
  - dgVoodoo (D3D8/D3D9/DDraw/D3DImm + `dgVoodoo.conf`)
  - FOV fix and alttabfix
  - LiveSplit and 2 autosplitters

  Game cfg: D3D via dgVoodoo, 640x480.
- **Rendering works in the VM.** dgVoodoo switched to windowed (`FullScreenMode = false`, `[DirectX] Resolution = unforced`; original in `core/backup/dgVoodoo.conf`). Game window is a 640x480 client area on a 1024x768 desktop.
- **Audio was the real blocker.** The VM has no sound device, and the game fatal-errors at startup with `TSoundEngine::open(): Error opening sound engine`.
  - Fixed with the VB-CABLE virtual audio driver (`get_vbcable.ps1`/`install_vbcable.ps1`). Its install also sets Audiosrv/AudioEndpointBuilder to Automatic.
- **alttabfix.asi had never loaded on the VM.**
  - It links MSVCP140/VCRUNTIME140, and the VC++ runtime was missing: ASI Loader dialog "Unable to load alttabfix.asi. Error: 126".
  - Fixed by installing the VC++ 2015-2022 x86 redist (`get_vcredist.ps1`/`install_vcredist.ps1`). VM-only; FDNYCore imports only kernel32/user32/bcrypt.
- ffmpeg is pinned to BtbN autobuild-2026-09-30-13-08 (`get_ffmpeg.ps1`/`install_ffmpeg.ps1`); gdigrab is available.
- Harness Python deps are in `config.ps1` `PythonPipPackages`: mss, Pillow, pydirectinput, PyYAML.
- **Host follow-up:** `provision.ps1` isn't on the VM copy. The host must add get/install for vbcable, vcredist and ffmpeg (and python, from an earlier session).
- The uploaded FOV fix `.asi` and `alttabfix.asi` are byte-identical to the installed ones. The owner's FOV ini (640x480) differs from the installed one (1920x1080); the owner's is the reference (`reference/original_tools/`).
- **Game console needs `ImADeveloper = true`** in `FDNYFireFighter.cfg`. Without it, the console object (`0x00502A90`) is never created, at the menu or in a level.
  - With it, backtick opens the console in typing mode (state 2) in-level, and typed lines run script verbs (`listmaps` printed all 18 maps).
  - The flag also skips the intro movies (WinMain plays ActiValue/Mekada only when it's 0).
  - Set in the game folder's cfg since 2026-10-21 02:06; original in `core/backup/FDNYFireFighter.cfg`.
- **Front end input:**
  - The splash advances on any LMB.
  - The menus draw their own cursor, driven by DirectInput mouse deltas (1:1, clamped to the screen); absolute cursor moves are ignored. The harness `gclick` slams to the top-left corner, then moves and clicks.
  - Path to Training: splash → `game` (97,457) → career slot "Hotrod Stein" (195,165) → briefing `GO!` (568,450) → about 20 s load (`harness/setups/load_training.yaml`).
- **Movement keys:**
  - W moves forward; S/A/D are presumably back and strafe (route uses them, not yet individually verified).
  - Mouse X turns the view.
  - `player.state` toggles 3/4 with C / Ctrl.
- **Game clock:**
  - The game is hard-capped at 60 FPS: `GameClock::ComputeFrameDelta` 0x0042BF70 busy-waits with Sleep(1) while dt <= 1/60.
  - Median measured fps in the VM is about 55-57.
  - The focus-loss pause is `GameClock::Pause` 0x0042BF20, called from `MainWindow::RunMessageLoop` on WM_ACTIVATEAPP(wParam=0). This resolves R2's "WndProc ignores WM_ACTIVATEAPP" conflict: it's handled in the message loop, not the WndProc.

## Research (item 1: R1-R5)

### R1/R4 Genesis exports + exe imports (Haiku PE parse, `core/harness/pe_dump.py`)

Full lists: `core/notes/genesis_exports.txt` (536 named exports, no forwarders) and `core/notes/exe_imports.txt` (291 imports, 9 DLLs). Stdcall exports are decorated (`@name@N`); cdecl ones are bare. Hook these with GetProcAddress by their exact decorated name.

- **Frame:**
  - `geEngine_BeginFrame` (RVA 0x25B70), `geEngine_RenderWorld` (0x24950), `geEngine_EndFrame` (0x260E0).
  - The game's per-frame render is `ROCRender::RenderFrame` 0x00494670. It runs BeginFrame → draw entries → `Console::Render` → FlushPipeline → EndFrame, and it skips everything while `gameState == 2`.
  - The render is called from `GameSession::RunFrame` 0x0042AEA0, after `GameClock::Tick`. The menu/loading path (`MenuManager::TeardownToLoading`) also calls Begin/EndFrame.
- **In-frame drawing (piggyback):** the exe already imports `geEngine_Printf` (0x24B60) and `geEngine_DrawBitmap` / `FillRect`. `geFont_*` is a fallback.
- **Camera/FOV:** there's no SetFov export; FOV goes only through `@geCamera_SetAttributes@12` (0x1350). The FOV fix patches Genesis.dll+0x1383, which is inside SetAttributes.
- **Level load:** candidates are `geWorld_Create` (0x3E810) and `geEngine_AddWorld` (0x243D0) / `RemoveWorld`.
- **Input:** Genesis has no input exports. The exe uses DirectInput8 + the WM pump (see R2).

### R3 Existing console (Haiku Ghidra passes; confirmed live 2026-10-21)

- **Object:** `main\console.cpp`, instance global `0x00502A90` (Console ctor 0x00427910, `DevConsole::Shutdown` 0x00427BD0). It is only created with `ImADeveloper = true`.
- **Functions:**
  - `OnChar` 0x004274F0, `OnKeyDown` 0x00428C50, `AddLine` 0x004286E0
  - `Print` 0x00428930, `WatchPrint` 0x00429100
  - `AddHistory` 0x00428A20, `AddCompletionCandidate` 0x00428970
  - `LoadFont` 0x004275A0 (`data\sprites\confont.bmp`), `Render` 0x00427F00, `SetState` 0x004274B0
- **State `[con+0x49C]`:** 0 closed, 1 view-only (Tab), 2 typing (backtick/tilde via WM_CHAR, live-confirmed). Esc closes.
  - `SetState` also writes `[[0x503A78]+0x330] = (state==2)`. That's probably the game's input-block flag; not yet verified.
- **Enter:** echoes `--> line`, then the line goes to the object at `0x00502FB0` (`+0x78`, via 0x00470F10 / 0x00475310). It's a deferred submit, consumed by the script engine; live, `listmaps` worked.
- **Script verbs:** a string-compare chain in `ScriptTrigger::HandleVerb` 0x0047A1F0 with about 60 `Verb_*` handlers, incl.:
  - `Verb_LoadMap` 0x00478150 (`loadmap`)
  - `Verb_ListMaps` 0x00479670
  - `Verb_Exec` 0x0047A070
  - `Verb_Set` 0x004750E0
- **Swallowing:** WM_KEYDOWN reaches the game (0x00499000) only while `[con+0x49C]==0`.
- **Config table** at 0x004FF524:
  - `ImADeveloper` 0x00502A78
  - `EnableDebugLogging` 0x00502A79
  - `SkipMenus` 0x00502A7A. Useless for the harness: it fires 3 no-op activations at the splash. There are no command-line switches and no start-map cfg key.
- **Piggyback plan for item 4:** intercept the submitted line, route `fd_*` / `echo` / `cvarlist` / `exec` to our registry, and print via `Console::AddLine`. Players need the console object to exist. The core must either set the `ImADeveloper` byte before console creation or create the console itself; that's a decision for item 4. Mind the side effects: the intro movies get skipped and the debug message feed turns on.

### R2 Input path (Haiku Ghidra pass)

Input reaches the game by two parallel paths:

1. **DirectInput 8, buffered.**
   - `GameControls::PollFrame` 0x00439330 → source poll 0x004394A0 → `DrainMouseBuffer` 0x00437AB0 / `DrainKeyboardBuffer` 0x00437C80. PollFrame then calls `SetCursorPos(200,200)`.
   - Key records sit at provider+0x16+3*DIK; the action layer is `GameControls::IsHeld/WasPressed/WasReleased/GetAxis`.
2. **Window messages.**
   - `MainWindow::WndProc` 0x004996C0: WM_KEYDOWN goes to 0x00498FC0 (console first, then the game only if the console is closed). WM_CHAR goes to the console only. Mouse messages go to 0x004990D0.

**Swallow plan:** skip the poll and zero the provider state while our UI is open, and filter WM input at WndProc. The game's own console already blocks the WM key path.

### R5 Autosplitter memory paths (Haiku read of both .asl)

Both `.asl` files share one `state("FDNYFirefighter")` block: 12 vars, all main-exe-relative, no sigscans. Two static roots:

| var | type | chain | used by |
|---|---|---|---|
| gameState | int | `[0x00502AAC]+0x6F0` | start (==1), split gate, ILs reset (==0), isLoading (==2/3) |
| mapName | string10 | `[[[0x00502AAC]+0x688]]+0xC` | splits |
| totalVictims / rescuedVictims | int | `[[0x00503A78]+0x30]+0x3528/+0x352C` | declared, unused |
| 8x level-end sound flags | int | `[[[[0x00503A78]+8]]+off]` | splits (0 → non-zero) |

Live 2026-10-21: in Training, gameState = 1 and mapName = `training`. All 12 chains resolve; their final (heap) addresses differ run to run even without the core.

## Build / harness notes

- **`core/build.ps1`:** zig 0.16 freestanding x86 (no CRT; kernel32/user32/bcrypt), `-Wall -Wextra -Werror`.
  - Debug adds `-O0 -g -fno-sanitize=all -DFDCORE_DEBUG`, because zig turns UBSan on at -O0 and a no-CRT DLL has no sanitizer runtime.
  - `-Wno-unused-command-line-argument` is needed because zig injects `-fno-rtlib-defaultlib`.
  - Zig emits a PDB per config. Preferred base 0x3D000000, relocatable.
- **Core startup:**
  - DllMain opens `FDNYCore.log` next to the .asi and spawns an init thread.
  - The thread waits for Genesis.dll, SHA-256s the exe and Genesis (BCrypt), then logs the version line and `FDNYCore ready`, about 0.1-1.3 s after load.
- **Harness flow** (`core/harness/run_test.py` + `fdh/`):
  1. Launch and wait for the window.
  2. **SetForegroundWindow**: screenshots and video grab a screen region, and input goes to the foreground window.
  3. Wait for the ready line, attach pymem, and pick the address-table version by exe+Genesis hash.
  4. Record `run.mp4` with ffmpeg gdigrab.
  5. Run the steps, with a crash check on every step: exit, a `#32770` dialog owned by the game, or a SendMessageTimeout hang > 10 s.
  6. Kill the game and write `result.json`.

  `--core none` renames the core to `.asi.off` for baseline runs. `--suite` runs `core/tests/*.yaml` (not `tests/explore/`).
- **Step types:** `wait_s`, `key`, `type`, `click`, `gclick`, `mouse_rel`, `lclick`, `focus`, `screenshot`, `assert_screen_nonblack`, `assert_mem`, `sample_mem` (+`append`), `sample_addr`, `sample_pos`, `assert_log`, `repeat`, `console` (the console step still needs `ImADeveloper`; it becomes real in item 4).
- **Address table:** `core/addresses.json`, JSON so the build-side generator and the Python harness need no extra deps. The harness reads it with `utf-8-sig`, because PowerShell 5.1 writes a BOM.
  - It holds the 12 autosplitter chains, console globals, player position/ground/state (`[[0x503A78]+0x30]` + 0x4C/0x12C/0x18C, from the collision overlay) and `g_gameClock` 0x00502AB8 fields (`deref_root: false`: +0x44 elapsed ms, +0x4C dt float, +0x50 fps **float**, verified live as 60.0 under the cap).
  - `sig` fields are filled in item 2.
- **`harness/compare_runs.py`:** item 1(c) baseline equivalence. Two no-core runs define the noise envelope, and the core run must sit inside it.

## Assumptions / workarounds

- **"Behaves the same" (item 1c) definition**, chosen by the agent at the owner's request. These are LOOP.md's 7 criteria, with two calibrations:
  - **(4)** Autosplitter final addresses are heap pointers that move run to run even without the core. So the check requires all 12 chains to resolve in every run, plus an exact match only where the two baselines agree with each other.
  - **(6)** Lap-position tolerance = max(32 u, 2× the baseline-pair spread). Scripted wall-clock input isn't frame-exact; the observed core-vs-baseline distance was 0.6-5.8 u.
- **Saves:** backed up to `core/backup/Saves` after several harness runs had already started the game, and the game rewrites `player*.rb`. The pristine originals are in the repo-root `FDNYSP1.1_0e4c1e9f.zip`. Harness runs only enter Training on the "Hotrod Stein" slot.
- **Tests run with `ImADeveloper = true`** in the game cfg (console availability), in both baseline and core runs.

## Log

- 2026-10-21 it1: created PROGRESS.md and dispatched 4 Haiku research subagents (R1/R4, R5, R3, R2).
  - R1-R5 done. MinHook 1.3.3 vendored to `core/third_party/minhook/`.
  - Smoke launch #1 hit the FMOD fatal error. Installed VB-CABLE; smoke #2 renders the splash windowed.
- 2026-10-21 it2: FDNYCore 0.1.0 skeleton (Debug+Release clean) loads live and logs version, hashes and ready. Harness v1 written; `core_loads` PASS. Fixed VM gaps: VC++ x86 runtime, ffmpeg.
- 2026-10-21 owner request: added backlog item 13 (teleporter, map-gated) and a bind feasibility check (before item 5) to LOOP.md.
- 2026-10-21 it2 (cont.): mapped the front end (DirectInput cursor → `gclick`) and found `ImADeveloper` enables the console (`listmaps` works in-level). Added player/clock entries.
  - Wrote `baseline_route` (10 laps ≈ 2.5 min in-game) and `compare_runs.py`.
  - Baseline equivalence: **EQUIVALENT** (runs `2026-10-21-022020-base_a`, `022253-base_b`, `022526-core`; fps 55.6 vs 57.2; laps within 5.8 u). The first attempt read fps as int; fixed to f32 and re-ran.
  - Regression suite (`baseline_route`, `core_loads`) PASS, Debug. **Item 1 DONE**, committed with evidence in `core/evidence/item01/`.
