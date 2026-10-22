# Plan — disable RescueBot, ship it as an .asi, merge with FDNYVictimSpawns

Status (2026-10-13): **BUILT** as `FDNYDeterministic.asi`. The owner's
decisions: name `FDNYDeterministic`; bot off on all difficulties; chatter
muted by default via the `MuteChatter` ini toggle; research corrections
committed first, which is now done (research.db KB 10/16/33, H125 superseded,
plus Ghidra labels and comments). Verified in a test host; **not yet tested
in-game.** See `FDNYDeterministic.README.md`.

Original plan text follows: Every address and byte
sequence below was read directly from `FDNY SP 1.1\FDNYFirefighter.exe`
(image base 0x00400000, file offset == RVA for `.text`) on 2026-10-13. A
read-only Ghidra pass then **confirmed claims C1–C7** the same day. That pass
corrected two addresses, which are already fixed below: the ctor store is at
0x004514F0, not 0x004514F6, and the TryStartRescue rets are at 0x004552F0 and
0x00455572. Not yet in-game verified.

Additional Ghidra facts:
- `RescueBot::Think` has a single caller: 0x00453876 in `PlayerRescueEnt::Think`.
- 0x0044BDE0 is **not a defined function**: the setter bytes have no code
  refs, no data refs and no pointer anywhere.
- The setrescuebot handler's `ebx` is confirmed as player+0x140.
- The only writers of +0x3530 are the `PlayerRescueEnt::Construct` zeroing
  at 0x00453FC7 and the BeginRescue INC at 0x00454B00.
- The same difficulty index picks a look clamp of 20/40/70 for idx 0/1/2.
  No string names which index is "easy".

---

## 0. Correction to existing research (must land first)

research.db currently says the bot is **opt-in** (KB 16 "The AI rescue bot",
KB 33 speedrun article, hypothesis H125: *"default missions never enable
it"*). **That is wrong:**

| Fact | Evidence (exe bytes) |
|---|---|
| `RescueBot::Construct` (0x00451480) sets `enabled = 1` | `C6 46 18 01` = `mov byte [esi+0x18],1` at **0x004514F0** |
| The only shipped script that touches it **turns it off** | byte scan of every game file: `setrescuebot 0` appears only in `Data\Maps\training.bsp`; `runrescuebot` / `setrescuebot 1` appear in no map |
| Difficulty decides how many victims the bot may take | `RescueBot::Think`, 0x004556AF..0x00455739 (see §1.1) |
| `PlayerRescueEnt+0x3530` is **not** a dead "lost" counter (KB 10 says nothing writes it) | `RescueBot::BeginRescue` increments it. It is the bot's claimed-victim count, and it is how a bot rescue reduces `outstanding` in both win gates. |

So on every non-training level the bot is live. Whether it ever acts
depends on difficulty, as shown below. This explains the "random rescues"
the owner sees.

---

## 1. Plan A — deactivate RescueBot on every difficulty

### 1.1 How a bot rescue happens (decoded)

```
RescueBot::Think (0x00455580)       thiscall, this = player+0x340C
  if (!enabled[+0x18]) return                       ; 0x0045559D
  if mission state (+0x18C) == 6 return
  if currentVictim && now > dropoffTimer -> drop off (UpdateZonesAndWin), clear
  if currentVictim -> return (busy)
  if mission state not in {3,4,5} -> return
  if g_devMode && F6 (DBG_ACTIVATE_RESCUEBOT) -> attemptTimer = now+1
  if now > attemptTimer:
      diff = *(*(0x00502AAC)+0x6E4)+0x2C            ; difficulty index
      k    = diff==0 ? 0.40 : diff==1 ? 0.25 : 0.0  ; stack doubles
      cap  = _ftol(total[+0x3528] * k + 0.5)         ; 0.5 @ 0x004DE6F8
      if botClaimed[+0x3530] < cap:
          TryStartRescue()                           ; CALL @ 0x00455732
      else: log "cap reached"
      re-arm attemptTimer (Rand_Below 30000|60000)
  chatter timer -> random "urge" subtitle line
```

Assuming idx 0 = easiest (consistent with the mission-timer table
×1.5/×1.5/×1.0), here are the bot's caps per map (n = victims in the shipped
.ini):

