# FDNYCore build loop (fdny-bxt)

Self-paced `/loop` spec. Each wakeup runs **one iteration** of "The loop"
below, then re-arms `ScheduleWakeup`. Branch: `fdny-bxt`. Project root:
`C:\fdny-decomp\core\`. State lives in `core/PROGRESS.md` + `git log`, not
in conversation memory — every iteration starts by re-reading them.

## Mission

Ship one BXT-style core, `FDNYCore.asi`, loaded by the existing Ultimate
ASI Loader (`dinput8.dll`), that absorbs five tools. Players do everything
from the in-game console.

| Tool | Form | Baseline for parity | Source? |
|---|---|---|---|
| FOV fix | `FDNYFirefighterAmericanHeroFOVFix.asi` + `.ini` | `reference/original_tools/` (ini = 640x480, FOVFactor 1.4 — the owner's copy; the game folder's 1920x1080 ini is stale) | Binary only. Its log format (spdlog, "Camera FOV Instruction: Address is Genesis.dll+1383") suggests one of Lyall's open-source fixes — search GitHub first, Ghidra second |
| Alt-tab fix | `alttabfix.asi` (no ini) | `reference/original_tools/alttabfix.asi` | Binary only. Imports: `FindWindowA` ("FDNY Firefighter"), `GetForegroundWindow`, `GetWindowRect`, `SetCursorPos`, `GetAsyncKeyState`, `CreateThread`, `Sleep`, `VirtualProtect`, QPC — i.e. a polling thread + a code patch. MSVC-built, links VCRUNTIME140 |
| Speedrun package | static victim spawns + rescue bot off | `asi/FDNYDeterministic.cpp` (+ `.ini`, README) | **Ours.** Never verified in-game — prove the baseline works before claiming parity |
| Player overlay | Prints player info | Fields: X/Y/Z position, player state, screen state, X/Y/Z velocity, XY speedometer | No source; the field list above IS the spec |
| Glitch overlay | `tools/collision_overlay/fdny_collision_overlay.py` | Same outputs as the Python overlay (WARP box status, stance/crouchbug, LAST WARP, HISTORY, log lines) on the same input | **Ours.** See its README.md, HOOK_SITES.md, mockup_v3.txt |

Installed exe/engine (the only supported version unless told otherwise):

- `FDNYFirefighter.exe` SHA-256 `4B3B4DC1CC0B05BCB42C8E27AC6F6CA72FE5F7064CA8122FDA7D41E7F6D2FC3E`
- `Genesis.dll` SHA-256 `5014BB879706BFB5AE6CF9CE1DE63DC3DEEBDCC5B8E1B41F247B284A336C4643` (loads at 0x10000000)

## Hard constraints

- **No player runtime deps.** Shipped = game + ASI loader + `FDNYCore.asi` (+ its cfg). No Python, .NET, companion exe, or VC++ redist — freestanding like `asi/FDNYDeterministic` (kernel32/user32 only, plus whatever the game already loads).
- **Console-first.** Every feature is an `fd_` command or variable, saved to config, runnable from `autoexec.cfg`.
- **Parity before features.** Each port matches its baseline before getting anything new.
- **x86 only.** Everything 32-bit.
- **Hook Genesis.dll exports by name** (GetProcAddress + MinHook); never proxy or modify the DLL.
- **Never modify the exe or Genesis.dll on disk.** In-memory patches at runtime are the point and are allowed. Back up any game-folder file you replace (first time only, to `core/backup/`).
- **Piggyback first.** Before building any subsystem, look for an existing fixture in the game/engine and reuse it. In particular the game **already has a usable in-game console** — leverage it (register `fd_` commands into it / extend it) instead of writing a new one, unless research proves it can't carry the load. Record what you found either way.

## Toolchain

- **Zig 0.16** (already in `environment/local/tools/zig*`, see `asi/build.ps1` for the proven freestanding flags). MSVC Build Tools only if Zig demonstrably can't do something needed — record the reason in PROGRESS.md before installing.
- **MinHook** vendored into `core/third_party/minhook/`, compiled by Zig, statically linked.
- **Genesis3D 1.1 SDK headers** for export signatures (open source) — verify each against the installed `Genesis.dll`'s export table; this build may differ from stock 1.1.
- **git**: `C:\fdny-decomp\environment\local\tools\git\cmd` — not on PATH; prepend per shell. Commit messages via `-F <file>`.
- VM-only test tools (never shipped): Python 3.14 (`environment/local/tools/python`, has pymem; add mss, Pillow, pydirectinput), ffmpeg (gdigrab), x32dbg, ProcDump. Install via the `get_<thing>.ps1`/`install_<thing>.ps1` convention + `environment/config.ps1`.
- **Rendering**: the game already renders here via dgVoodoo (D3D, 640x480, init verified 2026-08-27 in `D3DDrv.Log`). For testing, switch dgVoodoo to **windowed** (`dgVoodoo.conf` `FullScreenMode = false`; back up the conf first) so capture and input work without fullscreen focus games. Record the exact dgVoodoo settings used.

## Architecture

One .asi owns all plumbing; modules talk only to the core; only the core touches the game.

- **Address table** — one data file `core/addresses.toml` (or .json), keyed by exe+Genesis hash. Each entry: name, module (exe/Genesis), RVA, byte signature, Ghidra reference, one-line note. A build step **generates a C header** from it for the core (players never need the data file); the harness reads the same data file. No raw addresses anywhere in module code.
- **State snapshot** — typed per-frame struct (player, victims, fires, rescue bot, timer); modules read this, never memory.
- **Hook manager** — MinHook + byte patches on exe and Genesis exports; owns every hook; runtime toggle; events (frame end, level load, victim spawn).
- **Console + variables** — built on the game's existing console (see Piggyback); `fd_` vars, `exec`, `autoexec.cfg`, `bind`.
- **In-game drawing** — console/HUDs/watermark drawn through Genesis' own calls so they're in-frame on any driver.
- **Profiles** — `run` / `practice` var sets + on-screen watermark listing active mods.
- **Test pipe** — Debug-only named pipe for harness setup/readiness, `#ifdef`'d out of Release (verify by checking the Release binary's strings/imports).

