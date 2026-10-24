# Demos / ghosts spike: status and handoff

**Paused 2026-10-23, resumed 2026-10-24 (runs 8-13), paused again 2026-10-24.** Last commit of this work:
`2a7f5bd` "Spike: demos (deterministic replay) and ghosts", plus this file and the
`core/probes/re_*.py` helpers. Nothing in `core/src` changed: this is a spike (D22), and
FDNYCore's behaviour and `dist/` are untouched.

Read first: `README.md` in this folder (verdicts, prior art, the probe runs, the ghost
design). This file is the "how to pick it up" layer.

## 1. The ask

> Research demos (Quake / GoldSrc) for deterministic in-game replays and/or ghosts. Genesis
> may have shipped demo features; unknown whether FDNY uses them. Hourglass,
> hourglass-resurrection and libTAS may shed light. If demos mean a significant rewrite,
> evaluate ghosts: player position recorded, saved, overlaid as a transparent model.

## 2. Where it stands

| Track | State | Verdict |
|---|---|---|
| Does Genesis / FDNY have demos? | **answered**: no | no code or strings in either binary; FDNY doesn't import Genesis's netcode |
| Demos (deterministic input replay) | **researched + probed, one source left** | **MODERATE** (was MODERATE-HARD, open-ended). With time + input + RNG fed, presentation RNG split, a fixed predecessor and a zero-filled heap, a 58 s bank replay was identical on every reliable measure in 2 of the last 3 playbacks; the third diverged at frame 106 in a spawned fire's list position (see 7B) |
| Ghosts | **designed, not built** | EASY as a translucent box, MODERATE as the `firefighter.act` model. Needs no determinism. **Recommended next build.** |

## 3. What was researched

**External (web):**
- Quake / GoldSrc `.dem`: recordings of the server-to-client message stream (a state
  stream, timestamped blocks); playback needs no determinism.
- Hourglass (TASEmulators/hourglass-win32: `wintasee/` hook DLL, `dettime.cpp`,
  `phasedetection.cpp`) and libTAS (docs read in full): virtual time advancing one frame per
  present / swap, per-frame input injection, faked entropy, savestates as memory dumps;
  threads are the weak spot. Hourglass-Resurrection's GitHub URL was a 404.