| n | idx 0 (×0.40) | idx 1 (×0.25) | idx 2 (×0.0) |
|---|---|---|---|
| 2 (training, but disabled by script) | 1 | 1 | 0 |
| 3 (oil_rig_a/b/d) | 1 | 1 | 0 |
| 5 (boat_b, mbase) | 2 | 1 | 0 |
| 6 (bank, bar_r, oil_rig_c, subway_a) | 2 | 2 | 0 |
| 7 (gas_r) | 3 | 2 | 0 |
| 9 (boat_a) | 4 | 2 | 0 |
| 10 (subway_b) | 4 | 3 | 0 |

Hardest difficulty already has cap 0 on the timer path. Easy and medium
let the bot silently take up to 40% / 25% of the victims, at random times.

**Entry points into a bot rescue.** All of them go through one function:

| Path | Site | Gate |
|---|---|---|
| Timer (all difficulties) | `CALL TryStartRescue` @ 0x00455732 in Think | `enabled` + cap |
| Dev F6 key | sets attemptTimer, then the same call | `enabled` + g_devMode + cap |
| `runrescuebot` script/console verb | `ScriptTrigger::Verb_RunRescueBot` 0x00475020 → `CALL` @ 0x0047502E | **none**: bypasses both `enabled` and the cap |

`RescueBot::TryStartRescue` (0x00455230) is `__thiscall` with **no stack
args**. Both of its exits are plain `C3 ret` (0x004552F0 is the early "busy" exit, 0x00455572 the main one). Note that Ghidra's
function body currently stops at 0x004552F0, so 0x004552F1..0x00455572 (which
holds the BeginRescue call) is outside the defined extent. It is the **only** caller of
`RescueBot::BeginRescue` (CALL @ 0x00455458). Neither caller uses its return
value: Think `jmp`s past it, and the verb does `mov al,1; ret`.

Writers of `enabled` (player+0x3424 = RescueBot+0x18):
1. ctor, 0x004514F0 (`= 1`)
2. `setrescuebot` verb inside `PlayerRescueEnt::OnScriptCommand`:
   `88 8B E4 32 00 00` = `mov [ebx+0x32E4],cl`, where `ebx` = embedded
   ScriptActor (player+0x140), so the target is player+0x3424
3. 0x0044BDE0, a 4-instruction setter (`setnz al; mov [ecx+0x3424],al`)
   with **no call or pointer references anywhere in the image** (dead code)

### 1.2 Options

| # | Patch | Covers | Leaves |
|---|---|---|---|
| **P1** | 0x00455230: `6A` → `C3` (TryStartRescue returns immediately) | **every** rescue path (timer, F6, `runrescuebot`), every difficulty | bot "urge" chatter subtitles every 2–4 min (cosmetic) |
| **P2** | 0x004514F3: `01` → `00` (ctor defaults `enabled = 0`) | timer, F6, chatter, the dropoff branch; the same state `training.bsp` ships with | `runrescuebot` verb (no shipped map uses it, and it is console-only under devMode), and `setrescuebot 1` re-enables |
| P3 | zero the 0.40/0.25 constants (stack immediates at 0x004556E1..) | timer path only | verb path, chatter. Strictly worse than P1. |

**Recommendation: apply P1 + P2 together under one switch.**
- **P1 is the guarantee.** It is the single choke point, so no combination of
  difficulty, script, console or dev key can claim a victim.
- **P2 makes the result authentic.** `enabled = 0` is exactly the state the
  developers put the game in for `training`, so Think's early-out is a
  shipped, exercised code path. P2 also removes the leftover chatter
  that would talk about a bot that no longer does anything.
- Both are 1-byte patches. Neither needs a hook or a trampoline.

Effect on the rules: with the bot gone, `+0x3530` stays 0, so both win gates
reduce to *player-delivered == total*. That is the intended single-player rule
and what hardest difficulty already plays like. The mission timer, O2, burn
and difficulty scaling are untouched.

### 1.3 Verification

