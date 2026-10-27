# Spike: demos (deterministic replays) and ghosts, 2026-10-23

Owner ask: research Quake / GoldSrc-style demos for deterministic in-game replays, whether
Genesis3D shipped demo features FDNY could use, what Hourglass / libTAS teach. If demos
would mean a big rewrite, evaluate ghosts instead: the player's position recorded, saved,
and shown as a transparent model while playing a level.

## Verdict

| | Verdict | Why |
|---|---|---|
| Genesis / FDNY demo feature | **none exists** | no demo / record / playback / timedemo / journal strings or code in `FDNYFirefighter.exe` or `Genesis.dll`; the Genesis3D 1.1 source has DirectPlay netcode (`CSNetMgr.c`, `Netplay.c`) but no demo recorder, and FDNY imports neither |
| **Demos** (input replay, Hourglass-style, in-process) | **MODERATE** since 2026-10-24 (was MODERATE-HARD, open-ended); not a rewrite. See "Resumed 2026-10-24" at the end | four hook points, no engine change. The **player** replays bit-exact. The **world** (fires, roaming victims) still diverges from the first load frames: 3 sources of non-determinism found and controlled in this spike, at least 1 left (level-script / fire start-up timing) |
| **Ghosts** | **EASY (box) / MODERATE (firefighter model)**, no determinism needed | the player's position / view per frame is already read by the core's `state` service; translucent world boxes exist (`gfx::SubmitWorldBoxes`); the game ships `firefighter.act` and Genesis.dll exports `geActor_SetAlpha` / `geActor_SetBlendingType` (unused by the game) |

**Recommendation:** build ghosts (box first, model second) as an FDNYCore practice feature. Keep
full demos as research: the probe and its findings below are the starting point.

## Prior art (external research)