## Orchestration (who does what)

The primary session (Opus) is the orchestrator: it picks the item, designs, adjudicates conflicting evidence, decides pass/fail, writes code that needs judgment, edits PROGRESS.md, and commits.

Delegate every small, well-defined task to a subagent via the `Agent` tool with **`model: "haiku"`** (Haiku 5.5). Never use `fork` for these — forks inherit Opus. Typical Haiku tasks:
- One Ghidra MCP lookup (decompile a function, list xrefs, find a string, read bytes for a signature) — return a compact table.
- Run a build and report errors/warnings verbatim.
- Run one harness test and summarise `result.json` + log errors.
- Scan a file/binary for strings, imports, exports; parse the `.asl` autosplitters for their memory paths.
- Mechanical code edits fully specified by the orchestrator.

Rules carried over from this project's decomp loop (memory: loop-owns-ghidra-writes, ghidra-mcp-gotchas):
- The primary session **never calls `mcp__ghidrassistmcp__*` itself**. All Ghidra reads go through dispatched subagents.
- Ghidra writes (renames/comments) only via one commit subagent with an exact list; get-before-set on comments; stop on any target whose current name isn't the expected default.
- `rename_symbol` wants lowercase hex `FUN_xxxxxxxx`.
- A subagent killed by a 429 is resumed with `SendMessage`, not re-dispatched.
- Dispatch independent subagents in parallel; their completions wake the loop.

## The loop (one iteration per wakeup)

1. **Orient.** Read `core/PROGRESS.md` and `git log --oneline -15`. If the last iteration ended mid-item (PROGRESS says so), re-run the full regression suite first.
2. **Pick.** First backlog item whose deps are done and that isn't blocked. Split it if it won't fit one iteration; record the split.
3. **Research.** Check for existing fixtures first (Piggyback). Ghidra lookups via Haiku subagents; read original tool source/binary when porting. Record every address in the address table with Ghidra location, signature and note.
4. **Test first.** Add `core/tests/<name>.yaml` (format below). Run it; confirm it fails for the right reason.
5. **Implement** the smallest change that can pass.
6. **Build** Debug and Release x86. Zero warnings in touched code.
7. **Deploy** to the game folder (back up replaced files the first time).
8. **Run live** through the harness. Wait for the core's ready line, then drive the test.
9. **Verify.** Pass only if every assertion holds, screenshots show the expected state, no crash, log has no errors.
10. **On failure:** read core log, video, crash dump; x32dbg if needed; back to 3 or 5. After 5 failed attempts on one item → mark it blocked with findings, move on.
11. **On pass:** run the full regression suite live. Commit change + test + evidence (log excerpt, key screenshots, video path) — **one backlog item per commit** — and update PROGRESS.md.
12. **Re-arm** `ScheduleWakeup`: ~120-300 s if continuing straight into the next step; 1200-1800 s fallback when waiting on subagents (their completion wakes the loop sooner). Pass this loop's prompt back verbatim.