1. **Static, now:** byte-check both sites against the expected originals
   (`6A FF 68 29` @ 0x00455230, `C6 46 18 01` @ 0x004514F0). Done for this
   plan.
2. **Ghidra (via loop commit subagent):** confirm the Think decode above,
   that 0x0044BDE0 has no indirect references, and that nothing else writes
   +0x3424 or +0x3530.
3. **In-game (owner):** play `bank` on the easiest difficulty. Stay more
   than 600 u from undiscovered victims for more than 2 min. Expected: no
   "RESCUEBOT:" subtitle, every victim stays put, and the win requires
   carrying all of them. Repeat on medium. With devMode on, also check that
   F6 and `runrescuebot` do nothing.

---

## 2. Plan B — RescueBot deactivation as an .asi (standalone)

Same skeleton as `FDNYVictimSpawns.cpp`: freestanding, kernel32 + user32,
Zig build, log file beside the .asi.

1. **Patch table, no hooks:**
   ```cpp
   struct BytePatch { const char* name; uintptr_t rva; uint8_t orig[4]; uint8_t want[4]; int n; int at; };
   // TryStartRescue entry:  orig 6A FF 68 29, write C3 at offset 0
   // RescueBot ctor:        orig C6 46 18 01, write 00 at offset 3
   ```
   Verify a 4-byte signature around each site, but write only the 1 byte
   that changes. This follows the existing "verify original bytes, abort on
   mismatch" pattern.
2. **All-or-nothing.** Validate every site (VirtualQuery + signature) before
   writing any. On any mismatch, log it and write nothing. Already-patched
   bytes count as success (idempotent), so a second copy of the plugin
   or a pre-patched exe is harmless.
3. **Timing:** apply the patches in `DllMain(DLL_PROCESS_ATTACH)` itself,
   not the init thread. A 1-byte `VirtualProtect` write has no loader-lock
   hazard, and this guarantees the patches land before the first
   `PlayerRescueEnt` is constructed. Logging can stay in the thread.
4. **Config:** `[RescueBot] Disable = true` (default true).
5. **Log:** one line per patch with its address, before and after bytes, and
   OK/ALREADY/MISMATCH.

---

## 3. Plan C — merge into one plugin

### 3.1 What the existing plugin looks like (inspected)

`asi/FDNYVictimSpawns.cpp` (360 lines) is a single TU with:
- freestanding shims (`memcpy/memset/memmove`, `_fltused`, `_DllMainCRTStartup`)
- a tiny libc, a timestamped `logline()` with `%F`, and an INI parser that
  knows `[Settings]` (`Enabled`, `StopRoam`) and `[Spawns]` (map → index list)
- `resolve_map()` reads `*(0x00502AAC)+0x688` → TLevelInfo → map name
- one 7-byte inline hook at 0x0045D717 (`8B CD E8 E2 02 04 00`) into a
  VirtualAlloc'd trampoline that calls `OnVictimSpawn(victim)`, replays the
  displaced `mov ecx,ebp; call SnapToFloor` and jumps back
- per-load ordinal reset on a >2 s `GetTickCount` gap
- `build.ps1` hardcodes the `FDNYVictimSpawns` name, image base 0x3C000000

Everything except `OnVictimSpawn`/`install_hook`/`resolve_map` is generic
infrastructure. RescueBot needs only the infrastructure plus a patch table.

### 3.2 Target shape

