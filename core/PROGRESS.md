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
| — | Bind feasibility check (`bind <key> <command>`), owner request 2026-10-21 | **DONE (research)**: feasible with constraints, see "Bind feasibility" |
| 2 | Address table + version check | **DONE 2026-10-21**: live-tested, suite green, committed |
| 3 | Hook manager + events | **DONE 2026-10-21**: live-tested, suite green, committed |
| 4 | Console (on the game's console) | **DONE 2026-10-21**: live-tested in player config, suite green, committed |
| 5 | Variables, config, binds | **DONE 2026-10-21**: live-tested, suite green (11/11), committed |
| 6 | fov module | **DONE 2026-10-21**: parity with the original fix measured, suite green (13/13), committed |
| 7 | window module | next |
| 8-12 | — | not started |
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

## Bind feasibility (owner request; Haiku Ghidra pass 2026-10-21; not yet live-verified)

**Verdict: feasible, with constraints.** `bind <key> <command>` can't piggyback as-is, because the game has no bind verb, no key-name table, and no "key runs a line" mechanism. The plumbing it needs does exist and is reusable. Item 5 builds the binding store and the verb.

- **Every console line is a script line.** `Console::OnKeyDown` (Enter, 0x00428FD3) calls `ScriptEngine::ExecLine(g_scriptEngine, line)` 0x00470F10, and that is ExecLine's **only** caller.
  - So the single interception point for **all** our console commands (item 4) is a pre-hook on ExecLine: handle `fd_*` / `bind` / `unbind` / `echo` / `cvarlist` / `exec`-of-our-cfg ourselves, pass everything else through.
  - A bound key can run a line by calling the same ExecLine with the bound text, so game verbs and `fd_` commands both work from binds.
- **Verbs in the game:**
  - `ScriptTrigger::HandleVerb` 0x0047A1F0: if/ifnot/set/inc/dec/print/once/ignore/exec/execsetsonly/sendmsg/fog*/*snd*/hint/quake/if-comparisons/listmaps/loadmap/loadhosemap/setlighttable/difficulty/quit/debugvictims/runrescuebot/listvars.
  - `ScriptActor::HandleBaseVerb` 0x0046F9D0: que/flush/wait/idle/cmd/loopinque/onmsg/onmsgappend/printmsgs.
  - There's no bind/unbind/alias. Names to avoid colliding with: `exec` is a game verb (it loads Data\Scripts / Data\Maps), so our config exec should be `fd_exec`, or `exec` should fall back to the game verb when the file isn't ours. **Decide in item 4/5.**
- **Default controls are hardcoded:** 26 `ActionMap::BindKey` 0x00437410 calls inside `GameControls::Construct` 0x004395C0, with keys as raw **DIK** codes. Mouse uses custom codes 0x101-0x105 (ANY_KEY, ANY_MOUSE_BUTTON, MOUSE_LB/MB/RB).
  - Movement: W/S/A/D + arrows.
  - CROUCH = C / LCtrl / RCtrl.
  - **USE = LShift / RShift / Space**, ACTION = mouse LB, **JUMP = mouse RB**.
  - SELECT_* = 1-4.
  - Dev keys: F2 / F3 / F4 / F6 (SKIP_HOTSPOT and ACTIVATE_RESCUEBOT share code 0x40 in the decompile) / F7 spectator / F8 WIN_LEVEL / F11 CRASH_TEST.
  - `ActionMap::BindAction` 0x00438BF0 asserts "Invalid binding" for an unregistered name; new actions need `RegisterAction` 0x00438D70 first.
- **Constraints for item 5:**
  1. We ship our own key-name → DIK table (incl. mouse4/5 and wheel).
  2. Bound-key detection reads DIK state from the DirectInput keyboard buffer (`DrainKeyboardBuffer` 0x00437C80 state) or our own WM_KEYDOWN filter. Binds must not fire while the console is open (state != 0).
  3. A bind on a key the game already uses (W, Space, ...) runs **both** our line and the game action, unless we swallow it. Default: don't swallow, and warn in the console when binding a game-used key.
  4. Backtick/tilde stays reserved for the console.
  5. Binds persist in the core's own cfg (written by `fd_writeconfig` / on exit), not the game's `FDNYFireFighter.cfg`, which the game rewrites at startup and exit.
- **Harness note:** my `baseline_route` presses Space, which is USE, not jump. Jump is mouse RB. Not a problem for equivalence, since both runs do the same; noted for future routes.

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

## Item 2: address table + version check (done)

- **Signatures.** `core/tools/addrgen.py sigs <exe>` finds, for each exe entry's static root, the first code instruction that embeds that root as an absolute operand. It stores the instruction plus 6 context bytes as the entry's `sig`: `{rva, bytes, ref, ghidra}`.
  - This works because the exe has no relocations and is always at 0x400000. Matching bytes in memory prove the global is where the table says.
  - Five distinct sites cover all 23 entries; each is confirmed in Ghidra as an xref to its global (Haiku pass):

    | site | instruction | function |
    |---|---|---|
    | 0x0042465A | `MOV ECX,[0x502AAC]` | TPlayerInfo::RecordLevelComplete |
    | 0x004274C2 | `MOV EDX,[0x503A78]` | FUN_00427420 |
    | 0x00427920 | `MOV [0x502A90],ESI` | (see below) |
    | 0x0042AEC0 | `MOV ECX,0x502AB8` | GameSession::RunFrame |
    | 0x004220BE | `MOV AL,[0x502A78]` | SArray::OobFail = the debug-message dispatcher |

  - **Decomp follow-up (not ours to fix here):** Ghidra places 0x00427920 in `PlayerRescueEnt::Init @0x004277D0`, but the R3 pass called it the console ctor at 0x00427910. That's a function-boundary labelling question for the decomp loop.
- **Compiled-in table.** `build.ps1` runs `addrgen.py header` to produce `src/gen/addresses_gen.h` (gitignored, regenerated every build): an `Id` enum plus `kVersions[]`. Python is a build-time tool on the VM only; players get the table inside the .asi.
- **Startup (`src/addr.cpp`):**
  1. Select the version by exe + Genesis SHA-256.
  2. Compare each entry's signature bytes in memory via `SafeRead`, which uses VirtualQuery and never faults.
  3. Mark each entry usable or not.
  4. Unknown version → one WARN, and every module disabled.
- **Modules (`src/modules.cpp`):** each module declares required `Id`s. `StartAll` enables a module only if the version is known and all its entries verified, and logs `module X: enabled` / `disabled (unknown version)` / `disabled (needs <entry>)`.
  - The first module is `levelwatch`: it logs map/state changes, polling at 4 Hz from its own thread until item 3's frame events exist. Live, it shows state −1 → 0 (menu) → 3 (loading) → 1 (in level), the seed of the level-load event.
- **Debug-only override:** `FDNYCore.addroverride.txt` next to the .asi, in the `addrgen.py text` flat format. Tests feed patched tables through it without touching the exe.
  - Fixtures come from `tools/make_override_fixtures.py`: `tests/data/addr_override_unknown.txt` (hashes zeroed) and `addr_override_badsig.txt` (first sig byte of `game.map_name` flipped).
  - The harness places them via the new `pre_files` test key and removes them after the run.
  - Verified absent from Release: no `addroverride` string in the Release .asi.
- **Tests:** `addr_verified`, `addr_unknown_exe`, `addr_bad_signature` (all live, in Training). `core_loads` now expects `FDNYCore 0.2.0`; this is a version bump, not a weakened check.
- **Deviation:** I didn't run the new tests red before implementing; the item-1 binary had already been rebuilt. Each one asserts log lines 0.1.0 never emitted, so all would have failed on their first assertion.

## Item 3: hook manager + events (done)

- **Hook manager (`src/hooks.cpp`).** MinHook 1.3.3 is compiled with `zig cc` (third-party, no `-Werror`) and statically linked. Both builds still import only KERNEL32/USER32/bcrypt.
- **Frame hooks.** `geEngine_BeginFrame` / `geEngine_EndFrame` are resolved **by export name**. They're in the address table as Genesis entries `genesis.begin_frame` (RVA 0x25B70) and `genesis.end_frame` (0x260E0).
  - The signature is the first 16 prologue bytes, plus a cross-check that GetProcAddress == base+rva.
  - Both are **cdecl**: undecorated exports, and the exe cleans the stack after both call sites (`add esp,4` after EndFrame at 0x004949A6).
  - The two hooks toggle together via `MH_QueueEnable/Disable` + `MH_ApplyQueued`, so a frame never sees one hooked and not the other.
- **Events.**
  - `frame_end` is dispatched inside the EndFrame detour **before** the original runs, so later in-frame drawing lands before the flip.
  - `level_load(map)` is raised by `levelwatch` (now a frame_end subscriber, no thread) when state reaches 1 with a map name. Live: `event: level_load 'training'`.
  - `victim_spawn` arrives with item 8.
- **Debug-only test pipe `\\.\pipe\FDNYCore`** (`src/testpipe.cpp`, wholly `#ifdef FDCORE_DEBUG`; verified absent from Release). Commands: `ping`, `stats` (begin/end/events/levels/toggles counters + frame on/off), `hook frame on|off`. Harness client: `fdh/pipe.py`.
- **Independent frame count.** The game has no frame counter, but `g_gameClock`'s published elapsed-ms (+0x44) changes once per `GameSession::RunFrame`. In a level each RunFrame renders exactly one Begin/End pair. The harness step `assert_frame_hooks` polls it in a tight loop and compares against the core's counters.
- **Results:**
  - `hooks_frame`: clock ticks = BeginFrame = EndFrame = frame_end events = **287/287/287/287** in 5 s (and 286 again while moving).
  - `hooks_toggle`: 50× off/on with 0 failures and no crash. Afterwards hooks fire once per frame again (286, 172). While off, all deltas stay 0 for 2 s.
  - Tolerance ±2 is for window-edge races only; actual differences were 0.
- **Red run first:** `hooks_frame` against the item-2 core (`git stash` of the new sources) failed for the right reasons: no hook log lines, and no test pipe.
- **Version single-sourced:** `core/VERSION` → `build.ps1` generates `src/gen/version_gen.h`, and the harness substitutes `{core_version}` in `assert_log`. Without the generated header, `core.cpp` refuses to compile.
  - This fixed `core_loads`, which first failed in the suite after the 0.3.0 bump. Its check is unchanged: it still matches the exact running version.
- **Release 0.3.0, hooking every frame**, is still EQUIVALENT to the item-1 baselines (fps 58.8 vs 55.6, laps within 3.5 u).

## Item 4: console (done)

- **It's the game's console.** FDNYCore doesn't draw its own: it makes sure the game's developer console exists for every player, intercepts typed lines, and prints into it. The console is drawn by the game's own `Console::Render`, inside RenderFrame, so it shows on any driver (screenshot `evidence/item04/02_console_exec.png`).
- **Console without ImADeveloper (decision, reversible).** The game creates the console only when `ImADeveloper` is set: in `roc_CreateEngineSubsystems` 0x004992D0, `JZ` at 0x00499370 skips operator new(0x4A0) + ctor 0x004277D0(640, 460) + `MOV [MainWindow+0x70],EAX`.
  - Forcing the flag would turn on about 16 debug paths: diagnostic spam into the console, dev keys, F12, a 0xDEADBEEF sentinel write per frame, extra debug HUD objects, and RescueBot/hose debug text.
  - So the core NOPs **only that JZ**, in memory, from DllMain, which runs before WinMain (verified live: `g_mainWindow` and `g_console` are still null there). The game then creates its console itself at the normal moment.
  - `addr::EarlyCodeAddress` verifies the patch site's signature before writing (no version selection yet at DllMain). `addr::Init` later counts the pre-patch verification, so it doesn't report the patched bytes as a mismatch.
- **Rejected approach (documented failure).** Calling the ctor ourselves at the first frame crashed: AV at 0x0047EAC8 reading `[g_everGame 0x00503A78]+4`.
  - Some frames render **before** WinMain reaches `roc_CreateEngineSubsystems`, which is also where `g_everGame` (the root) is allocated, at 0x0042CD65.
  - The ctor depends on that root, and also on root+4 (the 0x17C-byte subsystem, 0x00495190).
  - Evidence: `evidence/item04/attempt2_lazy_ctor_crash.FDNYCore.log`. The new crashlog (below) located it without a debugger.
- **Interception (`src/console.cpp`).** MinHook on `ScriptEngine::ExecLine` 0x00470F10, group `console`. It's thiscall, takes the game `String` by value, and the callee releases it; its only caller is the console's Enter handler, after the game has already echoed `--> line` and before it adds history.
  - Our lines (`cmd::Dispatch`) are handled and the handle released the way ExecLine does: decrement, then `String::FreeRep` 0x004011C0 at <= 0. The game never sees them.
  - Everything else is passed to the original untouched, so game verbs (`listmaps`, `loadmap`, ...) keep working.
  - A mistyped `fd_*` is answered "unknown command" and never reaches the game.
- **Output.** `console::Print` builds a game String with 0x00402790 and calls `Console::AddLine` 0x004286E0 on `[MainWindow+0x70]`. This is `Console::Print` 0x00428930 minus its ImADeveloper gate. Every line is mirrored to FDNYCore.log as `console> ...` for tests.
- **Commands / vars (`src/cmd.cpp`).**
  - `echo`, `cvarlist`, `cmdlist`, `exec`.
  - `fd_version` (read-only) and `fd_hooks_frame` (toggles the frame hook group).
- **`exec` vs the game's `exec` verb.** `exec <name>` runs `<name>` / `<name>.cfg` from the game folder if that file exists. Otherwise the line goes to the game's own `exec` (Data\Scripts / Data\Maps).
  - Lines in our cfg that aren't ours are queued and handed to the game **one per frame**, because its script engine keeps a single pending line (`+0x78`).
- **Keystrokes don't reach the game while the console is open.** Live: W held 1.5 s with the console open → player moved **0.0 u**; console closed → 258 u. That covers both input paths:
  - The WM key path is blocked natively by `[con+0x49C] != 0`.
  - The DirectInput path is also blocked natively. This is presumably `Console::SetState`'s `[[0x503A78]+0x330]` flag; the live result shows the effect.
- **Crash reporter (`src/crashlog.cpp`).** A vectored exception handler logs AV/illegal/div0/stack-overflow first-chance exceptions with module+offset, registers, and the code addresses on the stack (≤ 8 reports per run). It is log-only and never changes the game's handling.
- **Address table:** +11 entries, 35 total.
  - Code entries (signature = first 16 bytes): `con.exec_line`, `con.add_line`, `str.from_cstr`, `str.free_rep`, `con.ctor`, `crt.operator_new`, `patch.console_gate`.
  - Data entries: `str.empty_rep`, `script.engine`, `mainwindow.console` (`[g_mainWindow 0x00503A70]+0x70`).
- **Game cfg restored** to the owner's original (`ImADeveloper = false`). The whole suite now runs in the player configuration. `console_basic` stages that cfg itself through `pre_files`, which now backs up and restores a replaced game file.
- **Red first:** `console_basic` against the item-3 core failed for the right reasons (no console, no output, W moved the player 344 u).

## Item 5: variables, config, binds (done)

- **Built (0.5.0):**
  - Archived cvars (`archive` flag) and binds are persisted to `FDNYCore.cfg` in the game folder, rewritten on every change (the game can be killed at any time); `fd_writeconfig` forces a write.
  - At startup, on the game thread once the console exists: `FDNYCore.cfg` runs first (no rewrite while loading), then `autoexec.cfg`. First archived var: `fd_log_commands` (default 1).
  - `bind`/`unbind`/`unbindall`/`bindlist` (`src/binds.cpp`), per the feasibility notes:
    - own key-name table (VK codes);
    - edge-triggered polling with `GetAsyncKeyState` on frame_begin, only while the game window is foreground and the console is closed;
    - backtick reserved; binding a game key warns but doesn't swallow;
    - bound lines go through `cmd::Dispatch`, or to the game one per frame.
- **Live so far:**
  - `autoexec` PASS (autoexec ran at startup; its F6 bind fired once).
  - `vars_binds_persist`: everything up to the restart passed. After the restart, `fd_log_commands 0` was in effect (no `cmd:` lines), F5 still fired from the persisted bind, and `FDNYCore.cfg` loaded. Then the test opened the console **at the main menu**, and the game raised FATAL ERROR "Console font texture not loaded".
- **Native game bug, confirmed:** the unmodified game (no core, `ImADeveloper = true`) raises the same fatal error when the console is opened at the main menu (`runs/...-explore_menu_console_native`). The developers evidently only used the console in a level. FDNYCore exposes the console to every player, so it must make the menu safe.
- **Fix (font guard, `console.cpp` GuardFont).**
  - **Why it happens** (Haiku Ghidra pass): the font is registered only during world load (`ROCRender::LoadWorldGeometry` 0x00495210 → `Console::LoadFont` 0x004275A0, its only caller). `Console::Render` checks it via the texture lookup 0x00493D00 (ECX = `[g_everGame]+4`, id = `[console+0x108]`) **only while the console is open**.
  - **The guard:** Render runs after `geEngine_BeginFrame` inside `RenderFrame`, so the core's frame-begin hook, if the console is open and the same lookup says the font isn't registered:
    - closes it again (`[con+0x49C] = 0`);
    - clears the game's input-block byte `[[g_everGame]+0]+0x330`, which the open path set (GameControls object, ctor 0x004395C0);
    - prints "console: not available until a level has loaded".
  - **Rejected alternative:** calling LoadFont at the menu, because the device may be null there (placeholder texture → later crash risk), and registrations look per-level.
  - **Cost:** no console at the menu, only in a level.
- New address-table entries: `gfx.find_texture`, `everGame.render`, `console.font_id`, `controls.block_input` (39 total).
- **Tests:**
  - `console_menu_safe` (new): backtick and Tab at the menu → state stays 0, input not blocked, refusal printed, menu still works; then in a level the console opens and echoes.
  - `vars_binds_persist`: now checks the refusal at the menu after the restart, then enters Training through the new `run_setup` step to verify the restored values in the console. This adds coverage; no check was dropped.
  - Both PASS.
- **Harness fixes:** `restart` step added (kill + relaunch inside one test); `Game.launch` now clears the stale HWND, which otherwise read as "hung". Also added `assert_file`, `assert_log_count`, `pre_files` with `src: null` (file must be absent, restored after), and F1-F12 scancodes.

## Item 6: fov module (done)

- **Original FOV fix provenance** (Haiku web pass plus my own clone):
  - It's by **AlphaYellow**, not Lyall: PCGamingWiki community file 3287, v1.0. Same toolchain as AlphaYellow's other fixes (inipp, spdlog, safetyhook, Ultimate ASI Loader).
  - AlphaYellow's MIT repo `github.com/alphayellow1/AlphaYellowWidescreenFixes` (shallow clone, 1,952 files) has **no FDNY / Line of Duty fix**, so the source isn't public there. Parity comes from the binary: disassembly of `geCamera_SetAttributes` and of the fix's hook (Haiku, capstone).
  - PCGamingWiki describes the game as Vert- and the fix as uncropped Hor+, with FOV-factor controls.
- **Engine facts** (capstone disassembly of Genesis.dll; Haiku plus my own read):
  - `@geCamera_SetAttributes@12` is fastcall: ECX = geCamera*, EDX = geRect*, Fov on the stack; `ret 4`.
  - It stores Fov raw at `[cam+0]`, Width/Height at +4/+8, and the screen scales `[cam+0x1C] = W/Fov`, `[cam+0x20] = H/Fov`. So **horizontal FOV = 2·atan(Fov/2)**.
  - The exe passes Fov = tan(deg·π/360) from its camera controller (`Camera::SetFov` 0x00491D20).
  - The old fix mid-hooks Genesis+0x1383 (`mov eax,[esp+0x10]`, the Fov load; its pattern is `8B 44 24 10 D8 05 ?? ?? ?? ?? 89 01`).
- **Old fix behaviour, measured live** with an fov-module hook logging Fov in vs stored:
  - Without the fix, stored = the game's value. It changes over loading (2.0 → 1.732 → 2.0), and in Training it is **1.428148 = 71.1° horizontal** (Vert-).
  - With the fix, stored is a **constant**, whatever the game passed: **1.43 × (W/H)/(4/3) × FOVFactor**, with W/H from *its ini*. Data points: 1920×1080 ini, factor 1.4 → 2.669333; 640×480, factor 1.0 → 1.43; owner's 640×480, factor 1.4 → **2.002 = 90.0°**.
  - The installed ini is the stale 1920×1080 one, so the owner's game currently runs at 2.669 = 106°. The owner's reference ini (640×480) is the parity target.
- **fov module (`src/fov.cpp`).** MinHook on `@geCamera_SetAttributes@12` (function entry; no overlap with the old fix's +0x33 mid-hook), group `fov`.
  - **`fd_fov`** (archived, default `0`):
    - `0` = classic, exactly the old fix: 1.43 × aspect/(4/3) × `fd_fov_factor`, with the aspect from the **real** render rect instead of an ini.
    - `N` = N° horizontal at 4:3, Hor+ for wider screens: 2·tan(N·π/360) × aspect/(4/3).
    - `-1` = leave the game's FOV untouched.
  - **`fd_fov_factor`** (archived, default 1.4).
  - **`fd_fovinfo`** shows the game and camera values.
  - Changes apply at once: re-call SetAttributes on the live camera with the game's last rect/FOV.
  - No-CRT math: `fptan` inline asm, plus a small float parser/formatter.
- **Camera address:**
  - `camera.fov` = `[[[[g_everGame 0x00503A78]+0x288]+0x8]+0x5C]+0` (f32). The chain to the camera controller came from a live **pointer scan** (`harness/pointer_scan.py`, from the static roots to the geCamera* the hook logged). Controller+0x5C = geCamera* is Ghidra-confirmed (0x00491BE0).
  - Dead end, documented: Haiku's static chain via `GameSession+0x64` is actually the **entity list** (dozens of elements, `+0x18` = 4/8); the player chain `[[g_everGame]+0x30]+0x7C` is not it either.
  - Bug found and fixed: Reapply first used a camera pointer cached in the detour and cleared on level_load, but level_load fires *after* the camera is configured, so changes never applied. Reapply now resolves the live camera through `camera.fov`.
- **Tests (live):**
  - `fov_parity_reference`: the **original fix** with the owner's ini, core told to leave the FOV alone (`fd_fov -1` via FDNYCore.cfg), gives camera.fov **2.001999855**.
  - `fov_sets_camera`: original fix removed. Default **2.001999855** (parity); `fd_fov -1` → 1.428148 (game stock); `fd_fov 90` → 2.0; `fd_fov 120` → 3.464102; `fd_fov 0` → 2.002, and FDNYCore.cfg holds no fd_fov line again (default).
  - Screenshots: 71° stock vs 120° shows a clearly wider view.
  - Red run against the item-5 core: no fov module, so the assertions fail.
- **Both fixes active at once:** the old fix's mid-hook runs inside the original, so its value wins and the owner's current behaviour is unchanged. The old .asi stays installed until the owner retires it; the profiles item (12) should decide what ships.

## Assumptions / workarounds

- **"Behaves the same" (item 1c) definition**, chosen by the agent at the owner's request. These are LOOP.md's 7 criteria, with two calibrations:
  - **(4)** Autosplitter final addresses are heap pointers that move run to run even without the core. So the check requires all 12 chains to resolve in every run, plus an exact match only where the two baselines agree with each other.
  - **(6)** Lap-position tolerance = max(32 u, 2× the baseline-pair spread). Scripted wall-clock input isn't frame-exact; the observed core-vs-baseline distance was 0.6-5.8 u.
- **Saves:** backed up to `core/backup/Saves` after several harness runs had already started the game, and the game rewrites `player*.rb`. The pristine originals are in the repo-root `FDNYSP1.1_0e4c1e9f.zip`. Harness runs only enter Training on the "Hotrod Stein" slot.
- **Game cfg history:**
  - Items 1-3 ran with `ImADeveloper = true` (the console needed it then). That covers the baseline_route base_a/base_b no-core runs used as the equivalence envelope.
  - From item 4 on, the cfg is the owner's original (`ImADeveloper = false`) and the core provides the console.
  - The item-4 Release core with the flag off is still inside the envelope (fps 55.6 vs 55.6). A future re-baseline in player config is cheap if a check ever becomes ambiguous.

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
- 2026-10-21 bind feasibility (Haiku): feasible with constraints; recorded above.
- 2026-10-21 it3: **item 2 DONE**. FDNYCore 0.2.0 has the signature-verified address table (23/23 OK live), version selection by hash, the module registry plus `levelwatch`, and a Debug override for failure-path tests.
  - Tests `addr_verified` / `addr_unknown_exe` / `addr_bad_signature` PASS.
  - Suite 5/5 PASS.
  - The Release 0.2.0 core is still EQUIVALENT to the item-1 baselines (fps 58.8 vs 55.6, laps within 5 u).
  - Evidence in `core/evidence/item02/`.
- 2026-10-21 it4: **item 3 DONE**. FDNYCore 0.3.0 adds the MinHook hook manager, by-name Genesis frame hooks (exactly once per frame vs an independent clock), frame_end + level_load events, and the Debug test pipe.
  - Red run first, then green; suite 7/7 PASS; Release EQUIVALENT.
  - Evidence in `core/evidence/item03/`.
  - Next: item 4 (console on the game's console; hook `ScriptEngine::ExecLine` 0x00470F10).
- 2026-10-21 it5: **item 4 DONE**. FDNYCore 0.4.0 gives players the game's own console without ImADeveloper (1 JZ NOPped at DllMain).
  - `fd_`/`echo`/`cvarlist`/`cmdlist`/`exec` are intercepted at ExecLine; output goes through `Console::AddLine`. Input is blocked while the console is open (0.0 u vs 258 u). Crashlog VEH added.
  - The first approach (calling the ctor at the first frame) crashed; it's diagnosed and documented.
  - Red then green; suite 8/8 PASS in player config; Release EQUIVALENT (fps 55.6 vs 55.6). Game cfg restored to `ImADeveloper = false`.
  - Evidence in `core/evidence/item04/`.
- 2026-10-21 it6: **item 5 DONE**. FDNYCore 0.5.0:
  - Archived vars and binds persist to `FDNYCore.cfg`; `autoexec.cfg` runs at startup; `bind`/`unbind`/`unbindall`/`bindlist`.
  - Fixed a native game crash: console opened before a level → FATAL "Console font texture not loaded". The core now refuses to open it until the font exists.
  - Red runs first; suite 11/11 PASS; Release EQUIVALENT (fps 55.6 vs 55.6).
  - Evidence in `core/evidence/item05/`.
- 2026-10-21 it7: **item 6 DONE**. FDNYCore 0.6.0 adds the fov module.
  - The old fix's behaviour was measured live: a constant 1.43·aspect/(4/3)·factor, which is 2.002 = 90° with the owner's ini.
  - FDNYCore's default reproduces it to the float (2.001999855). `fd_fov N` sets 2·tan(N/2) Hor+; `-1` gives the game default.
  - `camera.fov` chain found by pointer scan.
  - Red then green; suite 13/13 PASS; Release EQUIVALENT.
  - Evidence in `core/evidence/item06/`.