## Backlog

| # | Item | Needs | Done when |
|---|---|---|---|
| 1 | **Core skeleton + harness + foundation research** | — | (a) Game launches with the core loaded; log has a version line with exe + Genesis hashes and a ready line. (b) Harness launches/resets/captures/reports per "Harness". (c) **Baseline equivalence** — see definition below — passes with vs. without the core. (d) **Research notes** in PROGRESS.md answering R1-R5 below (findings + addresses into the address table). |
| 2 | Address table + version check | 1 | Every address checked against its signature at startup; an unknown exe/Genesis logs a warning and disables modules instead of crashing (test with a patched copy of the table, not a modified exe). |
| 3 | Hook manager + events | 2 | Hooks on Genesis frame exports fire exactly once per frame (count vs. harness frame counter); toggling hooks at runtime 50x doesn't crash. |
| 4 | Console (on the game's existing console) | 3 | Console opens in-game (screenshot); `fd_` commands registered into it; `echo`, `cvarlist`, `exec` work; keystrokes don't reach the game while it's open (memory check on player position/input state). |
| 5 | Variables, config, binds | 4 | `fd_` vars persist across restart; `autoexec.cfg` runs at startup; `bind` maps keys to commands. |
| 6 | fov module | 5 | With the old FOV fix disabled: default `fd_fov` reproduces the old fix's camera value at 640x480 / factor 1.4 (memory, same units the fix writes at Genesis.dll+1383); `fd_fov 90` sets the expected value and the screenshot shows the wider view. |
| 7 | window module | 5 | With `alttabfix.asi` disabled: 5 scripted alt-tab cycles leave the game rendering (non-black screenshot), no crash, and cursor/focus behaviour matches the old fix (compare against a run with the old fix). |
| 8 | speedrun module | 5 | First prove `FDNYDeterministic.asi` itself works live (baseline). Then with it removed: `fd_static_spawns 1` → victim positions identical across 3 reloads of a level and equal to the baseline's; `fd_rescuebot 0` → bot disabled in memory, absent on screen. |
| 9 | Game state snapshot | 2 | Typed per-frame snapshot (player, victims, fires, rescue bot, timer) matches harness direct reads. |
| 10 | hud_player | 9 | In-game HUD shows X/Y/Z position, player state, screen state, X/Y/Z velocity, XY speed; each equals memory within one frame. |
| 11 | hud_glitch | 9 | Port of the Python collision overlay. Parity = same outputs (WARP status line, stance incl. "Bugged" crouchbug, target row, LAST WARP, HISTORY, log lines) as the Python overlay on the same scripted route. Run the two **separately, never both hooked at once** (same hook sites), and diff their logs. **Do not attempt to trigger/validate an actual OOB warp** — out of scope for now. |
| 12 | Profiles + watermark | 8, 10 | `fd_profile run` / `practice` switch var sets; watermark lists active mods; practice-only vars refused in `run`. |

Lua scripting is out of scope until requested.

### Item 1 research questions (answer before building on them)

- **R1 In-frame drawing.** Which Genesis exports/calls draw text and 2D quads inside the frame (e.g. `geEngine_Printf`, `geEngine_DrawBitmap`, whatever this build exports)? Does the game already draw debug/HUD text we can reuse?
- **R2 Input.** How does the game read keyboard/mouse (DirectInput 8 via the real dinput8 behind the ASI loader? window messages? `GetAsyncKeyState`)? Where would the core swallow input while the console is open?
- **R3 Existing console.** How is the game's console opened (key, `ImADeveloper` / `EnableDebugLogging` in `FDNYFireFighter.cfg`?), where is its command table, and how do we register commands into it? Same for the script verb system (`setrescuebot`, `runrescuebot`).
- **R4 Frame + level-load events.** Which Genesis exports bracket a frame (`geEngine_BeginFrame`/`EndFrame`?) and where does a level load start/finish?
- **R5 Autosplitter safety.** Parse `FDNY SP 1.1/Speedrunning/Autosplitters/*.asl` for every module + offset / pointer path they read. Add them to the harness as a check (see baseline equivalence) so the core never moves or breaks them.

### Baseline equivalence (item 1's "behaves the same")

Run the same scripted 2-minute route twice — without the core, then with it (Release build) — and pass only if all hold:
1. No crash, hang >10 s, or error dialog in either run.
2. Same level loaded and reached the same checkpoint of the script (by memory: level name/id).
3. Module bases of `FDNYFirefighter.exe` and `Genesis.dll` identical.
4. Every autosplitter memory path from R5 resolves to the same address in both runs and its value behaves the same (e.g. timer advances, level id equal at the same script step).
5. Median frame time within ±10% of the no-core run (measured via harness frame counter / QPC).
6. Player position at the end of each scripted input segment within a small tolerance (record the tolerance and why — the game isn't fully deterministic).
7. Core log has zero ERROR lines; the old tools' logs are unchanged.

## Harness (VM only, never ships) — `core/harness/`

- **Launch/reset:** start the game with the core loaded **via `schtasks /it`** (interactive session — PowerShell Direct/WinRM sessions hang Swing/AWT and D3D GUIs, see CLAUDE.md); wait for the ready line; kill + relaunch cleanly between tests. Process exit, hang >10 s, or a Windows error dialog = crash.
- **Never steal focus.** Losing focus fires `WM_ACTIVATEAPP` → GameClock pause. The harness must not create focusable windows over the game; use windowed dgVoodoo + capture by window rect.
- **Drive:** scan-code keyboard/mouse (pydirectinput / SendInput). The Debug-only pipe may run setup commands and report readiness. Use `FDNYFireFighter.cfg` `SkipMenus` (back up first) if it shortens setup.
- **Observe:** read memory by address-table name only (same data file as the core).
- **Capture:** screenshots at checkpoints; ffmpeg gdigrab video of every run; OBS optional.
- **Report:** `runs/<date>-<test>/result.json` (each assertion pass/fail) + log + screenshots + video. `runs/` is gitignored except evidence explicitly committed with an item.

Test file format:

```yaml
name: fov_sets_camera
setup: load_level_1
record: true
steps:
  - console: "fd_fov 90"
  - wait_frames: 30
  - assert_mem: {name: camera.fov, equals: 90, tolerance: 0.01}
  - screenshot: fov_90
expect:
  no_crash: true
  log_errors: 0
```

The regression suite = every file in `core/tests/`, run live in sequence.

## Rules

- No raw addresses outside the address table.
- Disable an old tool while testing its replacement; compare for parity before retiring it.
- Never weaken or delete a test to make it pass. Tolerance changes are explained in the commit.
- One backlog item per commit, with its test and evidence.
- Every assumption and workaround goes into PROGRESS.md.
- Keep project docs current: domain findings → `RESEARCH.md` / `research.db` (per the decomp workflow); infra/tooling → `plan.md`.

## Stop and report when

- The backlog is done.
- Three items in a row are blocked.
- The game can't render in the VM with any driver option (dgVoodoo windowed/fullscreen, Genesis software driver).
- A choice is irreversible and could reasonably go either way. Reversible choices: take the sensible default, log it, continue.

On stop: `ScheduleWakeup` with `stop: true`, then a short visible summary.

## Decisions already made (owner, 2026-10-21)

- Toolchain: Zig; MSVC only if needed.
- Console: use the game's existing in-game console; always check for existing fixtures before adding new.
- Address table: data file + generated header.
- Supported version: the installed exe/Genesis hashes above only.
- OOB warp: not reproduced/validated; hud_glitch parity is on the overlay's other outputs.
- Player overlay spec = the field list in the tool table.
- dgVoodoo windowed mode for testing is fine.

Open (take the default until answered): console toggle key — default = whatever the game's existing console uses.