**Name:** `FDNYRescueTweaks.asi` + `FDNYRescueTweaks.ini` +
`FDNYRescueTweaks.log` (placeholder; owner's call, see §4).

**Source layout:** keep it a single TU like the original, to stay consistent
with the existing build and its no-CRT constraints. Organize it by section:
```
shims / tiny libc / log / ini          (moved verbatim from FDNYVictimSpawns.cpp)
patch helpers: write_protected, check_code, apply_byte_patches
feature: RescueBot  -> BytePatch table + rb_apply()
feature: VictimSpawns -> resolve_map, OnVictimSpawn, vs_install_hook (unchanged logic)
DllMain: rb_apply() synchronously; thread: log + config + vs_install_hook
```
The log prefix becomes `[FDNYRescueTweaks][rescuebot]` / `[...][spawns]`.

**Config, backward compatible:**
```ini
[RescueBot]
Disable  = true          ; no bot rescues on any difficulty (+ mutes bot radio chatter)

[VictimSpawns]
Enabled  = true
StopRoam = true

[Spawns]                 ; unchanged format -- copy the old section verbatim
bank = 0, 0, 0, 0, 0, 0
...
```
- The parser accepts the old `[Settings] Enabled/StopRoam` as aliases for
  `[VictimSpawns]`, so an old ini pasted in still works.
- If `FDNYRescueTweaks.ini` is missing but `FDNYVictimSpawns.ini` exists,
  read that one instead and log it.
- **Order problem:** the RescueBot patches run in `DllMain`, before the
  config is loaded. Fix: in `DllMain`, read just the `[RescueBot] Disable`
  key with `GetPrivateProfileIntA` (kernel32, no CRT) and do the full
  parse later in the thread. Alternatively, parse the whole small file
  synchronously in `DllMain`; it is a single ReadFile.

**Features are independent.** A signature mismatch in one feature is logged
and skips only that feature.

**Coexistence with the old plugin:** if `FDNYVictimSpawns.asi` is still in the
game folder, both plugins would try to hook 0x0045D717. Whichever loads
second sees `E9` instead of `8B CD…` and aborts its hook (safe, but
order-dependent). The merged plugin should check
`GetModuleHandleA("FDNYVictimSpawns.asi")` and log a clear
"remove the old FDNYVictimSpawns.asi" warning. The README tells the owner to
delete it.

**Build:** add a `-Name` parameter to `asi\build.ps1` (default
`FDNYRescueTweaks`) and keep the Zig flags and image base exactly as they
are. 0x3C000000 can be reused because the merged plugin replaces the old
one. Retire `FDNYVictimSpawns.cpp/.asi/.ini` only after the owner confirms
the merged build works in-game.

### 3.3 How the two features interact

- `StopRoam` forces pinned victims to state 4. That is exactly one of the
  bot's eligible states {4,7}, so pinning alone made the bot *more*
  predictable, not absent. Disabling the bot removes that interaction.
- Speedrun angle: H125 names exactly two split-relevant RNG sources, the
  victim spawn-spot draw and RescueBot timing. This plugin pins the first
  and deletes the second, so a level's victim layout and rescue workload
  become fully deterministic on every difficulty. It is still a modded
  category.

### 3.4 Steps

1. Commit the §0 research corrections (KB 10, 16, 33 and H125 updated in
   place, not deleted). Have a loop commit subagent label 0x0044BDE0
   `PlayerRescueEnt::SetRescueBotEnabled` and comment the cap table in
   Think.
2. Copy `FDNYVictimSpawns.cpp` → `FDNYRescueTweaks.cpp`, rename the
   log/ini/prefix strings, and add the `[VictimSpawns]` section plus the old
   aliases.
3. Add the patch helpers and the RescueBot `BytePatch` table, and apply it
   in `DllMain`.
4. Parametrize `build.ps1`, build, and run the existing PE sanity check.
5. Test host (as done for v0.1): LoadLibrary into a process that maps a
   *copy* of the exe's `.text` at 0x00400000, or simply a byte-image
   check. Confirm both RescueBot patches land, the spawn hook installs,
   a mismatch aborts only its own feature, and the old-ini fallback works.
6. Owner in-game test: §1.3 step 3, plus an existing spawn-pin check
   (e.g. `bar_r` pinned list, then check the log's FORCED lines).
7. Update `asi/README.md`, retire the old plugin, and update memory and
   research.db.

---

## 4. Open questions for the owner

1. **Chatter:** P2 also mutes the bot's "Let's move…" radio subtitles. If you
   want those kept, it becomes P1-only (`MuteChatter = false`).
2. **Name** of the merged plugin (placeholder `FDNYRescueTweaks`).
3. Should `Disable` be per-difficulty (e.g. keep the bot on easy only)? The
   cap table makes this easy: patch the 0.40/0.25 immediates instead of
   P1/P2. The default plan is off everywhere, as asked.