- **Quake / GoldSrc demos** are recordings of the server-to-client message stream (entity
  states, sounds, prints, a time stamp per block). Playback feeds the client the same
  messages: it is a **state** stream, not an input stream, and needs no simulation
  determinism. FDNY is not client/server, so the equivalent would be recording the world's
  state every frame (a "state demo", the ghost idea grown up).
  Sources: [hl1 demofile spec](https://cgit.sukimashita.com/hl-demofile-spec.git/plain/doc/hl1-demofile-spec.txt),
  [Valve DEM format](https://developer.valvesoftware.com/wiki/DEM_Format).
- **Hourglass / libTAS** make a native game deterministic from outside: hook the time
  sources (time advances exactly one frame length per frame, frames delimited by the
  present / swap call), feed recorded input per frame, fake the other entropy (RNG, PIDs,
  zeroed allocations), avoid threads; savestates = whole-process memory dumps. Movie files
  are per-frame input lists. Threads are the main unsolved problem for them.
  Sources: [libTAS docs](https://clementgallet.github.io/libTAS/guides/how/),
  [libTAS format](https://clementgallet.github.io/libTAS/guides/format/),
  [hourglass-win32 source](https://github.com/TASEmulators/hourglass-win32/tree/master/src/wintasee),
  [TASVideos libTAS](https://tasvideos.org/EmulatorResources/LibTAS).
  (Hourglass-Resurrection's repository URL returned 404.)
- **Genesis3D 1.1 source** ([RealityFactory/Genesis3D](https://github.com/RealityFactory/Genesis3D)):
  engine, actors, worlds, DirectPlay client/server netcode; no demo recorder found in the
  parts read (`engine.c`, `CSNetMgr.c`, `Netplay.c`). The sample game GTest is not in that
  repository.

## The game's inputs (static scan + research.db)

`static_scan.txt` (`probes/demo_scan.py`), `rng_sites.txt` (`probes/demo_rngsites.py`):

| Input | Where | Notes |
|---|---|---|
| Time | `GameClock::Tick` 0x0042C080, start of `GameSession::RunFrame`: publishes dt (+0x4C) and `g_gameTimeMs` (+0x44, 0x00502AFC) | the exe reads the clock only through `timeGetTime` in GameClock (+ the WinMain RNG seed, + one sound call); Genesis.dll's QPC reads are its FPS counter and `gePhysicsSystem` (FDNY imports no `gePhysics*`) |
| Input | DirectInput8 keyboard + mouse, **buffered**: `DirectInputProvider::DrainMouseBuffer` 0x00437AB0 / `DrainKeyboardBuffer` 0x00437C80 -> `IDirectInputDevice8::GetDeviceData` (vtable +0x28), from `GameControls::PollFrame` right after the clock tick | no joystick, no `GetDeviceState`; window messages only for the console and Esc |
| RNG | one MT19937 (`g_mtState` 0x00501A28, cursor 0x004FF370 / 0x005023EC), seeded once from `timeGetTime` | **`Rand_Float01` 0x00404AC0 inlines its own draw** (every `Rand_FloatRange` goes through it); `MT19937_NextU32` only serves `Rand_Below`. Consumers mix gameplay (fire spread, victim roam / idle, RescueBot) and presentation (particles, ash, embers, hose graphics, camera shake, sound, chatter, footsteps): ~860 draws per frame on bank |
| Threads | the exe creates none | (FMOD / DirectInput have their own) |

## The probe (`probes/demoprobe.cpp` -> `zz_demoprobe.asi`, `tests/game/probes/probe_demo.yaml`)

An in-process Hourglass: F5 records from the next level start (every frame's dt, every
`GetDeviceData` result, the RNG), F6 plays it back after a fresh `loadmap bank` with no
harness input, and compares each frame bit-exact with the recording. F8 = playback on real
dt, F9 = without restoring the RNG. Bank, RescueBot on, victims pinned (`fd_static_spawns 1`),
~32 s of walking, strafing, turning, jumping and spraying. `probes/demo_compare.py` and
`demo_entdiff.py` diff the traces.

| Run | Probe change | Player (pos / vel / view / move state) | World | Found |
|---|---|---|---|---|
| 1 | time + input + RNG at the first play frame | bit-exact 1954 / 1954 frames (play 1); on real dt (F8) off from frame 1279, up to 35 u | RNG cursor differs from frame 3 | dt must be fed; the start state differed slightly (y 0.05 u until the first landing) |
| 2 | load frames at a fixed 1/60 s; entity hashes; RNG hook on `NextU32` | bit-exact | victims / fires differ from frame 0 | `NextU32` sees no draws: the generator is inlined in `Rand_Float01`; `+0x32FC` from the core's player pointer is constant ASCII, not a timer (`mission.time_left` +0x343C is, and it matches exactly) |
| 3 | RNG saved / restored at `GameSession::LoadLevel` 0x0042AB00; hook `Rand_Float01` | bit-exact | differ from frame 0 | victims draw during the load (spawn, idle pose) |
| 4 | game time virtual from `LoadLevel` on (E0 + n x 1000/60 ms) | bit-exact | differ | load frames now equal (5 each) |
| 5 | per-frame histogram of RNG draws by call site | bit-exact | differ | first difference: **`ParticleSystem::Emit` draws** at load frame 2 (9 vs 14-28): presentation consumes the gameplay stream at a render-dependent rate |
| 6 | **RNG split**: presentation callers (16 functions) draw from a private generator | bit-exact | victims match to frame 78 | first gameplay difference: `FireRescueEnt::TrySpawnSpreadFire` at load frame 3 |
| 6b | every run preceded by a fresh `loadmap training` (same predecessor) | bit-exact | **victims match to frame 1517 (25 s)**; fires still diverge | the previous level / camera mattered too |
| 7 | load-frame census: entities, fires, extents, spread timers | bit-exact (`run7/compare.txt`) | at load frame 3 every run has 192 entities, 72 fires, all spread timers due, extents 0; then 1 vs 3 spread attempts | spread is gated on `FireFx::GetExtents` (the fire's visual size), whose keyframes `FireRescueEnt::HandleScriptCommand` schedules from the **level script**; the script's command timing in the first frames is the remaining uncontrolled input (not yet traced) |

Also seen: two playbacks of the same recording diverge from each other in the world the
same way (run 7, play 1 vs play 3), so this is start-up non-determinism, not a
record-vs-playback asymmetry. No input-call mismatches in any playback (the game polls each
device once per frame, as recorded).

**What this means for demos.** The player physics is deterministic given dt and input,
exactly as the TAS tools assume. The world isn't yet: anything a run does with fires and
victims (carrying, hosing, the RescueBot) would desync. To get there:

1. **Time:** a core clock service that can feed dt / `g_gameTimeMs` (done in the probe; the
   core's `pause` service already owns GameClock calls).
2. **Input:** a core DirectInput service hooking `GetDeviceData` (done in the probe).
3. **RNG:** the split, presentation vs gameplay, as a core RNG service (done in the probe,
   by call-site ranges from research.db; the list must be complete and reviewed).
4. **Start state:** RNG + virtual time from `LoadLevel`, a fixed predecessor or a defined
   reset, static spawns (done / partly done).
5. **Open:** the level-script / fire start-up timing (next step: instrument
   `ScriptTrigger` command execution and `FireRescueEnt::HandleScriptCommand` per load
   frame, find its time source), then repeat until world hashes match for whole levels.
   Unknown how many sources remain; each one cost one probe iteration here.
6. Then the product work: file format, menus / alt-tab / console / FDNYCore pause during a
   recording, a desync check (this probe's per-frame comparison), versioning.

## Ghosts: design sketch

- **Record:** at the level start (game.state 3 -> 1, the autosplitter's start), every frame:
  game time since the start, player position / yaw / pitch / move state (core `state`
  service). ~24 bytes per frame (10 min at 60 fps ~ 860 KB; 20 Hz is plenty).
- **Save:** per map, last and best run (`Ghosts/<map>.fdg`), best = the completed run with
  the lowest time; the file names the core version and map.
- **Play:** each frame, interpolate the ghost's sample at the same game time since start;
  show it, and optionally a HUD line with the time difference at the same place.
- **Show:**
  - **G1 box:** a translucent player-sized box with a facing marker, `gfx::SubmitWorldBoxes`
    in `LAYER_WORLD` (walls hide it). Exists today (trigger boxes). **EASY**, ~2-3 days with
    tests.
  - **G2 model:** `firefighter.act` loaded with `geActor_DefCreateFromFile`, `geActor_Create`,
    `geWorld_AddActor`, posed per frame with `geActor_SetPose`, translucent with
    `geActor_SetAlpha` / `geActor_SetBlendingType` (exported, never imported by the game: to be
    proven to render through the D3D driver + dgVoodoo); a walk motion by name optional. A
    new `gfx` block (R5) that adds / removes the actor across level loads. **MODERATE**, ~1 week
    more; risks: actor alpha / lighting in this driver, actor lifetime across loads.
- Practice-only (it shows information the game doesn't).

## Files

| File | What |
|---|---|
| `static_scan.txt` | demo strings, time / input imports, Genesis actor exports (`probes/demo_scan.py`) |
| `rng_sites.txt` | every RNG draw site and caller (`probes/demo_rngsites.py`) |
| `run1/` | v1: log, recording and playback 1 traces |
| `run2/`, `run3/`, `run6/` | logs of the intermediate probe versions (`run6/` is 6b, the same-predecessor run; run 4, 5 and 6's logs are quoted above, not kept) |
| `run7/` | final probe: log (load-frame census, RNG histograms), traces of the recording and two playbacks, entity dumps, `compare.txt` |

## Resumed 2026-10-24: runs 8-13, the remaining world non-determinism

Picked up where the spike paused (fire spread differed at load frame 3).

| Run | Probe change | Found |
|---|---|---|
| 8 | script timeline: every `ScriptActor::ExecCommand` 0x00470150 and fire size-keyframe start (`FUN_00447920`) per slot | **the level script replays identically** (1130 commands, same slots, same relative times). `ScriptActor::Tick` 0x00470740 waits on `g_gameTimeMs` (actor +0x48), which is controlled |
| 8b | the small-fire counter `DAT_00502B90` per load frame | it scales the spread delay (`TrySpawnSpreadFire` +0x4CA: delay / (const / counter)) but is 0 at every load: not the cause |
| 9 | every `TrySpawnSpreadFire` call: fire index, gate inputs, call path | different **fires** attempt to spread at load frame 3 in each run, all via `SetFireStrength` (0x00448320) from the fire vtable slot 18 handler 0x004483A0 |
| 10 | that handler: its argument and the vector it measures from | fire strength = Chebyshev distance from the **player** (`max(abs(dx), abs(dy), abs(dz))`, the arg is the PlayerRescueEnt) to the fire (+0x7C..+0x84): identical in every run |
| 10b | the handler's gates | **`FireRescueEnt+0xB8` is uninitialised**: values like 0, 69, -980352690. The handler runs only for 0 <= +0xB8 < 4, and the only writer is `inc [esi+0xB8]` (0x00448533; `re_fieldwrites.py`). An original game bug: which fires take their first heat, and spread, depends on leftover heap bytes. It is random in the vanilla game too |
| 11 | zero every `operator_new` block (0x004A5A84) | **play 1 identical to the recording on every reliable signal over 3466 frames (58 s)**: player, mission time, RNG draw counts and callers (gameplay and cosmetic), victims, entity count, entity dumps. Play 3 (after a diverged run, so different leftover heap) still differed in fire count |
| 12 | zero-fill the whole heap: `HEAP_ZERO_MEMORY` in `ntdll!RtlAllocateHeap` / `RtlReAllocateHeap` (covers malloc and realloc growth) | the two playbacks now agree exactly with each other; both differ from the recording from frame 49 (one more fire) |
| 13 | also feed GameClock +0x50 / +0x54 (1/dt and the next field; `RescueHud::Update` reads +0x54); spread log to 150 slots | **play 3 identical to the recording on every reliable signal over 3464 frames.** Play 1 identical until frame 101: at frame 106 the fire numbering shifts by one (a new fire is in a different list position), while every gameplay RNG draw count stays identical |

Also checked: the exe's import thunks for `timeGetTime` / `GetTickCount` / QPC / `GetAsyncKeyState`
have no callers (`re_thunkcallers.py`), so the time sources found in the first scan are complete.

**Remaining (open):** in some runs a spawned child fire ends up at a different position in the
entity list. Draw counts per site stay equal, but which fire gets which random number changes.
Hypothesis, not verified: entities take a free slot in the fixed 512-slot `EntityHandleTable`
(0x0049B9E0), and which slots are free depends on earlier, non-simulated lifetimes. Next step:
log each spawn's list / handle position around frames 100-110, and how `EntityList` /
`EntityHandleTable` pick a slot.

**Verdict update:** full demos are **MODERATE** now, no longer open-ended. With time, input and
RNG fed, presentation RNG split, a fixed predecessor and a zero-filled heap, a 58 s bank replay
was bit-identical on every reliable measure in 2 of the last 3 identical-setting playbacks. One
source is left, and it is identified to the spawn / list-order step.

Files: `run13/` (log, traces, script timelines with the heat / spread lines, `compare.txt`,
`firediff_*.txt`); probe helpers `demo_scriptdiff.py`, `demo_firediff.py`, `re_refs.py`,
`re_fieldwrites.py`, `re_thunkcallers.py`.

## Key facts (retail 1.1)

From the spike's status file (2026-10-24), folded in here with its next steps.

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

## Picking the demo work back up

**The exact next step (2026-10-24):** the last source. In run 13 play 1, at frame 106, a spawned
child fire is at a different position in the entity list (fire numbering shifts by one) while every
gameplay RNG draw count is identical, so which fire receives which random number changes from there.
- Hypothesis, unverified: entities take a free slot in the fixed 512-slot `EntityHandleTable`
  (0x0049B9E0), and which slots are free depends on earlier lifetimes that the simulation doesn't decide.
- Instrument `roc_SpawnEntity` 0x0049D4C0 / `EntityManager::FlushSpawnQueue` 0x0049C670: for each
  spawn, log the slot / list position, and which slots were free.
- Fix candidates: a deterministic slot pick, or a full handle-table reset at `LoadLevel`.
- The probe and its diff tools (`demo_compare.py`, `demo_firediff.py`, `demo_scriptdiff.py`) are in git history (`2a7f5bd`).

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

### Known gaps and caveats

- The presentation call-site list is heuristic (research.db names + "next symbol" ends).
  Some functions may be mis-bounded or missing.
- The probe's stack walk attributes `Rand_FloatRange` draws to the first `call` return
  address found on the stack. It is good enough for histograms, not a proven unwinder.
- Measured on one map (bank) and one ~32 s input script, on the VM (dgVoodoo, windowed).
- The +0x32FC vs +0x343C timer-offset discrepancy with an older research note is unresolved.
- Genesis3D source folders beyond `engine.c` / netcode were not read.

_The logs and run output named above were removed on 2026-10-27 (repository tidy-up); git history keeps them._