- Genesis3D 1.1 source (RealityFactory/Genesis3D): `engine.c`, `CSNetMgr.c`, `Netplay.c`
  read; DirectPlay client/server, no demo recorder; no GTest in that repo. **Not done:**
  `World/`, `Actor/`, `Entities/`, `Support/` folders weren't read, and no GTest mirror was found.
  (The static scan of FDNY's own Genesis.dll is the decisive check anyway.)

**Internal:**
- `static_scan.txt`: keyword strings, time / input imports with call sites, Genesis actor exports.
- `rng_sites.txt`: every RNG draw site and caller.
- research.db (symbols for GameClock, DirectInputProvider, MT19937, Rand_*, FireFx,
  FireRescueEnt, RenderInstance, ParticleSystem) and RESEARCH.md (movement, framerate audit,
  victims, save-state verdicts).
- **Ghidra was not used**: no project was open on the VM, so the GhidrAssist MCP had no
  program (the read-only subagent reported BLOCKED). Everything was done from the binaries
  (pefile + capstone) and research.db.

## 4. Key facts found (addresses: retail 1.1)

| Fact | Address / detail |
|---|---|
| Frame order | `GameSession::RunFrame` 0x0042AEA0: pending level change -> `GameSession::LoadLevel` 0x0042AB00 (thiscall, 1 stack arg, `RET 4`) -> `GameClock::Tick` 0x0042C080 (ECX = 0x502AB8, no args) -> `GameControls` PollFrame (input) -> `TickSubsystems` 0x00429520 -> `RenderFrame` |
| Clock outputs | dt float at 0x502AB8+0x4C (0x00502B04), `g_gameTimeMs` int at +0x44 (0x00502AFC). Load: 5 frames of game.state 2/3 before state 1 on bank |
| Input | both drains call `IDirectInputDevice8::GetDeviceData` (vtable +0x28, cbObjectData 20, flags 0), mouse then keyboard, once each per frame; both devices share the dinput8 vtable |
| RNG | MT19937 `g_mtState` 0x00501A28 (624 words), words-left 0x004FF370, next-ptr 0x005023EC. `Rand_Float01` 0x00404AC0 (cdecl, no args, float in st0) **inlines the draw**; `MT19937_NextU32` 0x00404A70 only serves `Rand_Below` 0x00404B30. ~860 draws per frame on bank |
| RNG consumers | presentation: `ParticleSystem::Emit`, `WaterImpactFx::Draw`, `HoseGfx::RebuildGeometry`, `AshSim::*`, `Fire::EmitEmbers`, `FireFx::Init`, `MiscEmitterFXImpl::Update`, `Camera::UpdateShake`, `ROCSoundSystem::Play`, `TChatterSet::PlayRandom`, `StreamTube::BuildMesh`, `PlayerRescueEnt::DoFootstep`, menu, roster names. Gameplay: `FireRescueEnt::TrySpawnSpreadFire`, `Hotspot::AdvanceActiveFire`, victim roam / idle / spawn, `RescueBot::*` |
| Fire strength (2026-10-24) | `FireRescueEnt` vtbl 0x004E1780 slot 18 = 0x004483A0 (thiscall, 1 arg = the player): strength = max(abs(dx), abs(dy), abs(dz)) from the player to the fire (+0x7C..+0x84), then `SetFireStrength` 0x00448320 -> `TrySpawnSpreadFire`. Gated on `fire+0xB8` in 0..3, which is **never initialised** (only `inc` at 0x00448533): an original bug, the source of load-to-load fire variation. Spread delay scales with the small-fire counter `DAT_00502B90` (0 at every load) |
| Script timing | `ScriptActor::Tick` 0x00470740: a `wait` (state 1) ends when `g_gameTimeMs >= actor+0x48`; one queued command per tick. Replays identically |
| Fire spread gate | `TrySpawnSpreadFire` 0x00447A90: `FireFx::GetExtents(fire+0x12C).x > const` **and** `g_gameTimeMs >= fire+0x124`, then a roll vs strength `+0x128`. Extents come from size keyframes (`FireFx::UpdateSizeKeyframes` by `g_gameTimeMs`), scheduled by `FUN_00447920` (from `FireRescueEnt::HandleScriptCommand`, i.e. the level script) and by the spread stepper |
| Entity list | `[0x00502AAC]+0x64` head node; node +0 entity, +4 next; entity +0x18 kind (2 victim, 6 fire), +0x4C position; victim +0x120 state |
| Timer offsets | from `[g_everGame]+0x30`: `+0x343C` = mission time left (bit-exact in replays); `+0x32FC` = constant ASCII "[Cou". An older research note calls +0x32FC the timer, 0x140 lower (maybe a different base). Unresolved |
| Ghost assets | `Data\Acts\firefighter.act`, `Fireman.act`; Genesis exports `geActor_DefCreateFromFile`, `geActor_Create`, `geWorld_AddActor`, `geActor_SetPose`, `geActor_SetAlpha`, `geActor_SetBlendingType` (the last two never imported by the game). The game's own actors: `RenderInstance` (0x00491EA0..) -> `geActor_SetPose` / `geActor_Render` |

## 5. Code written

All standalone (not part of FDNYCore). Python is `environment\local\tools\python\python.exe`, run with `-I`.

| File | What |
|---|---|
| `core/probes/demoprobe.cpp` | **the probe**, built to `core/build/probes/zz_demoprobe.asi` by `core/probes/build_probes.ps1` (image base 0x3E800000). Details below |
| `core/tests/probes/probe_demo.yaml` | harness script that drives it (outside the suite; ~10 min) |
| `core/probes/demo_scan.py` | static scan: keyword strings + code refs, time / input imports + call sites, Genesis actor exports |
| `core/probes/demo_rngsites.py` | every code site touching the MT cursor, every call to NextU32 / Rand_* with callers |
| `core/probes/demo_compare.py` | diff two probe traces field by field (frames differing, first frame, max diff) |
| `core/probes/demo_entdiff.py` | diff two entity dumps per frame by kind |
| `core/probes/re_dis.py`, `re_callers.py`, `re_owners.py`, `re_dbq.py`, `re_sites.py` | small RE helpers used along the way (disassemble, find callers, name an address, query research.db, time-call sites) |

**The probe (`demoprobe.cpp`), as of v13:**
- **Hooks (MinHook):**
  - `GameClock::Tick`: the frame boundary. Keys, mode machine, time injection, sampling and compare all happen here.
  - `IDirectInputDevice8::GetDeviceData`: found by creating a throwaway mouse device on the first Tick. It records or replays each call.
  - `Rand_Float01` and `MT19937_NextU32`: count draws per call site, and route presentation call sites to a private xorshift generator (the "split").
  - `GameSession::LoadLevel`: saves the RNG (record) or restores it (playback), starts virtual game time, and starts the per-slot histograms.
  - v8-v10: `ScriptActor::ExecCommand`, `FUN_00447920` (size keyframes), `TrySpawnSpreadFire`, fire vtbl slot 18: the per-run timeline `zz_demoprobe_script_<run>.txt` (script commands, heat / spread lines with fire index and gate inputs).
  - v11-v12: **zero-filled heap**: `operator_new` and `ntdll!RtlAllocateHeap` / `RtlReAllocateHeap` with `HEAP_ZERO_MEMORY`, from game start.
  - v13: also feeds GameClock +0x50 / +0x54 on playback.
- **Keys (F-keys, so the harness's console typing can't trigger them):**
  - F5 arms record from the next level start; F7 stops recording.
  - F6 plays back with time + input + RNG.
  - F8 plays back on real dt.
  - F9 plays back without restoring the RNG.
- **Each run, from `LoadLevel`:** load frames get dt = 1/60 and elapsed = E0 + n x 1000/60. The recording or playback starts at the first game.state 1 frame. Recorded elapsed times are relative to E0.
- **Compared per frame (bit-exact):**
  - dt and RNG draw counts and callers;
  - player position, velocity, yaw / pitch and move state;
  - +0x32FC and +0x343C;
  - entity count, plus hashes of victims and of other entities.

  The first difference per category is logged. Two hashes are unreliable: the "entity positions" and "other entities (+0x70)" hashes include heap pointers for some entity kinds, so they always differ. Trust the player fields, `time_left`, the entity count, the victim hash and the RNG histograms.
- **Outputs, in the game folder:**
  - `zz_demoprobe.log`: events, first differences, the load-frame census, and the gameplay and cosmetic RNG histogram verdicts;
  - `zz_demoprobe_rec.txt` and `_play<N>.txt`: per-frame traces;
  - `zz_demoprobe_ents_<run>.txt`: every entity at frames 0, 60, 600, 1200 and 1800.
- **Limits:**
  - 20000 frames;
  - 8 MB of input;
  - 400 histogram slots of 96 sites;
  - the presentation list is 16 function ranges taken from research.db (function start to the next symbol), not reviewed in Ghidra.

**How to run:**
```
cd core
.\probes\build_probes.ps1
python -I harness\run_test.py tests\probes\probe_demo.yaml
python -I probes\demo_compare.py "..\FDNY SP 1.1\zz_demoprobe_rec.txt" "..\FDNY SP 1.1\zz_demoprobe_play1.txt"
```
The test needs the Debug FDNYCore (the harness default) for `fd_static_spawns 1`. It
precedes every run with `loadmap training` so all bank loads have the same predecessor. The
harness removes `zz_demoprobe.asi` afterwards; the logs stay in the game folder.

## 6. Results in one table

| Run | What changed | Player | Victims | Fires / RNG |
|---|---|---|---|---|
| 1 | time + input + RNG at first play frame | bit-exact | n/a | RNG cursor off from frame 3 |
| 2-4 | fixed load dt, RNG at LoadLevel, virtual load time | bit-exact | off from frame 0 | off |
| 5 | per-site histograms | bit-exact | off | first: `ParticleSystem::Emit` count at load frame 2 |
| 6 | RNG split (presentation -> private) | bit-exact | to frame 78 | first gameplay diff: fire spread, load frame 3 |
| 6b | + same predecessor (`loadmap training`) | bit-exact | **to frame 1517 (25 s)** | fire spread still |
| 7 | load-frame census | bit-exact | off from ~frame 6 | identical census at load frame 3, then 1 vs 3 spread attempts |

## 7. Handoff: where to pick up

### A. Ghosts (recommended next; independent of the demo work)

Build as an FDNYCore feature with the `new-feature` skill. Design in `README.md`, "Ghosts".
1. Feature `ghost` (practice-only). On game.state 3 -> 1, record every frame (or at 20 Hz):
   time since start (from `clock.elapsed_ms`), player pos / yaw / pitch / state (core
   `state` service).
2. Save `Ghosts/<map>.fdg` (last + best; best = completed run with the lowest time). The
   header holds the format version, core version and map. Load on level start.
3. Show G1: the ghost sample at the same time since start, interpolated, as a translucent
   player-sized box + facing marker via `gfx::SubmitWorldBoxes(LAYER_WORLD)`; cvars like
   `fd_ghost 0/1`, `fd_ghost_mode last|best`; an optional HUD delta line.
4. Then G2: a `gfx` block that owns one `geActor` from `firefighter.act`
   (`geActor_DefCreateFromFile` -> `geActor_Create` -> `geWorld_AddActor`; per frame
   `geActor_SetPose`; `geActor_SetAlpha` / `geActor_SetBlendingType`). First prove the alpha
   renders through the D3D driver + dgVoodoo with a probe; handle add / remove across level
   loads (see `RenderInstance::EnsureActor` 0x00492560 / `DestroyActor` 0x00492690 for the
   game's own pattern). Every address goes in `addresses.json` (R4).
5. Owner questions to ask first: what counts as "best"; the ghost per map or per
   mission (staged levels: oil rig A-D, subway A-B, boat A-B); box colour; is it allowed in
   `fd_profile run` (default: practice-only).

### B. Demos (research; pick up only if wanted)

**The exact next step (2026-10-24):** the last source. In run 13 play 1, at frame 106, a spawned
child fire is at a different position in the entity list (fire numbering shifts by one) while every
gameplay RNG draw count is identical, so which fire receives which random number changes from there.
- Hypothesis, unverified: entities take a free slot in the fixed 512-slot `EntityHandleTable`
  (0x0049B9E0), and which slots are free depends on earlier lifetimes that the simulation doesn't decide.
- Instrument `roc_SpawnEntity` 0x0049D4C0 / `EntityManager::FlushSpawnQueue` 0x0049C670: for each
  spawn, log the slot / list position, and which slots were free.
- Fix candidates: a deterministic slot pick, or a full handle-table reset at `LoadLevel`.
- Tools: `demo_firediff.py` (heat / spread lines by slot), `demo_compare.py`, `demo_scriptdiff.py`.

Done already (runs 8-13, `README.md` "Resumed"):
- script timing: replays;
- the small-fire counter: 0 at every load;
- fire strength: distance to the player;
- the uninitialised `fire+0xB8`: fixed by the zero-filled heap;
- GameClock +0x50 / +0x54: fed back on playback;
- the clock imports' thunks: no callers.

**Success criterion:** victim hash and entity count identical between the recording and every
playback for a whole level, several runs in a row (now: 2 of the last 3). Then rerun with a run that carries a victim and hoses a fire.

**After that, productising** (FDNYCore services, not a probe):
- a clock override service (extend `pause`, which already owns GameClock calls);
- a DirectInput service (`GetDeviceData`);
- an RNG service with the presentation / gameplay split, with the presentation list reviewed in Ghidra;
- a defined start: RNG and virtual time from `LoadLevel`, a fixed predecessor or reset, static spawns;
- a demo file format;
- the probe's per-frame compare, kept as a desync detector;
- handling of console / menus / alt-tab / `fd_pause` during a recording.

**Note:** the RNG split changes which numbers gameplay sees compared with the vanilla game,
so it is only acceptable inside a demo or practice context. Decide that with the owner
before any `fd_profile run` use.

### Known gaps / caveats

- The presentation call-site list is heuristic (research.db names + "next symbol" ends).
  Some functions may be mis-bounded or missing.
- The probe's stack walk attributes `Rand_FloatRange` draws to the first `call` return
  address found on the stack. It is good enough for histograms, not a proven unwinder.
- Measured on one map (bank) and one ~32 s input script, on the VM (dgVoodoo, windowed).
- The +0x32FC vs +0x343C timer-offset discrepancy with an older research note is unresolved.
- Genesis3D source folders beyond `engine.c` / netcode were not read.

## 8. Other work paused at the same time (from this session)

- fd_pause step 6 is still waiting for the owner. That covers the full suite, the
  docs/PROGRESS entry, VERSION 0.19.0, the `dist/` update and the owner report.
- `freecam` test: one intermittent failure (player y moved 0.9 units during the pause),
  passed on rerun. Not investigated (`core/evidence/key_sheet/`).
- Live split: the splitters still need a run in real LiveSplit on the owner's PC.
- Earlier open items:
  - choose the default branch (`main` vs `fdny-bxt`);
  - the Ghidra GZF script is waiting for approval;
  - delete the old `gitbackup` folder after the owner verifies it;
  - research.db corrections.
