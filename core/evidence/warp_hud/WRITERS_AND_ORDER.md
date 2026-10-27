# Game-side slot writers (fire, hose, victim, axe), entity order, threads, tick

Research date 2026-10-16. Read-only. Nothing was renamed, commented, patched or created in the
Ghidra DB. Sources:
- capstone disassembly of the on-disk `FDNYFirefighter.exe`;
- MCP decompiles of the live DB (read-only `get_code` / `xrefs`);
- a raw-string scan of the `.bsp` entity lumps.

Anything marked **UNVERIFIED** is inferred. Everything else comes from instruction bytes.
Companion to `HOOK_SITES.md`, `WARP_CONDITIONS.md` and `HOSE_AND_FRAME_ORDER.md`.

"Slot" means the Genesis last-hit globals: impact at 0x10362e9c, plane at 0x10362f48.

**Warp-relevant plane** means normal.y <= 0.9:
- walls and steep slopes (> ~26 degrees) are warp-relevant;
- floors and ceilings both read +1 (dominant axis stored positive), so they *block* the warp.

---

## 0. Corrections to earlier notes

1. **roc_SegmentClearLOS flag polarity is the reverse of HOOK_SITES 6a.**
   - The code at 0x0048ff00-0x0048ff0f is `setne dl; dec edx; and edx,2`.
   - So **arg4 == 0 gives CollideFlags = 2** (world + models), and arg4 != 0 gives 0.
   - Every caller below passes 0, so they all trace models too.
   - It returns AL=1 when **clear** (geWorld_Collision returned 0) and AL=0 when blocked.
2. **"FireRescueEnt::Spawn" 0x00449340 is really AdvanceSpread** (its DB plate comment already says
   so).
   - FireRescueEnt::Think calls it **every frame while fire state (+0x70) == 4** (0x0044a186-0x0044a18f).
   - It reaches FloorMe at 0x00449a24 only on a spread *step*, and only when the def flag
     `[+0x78]+0x1c` is set (see 1c).
3. **The fire vfunc at 0x004483a0 is primary-vtable slot +0x48 ("Interact(other)")** of the
   FireRescueEnt vtable 0x004e1780. Its secondary vtable is 0x004e1774:
   - [0] = FireRescueEnt::Think 0x00449fe0
   - [1] = HandleScriptCommand 0x00449a80
   - The vtable start is proven by the refs at 0x0044a2b5 / 0x0044a36c (ctor and dtor).
   - It is called **once per frame per (fire, player) pair** from the interaction manager (section 5).
4. **TickSubsystems' four `vtbl[0](0,0)` calls are the entity passes** (section 5). The think list is
   session+0x50.

---

## 1. FIRE

### 1a. TrySpawnSpreadFire 0x00447a90, TraceToFloor at 0x00447cca (ret 0x00447ccf)

**Call chain**
- InteractionMgr 0x0049bd20 calls `fire->vtbl[+0x48](player)` = 0x004483a0.
- That calls FUN_00448320(dist) at 0x0044844c.
- FUN_00448320 calls TrySpawnSpreadFire at 0x0044838e.
- The **only** caller of TrySpawnSpreadFire is 0x0044838e.

**Gates, in order**
1. 0x004483a0: fire state `+0x70 != 5`, and `0 <= +0xb8 < 4`.
   - +0xb8 counts the heat messages queued this frame.
   - It is reset to 0 at the end of FireRescueEnt::Think (0x0044a27f writes secondary+0x94 = +0xb8).
2. `dist` = **Chebyshev** distance, i.e. max(|dx|,|dy|,|dz|), from the player's collision origin
   (player vtbl+0x0c returns player+0x68) to the fire position +0x7c..+0x84. It is computed at
   0x004483e4-0x00448444 and truncated by Ftol.
3. FUN_00448320:
   - `[+0x48]+4 != 0` (the fire FX is live).
   - It stores `+0x120 = dist`.
   - It keeps the global 0x00502b90 = number of fires within 750 of the player (inc/dec on crossing).
   - It calls TrySpawnSpreadFire **only if dist < 750** (`fcomp [0x4e16b0]=750.0; test ah,5; jp skip`).
4. TrySpawnSpreadFire itself:
   - FX extent.x >= 0.01 (`[0x4dfc78]`).
   - `g_gameTimeMs >= +0x124` (the spread timer).
   - `Rand(0,1) <= +0x128` (spread chance). **If the roll fails:** `+0x124 = now + 500 ms` and no trace.

**Trace geometry** (from bytes 0x00447bc7-0x00447cca)

| item | value |
|---|---|
| start | (cx + R*cos(theta), cy + **20**, cz + R*sin(theta)). c is the FX xform translation (`FireFx::MarkDirty()+0x28/0x2c/0x30`), and 20.0 comes from `[0x4e1604]`. |
| theta | Rand(0, 2*pi) |
| R | `r * (1 + f)`, where r = 0.5 * min(extent.x, extent.z) and f = Rand(0,1) * spread chance |
| delta | **(0, -200, 0)**. The locals are [+0x64]=0, [+0x68]=0xc3480000 (-200.0), [+0x6c]=0. |
| box | mins (-1,-1,-1), maxs (+1,+1,+1). A 2-unit cube. |
| path | TraceToFloor, so the box path, contents 0x43, flags 2 |

**Acceptance** (0x00447cd5-0x00447d2a). All of these must hold:
- the trace hit;
- |impact.y - start.y| >= 0.001;
- |impact.x - start.x| <= 0.1;
- |impact.z - start.z| <= 0.1.

A box impact on a vertical sweep always keeps x and z, so the x/z test is effectively a **stale-hit
rejector**.

**What happens on reject or miss**
- It returns **without** touching +0x124.
- So the same fire retries every frame: a fresh roll, and on success another trace.
- This continues until a child is spawned.

**What happens on success**
- It spawns a child RCS_FireRescue through the spawn queue.
- It sets `+0x124 = now + T * max(12/n, 1)^-1`. Here T is the child's Rand(1000..6000) ms keyframe
  and n = 0x00502b90, so the delay is T*min(n,12)/12.
- This formula is asm-derived (0x00447f58-0x00447fab); the exact meaning of T is UNVERIFIED.

**Frequency:** at most one trace per fire per frame. It only runs for fires within 750 (Chebyshev) of
the player, and only after the timer expires. It repeats every frame while the roll succeeds and
the trace is rejected.

**What it writes into the slot:** a vertical box drop.
- A fresh hit is almost always a **floor (normal.y = +1, blocks)**. A vertical segment cannot cross
  a vertical plane, so axial walls are never the hit plane.
- Warp-relevant writes are possible only on **steep slopes / ramps / sloped model brushes** under
  the drop point (UNVERIFIED how common that is in the maps).
- If the start box is inside solid, the engine returns a stale 1 with no write (HOOK_SITES 3a,
  UNVERIFIED engine semantics). That case is then rejected by the x/z test.

**Net:** the spread trace is mainly a floor **blocker**. It overwrites a wall value with a floor value.

### 1b. Fire Interact 0x004483a0: the heat-LOS ray at 0x004484c8 (ret 0x004484cd)

This is the remainder of the same per-frame Interact(player), after FUN_00448320. So each fire's
spread trace (if any) comes first and the LOS second.

**Gates** (0x00448451-0x004484ae)
- `+0x120 (dist) < 500` (`[0x4e16c0]`).
- `heat > 0.0001` (`[0x4e16b8]`). Heat is computed as:
  - `heat = FireFx::GetHeatAt(+0x12c, playerCollBlock, ...) * k`, where k is derived from `+0x128`
    (0x00448468-0x00448499);
  - the result is stored to 0x004ff8e0;
  - the exact formula is UNVERIFIED.
  - Practically: it is true whenever the player is near a burning fire.

**The ray** (`roc_SegmentClearLOS(render, start=0x00502b94, end=this+0x7c, 0)`)

| item | value |
|---|---|
| start | the **player's collision origin** (pointer stored in 0x00502b94 = player vtbl+0x0c result = player+0x68) |
| end | **fire position** +0x7c..+0x84 |
| type | RAY, contents 0x43, **CollideFlags 2** (world + models), Col 0x0050316c |

**Result**
- Blocked: heat *= 0.1 (0x004484d4-0x004484e0).
- Either way it then queues message 0x7531 (heat) to the player via 0x0049d4e0 and increments +0xb8.

**Frequency:** **every frame, for every fire whose Chebyshev distance to the player is < 500.**
It runs in fire list order (section 5), in the interaction pass, which is after every entity's Think.

**What it writes into the slot**
- **Something between the player and a nearby fire (wall, door brush, pillar):** a FRESH write.
  The impact is on the obstacle face and the plane is that face's stored plane. A wall has
  normal.y = 0, which is **warp-relevant**.
- **Clear LOS:** no write. The ray ends at the fire point; the fire sits about 4 units above its
  path point or on a FloorMe'd surface. A graze of the floor at the end point is possible but
  UNVERIFIED.
- **The player's origin is inside solid:** the ray starts in solid. That returns 1 (blocked) and
  **does not write** (HOSE_AND_FRAME_ORDER 0a). Heat is attenuated as if blocked.
  - So when fully embedded the fire LOS is *silent*.
  - It writes only while the player's **origin point** is in open space. Examples:
    - the box is partly clipped into a wall;
    - the player is out of bounds in an open void and a fire is on the other side of a wall.

### 1c. FloorMe 0x00448630 (engine call 0x004486ed, ret 0x004486f3)

**Ray** (0x0044864f-0x004486ed)
- start = pos + (0, **+50**, 0) (`[0x4e170c]`), end = pos + (0, **-400**, 0) (`[0x4e1710]`).
- pos is the FX translation (`0x00447870` = +0x12c+0x28).
- Contents **0x01**, flags 2, RAY, Col 0x00502bec.

**Callers**
- AdvanceSpread 0x00449a24: only when a spread step happens (the progress crosses half a segment
  or passes the path point), and only if `[+0x78]+0x1c != 0`.
- GotoPathPoint 0x004492a6: script command.

**Frequency:** event-driven, not per frame. **Floor-only** in practice (vertical ray), with the same
steep-slope caveat as 1a. A start in solid gives no write.

### 1d. Bottom line for the "fire gap"

**The heat-LOS ray (call 0x004484c8; at engine entry `[esp]` = 0x0048ff27 and `[esp+0x30]` =
0x004484cd) is the only fire trace that is horizontal-ish, so it is the only one that routinely writes
WALL planes.**

1. It runs every frame for each fire within 500 units (Chebyshev) of the player.
2. It runs **after all Thinks** (interaction pass) and before render.
3. It is the **last** fire-side write of the frame for that fire.
4. Spread raises the count of nearby fires, and new children appear closer to the player and
   around corners. So **"fire spread" makes this writer fire more often and from new angles.** That
   is consistent with the witnessed "fire spread warps the player" (UNVERIFIED at runtime).

The spread drop (1a) and FloorMe (1c) write floors. When they run after an LOS wall write from an
earlier fire in the list, they *overwrite* it with a blocker.

**Overlay classification** (engine-entry `[esp]` = 0x0048ff27, outer `[esp+0x30]` = **0x004484cd**)

| fire trace | engine-entry `[esp]` | outer address |
|---|---|---|
| heat LOS | 0x0048ff27 | `[esp+0x30]` = **0x004484cd** |
| spread drop | 0x00492da6 | `[esp+0x90]` = 0x00447ccf |
| FloorMe | 0x004486f3 | — |

---

## 2. HOSE: rope versus water

### 2a. Rope collider activity

**Who calls PostTick for the hose**
- PlayerRescueEnt's tool loops call `vtbl+0x20` (Tick, 0x0044f4e3) and `vtbl+0x24` (PostTick,
  0x0044fb61) on **every non-null entry of player+0x3564** (count +0x3568).
- There is no "is equipped" test in either loop. So **Tool_Hose::PostTick 0x004640f0 runs every
  frame whether or not the hose is the active tool.**
- The gate is the hose state `+0xc68`.

| `+0xc68` | where it is set | PostTick behaviour |
|---|---|---|
| 0 | init/equip path 0x00463785 | nothing: `+0xd08` = (state==1 or 3) = 0 -> skip at 0x00464180 |
| 1 | Tool_Hose SetActive(false) 0x00463427 turns 3 into 1, i.e. **put the hose away while it is still connected** | **rope only:** `HoseGfx::Advance(tool+0xc70, NULL)` at 0x0046416d, then RebuildStreamTube. No water. |
| 2 | Verb_TouchHose 0x0044cc55 / Verb_Reset 0x00450407 / OnLevelWon 0x0044c8b9 | nothing |
| 3 | OnLevelWon/hydrant path 0x0044c9b7 (hose connected and in hand) | rope `Advance(tool+0xc70, &playerOrigin)` at 0x00464383, then water UpdateEx at 0x004643b5 |

The state names are inferred from their writers (UNVERIFIED wording).

**Important difference between state 1 and state 3** (HoseRope::Step 0x00485ef0)
- Step does nothing while byte `this+0x64` ("settled") is set.
  - The flag is **cleared on every call that passes a non-NULL anchor** (state 3).
  - It is **set** when the anchor/tip node itself collides. That node is processed only when the
    anchor is NULL.
- So:
  - **State 3 (in hand):** the rope sweeps every frame.
  - **State 1 (stowed but connected):** the rope sweeps every frame **until the tip node first
    touches something**, then stops sweeping entirely. It stays settled until the hose is taken
    back in hand.
  - Settling usually happens within frames of putting the hose away (UNVERIFIED at runtime).

### 2b. How many sweeps per frame, and which direction

**Node count**
- HoseRope::SetNodeCount 0x00485d20.
- At construct: 240/segLen = 240/20 = **12 nodes**. segLen `+0x40` = 20.0 (0x004866b5); 240.0 is at
  `[0x4e36bc]`.
- On connect: `Ftol(L * 24.0 / 20.0)` = 1.2*L nodes (0x00482780 via 0x0044b91e / 0x0044cbeb /
  0x0044cc3c). L is the int hose-length argument (UNVERIFIED units).

**Sweeps**
- Step walks the nodes from the head (this+0x48) up to and including the anchor (this+0x60). In
  state 3 the anchor itself is skipped (`break`).
- The collider runs **up to 2 times per node** (loop counter `local_f4 < 2`; the second sweep runs
  only after a hit).
- So sweeps per frame are between N and 2N, with N = number of nodes.

**Direction:** from the node's current position to its integrated next position,
`p + v*dt + 0.5*a*dt*dt`.
- `a` includes gravity, which HoseGfx::Construct sets to (0, **-5000**, 0) at 0x004828a8.
- It also includes spring forces from the neighbour nodes.

**What the sweeps hit**
- A resting rope gives short **downward** sweeps, so mostly fresh **floor** writes (+1, blocks).
- While you walk/drag the hose, nodes are pulled sideways, giving **lateral** sweeps and **wall**
  writes.
- The code itself tests `normal.y >= 0.9` to tell floor contacts from wall contacts (0x00486385
  area: snap y versus push 5 units along the velocity). So wall contacts are an expected case.

**Order inside the player's Think:** Depenetrate, then the tool Tick loop, then the PostTick loop.
In PostTick: **rope sweeps (head to tip, nearest-player node last), then the water rays**. So
whenever water flows, the water ray (if it hits) is the final hose write.

### 2c. Telling rope sweeps from water rays by return address: CONFIRMED

These were checked by byte-level caller scans.

**Rope**
- roc_SweepWorldBox 0x00490000 has exactly **one** caller: 0x00482774 (the collider 0x00482740).
  The collider is reached only through vtable 0x004e36b8, from HoseRope::Step `CALL [EAX]` at
  0x00486315.
- At geWorld_Collision entry:

  | offset | value |
  |---|---|
  | `[esp]` | **0x004900de** |
  | `[esp+0x30]` | **0x00482779** |
  | `[esp+0x54]` | **0x00486317** (inside HoseRope::Step) |

- It is a box: +/-7.5 (radius 15 * 0.5), contents **0x01**, Col 0x005031f4.

**Water**
- HoseGfx::RebuildGeometry 0x0046c5a0 has exactly one caller (UpdateEx 0x0046d46e).
- It calls the IAT directly. `[esp]` = **0x0046cbd8**. RAY, contents 0x43, Col 0x00502e80.
- Mini-game caveat: in City_h/const_h/dock_h/oil_rig_h, PlayerHose's own HoseGfx (+0x1480) goes
  through the same RebuildGeometry, so that address also appears there.
- HoseGfx::Advance (0x004826b0) callers are only 0x0046416d and 0x00464383, both in
  Tool_Hose::PostTick. HOSE_AND_FRAME_ORDER's attribution of those to Fire::EmitEmbers is wrong;
  they are PostTick.

---

## 3. VICTIM

### 3a. State gating and call chain

`VictimRescueEnt::Think` 0x0045bea0 is a `thiscall` on the secondary subobject at victim+0xd4. It
switches on `[sub+0x4c]` = victim+0x120:

| state | what runs |
|---|---|
| 5 (carried) | return |
| 6 (rescued / bot-claimed) | UpdateFadeFlicker |
| **7 (roaming)** | **UpdateRoamMovement 0x0045b220** (call at 0x0045bec8) |

- **Every victim trace that runs during gameplay is in state 7.** State 7 is entered only through the
  `roam` script verb (ValidateSpawn). See the KB *victims* article.
- State 4 (idle) and state 5 (carried) do no traces.
- UpdateRoamMovement calls AdvanceRoamTarget 0x0045ae50 **unconditionally every frame** (0x0045b28a).
  It is the only caller.
- Separately, ValidateSpawn 0x0045c7f0 runs 4 CanReachOffset probes **once**, when the `roam` verb
  fires. SnapToFloor at spawn is load-time only.

### 3b. AdvanceRoamTarget 0x0045ae50

The waypoint is T = victim+0x1c8; the victim position is +0x4c.

1. **Early out:** unless `|T - pos|^2 <= 5184` (72 units), it returns with **no traces**.
2. **"Stuck" branch** (flag byte +0x1ec set):
   - offset = the stored direction (+0x1e0) rescaled to Rand(6..18) and rotated by Rand(0..360)
     degrees about Y, with y = 0;
   - **one** CanReachOffset;
   - on success, clear the flag and advance T.
3. **Normal branch:**
   - If the player (vtbl+0x18 xform translation) is within 432 of T **and** within 45 degrees of
     the victim's facing: **LOS ray** `roc_SegmentClearLOS_ActiveWorld(T, playerPos, 0)` at
     0x0045afbe (engine ret 0x0048ff27; wrapper ret 0x0049014d; real caller at `[esp+0x44]` =
     0x0045afc3).
     - It runs from the **waypoint to the player position**, roughly horizontal, contents 0x43,
       flags 2.
   - Then an offset of 12 units (toward or away from the player, or along the previous heading),
     with y = 0.
   - Up to **3** CanReachOffset calls: offset, then (offset/2 rotated +30 degrees), then (rotated -60).
   - If all 3 fail: store the direction, set +0x1ec, and from the next frame on run branch 2 every
     frame.

### 3c. CanReachOffset 0x0045ab40 (box `this` = victim, ESI/EBX)

1. Returns 0 with no trace if:
   - |offset|^2 < 0.001; or
   - |T+offset - anchor(+0x218)| > leash radius (+0x224).
2. **Horizontal box sweep:** `TraceToFloor(start = T, mins = victim+0x78, maxs = victim+0x84,
   delta = offset (6-18 or 12 units, y = 0))` at 0x0045ac9b (ret 0x0045aca0).
   - A **HIT returns 0 immediately**, so this sweep is the last write of the call.
   - A hit means the waypoint box ran into a wall, prop or model brush. That is a **WALL-type fresh
     write** (normal.y = 0 for an axial wall).
3. On a miss, it checks the obstacle list (+0x1c4, kind-6 volumes). That check is pure maths with no
   trace and returns 0 early if inside.
4. Then two **vertical** rays: `World::TraceGroundHeight(T + rotY(offset, +/-15 degrees), |mins.y|+240)`
   at 0x0045adde / 0x0045ae0c (ret 0x0045ade3 / 0x0045ae11).
   - Each runs from y+50 down to y-(|mins.y|+240), contents 0x01, RAY.
   - These write **floors (+1)**.
   - The second runs only if the first landed within 6 units of the expected height.

### 3d. Which victim trace is "a victim hitting a wall"?

**The CanReachOffset box sweep at 0x0045ac9b, when it hits.** A stuck victim (in a corner or against
a wall) re-runs it **every frame** through the +0x1ec branch until a random direction works.

Each frame's last victim write is then a wall plane. Return addresses:

| item | value |
|---|---|
| engine `[esp]` | 0x00492da6 |
| outer `[esp+0x90]` | **0x0045aca0** |
| saved EBX at `[esp+0x30]` | the victim |

**Other victim cases**
- **A free-walking victim:** the sweep misses, and the TraceGroundHeight rays then leave a **floor**
  (blocker) as the last write.
- **The victim-to-player LOS** (0x0045afbe) runs earlier in the same call, so CanReachOffset usually
  overwrites it.
  - It survives only if every CanReachOffset in that call either was gated out (leash or
    obstacle, no trace) or missed and its ground rays were skipped.
  - When the player is embedded in a wall, this ray's *end* point is inside solid. A ray that
    starts in open space and ends inside solid **writes the face it enters**, so it writes the wall
    next to the player.
  - That makes it a plausible wall writer **when it is the last victim write** (UNVERIFIED how often).

**Frequency:** per state-7 victim.
- Traces happen only on frames where the victim is within 72 units of its waypoint.
- Walking: each successful advance moves T 12 units, so the traces repeat about every
  12/(victim speed per frame) frames (speed UNVERIFIED).
- **Stuck: every frame.**

---

## 4. AXE: AxeSwing::ResolveHit 0x00460b90, TraceToFloor at 0x00460dd1 (ret 0x00460dd6)

**Gate:** it runs only on the frame where the swing fraction crosses 0.275, that is
`+0x340 < 0.275 <= +0x33c`; otherwise it returns before the trace. So there is **one trace per
swing**. It is called from the swing update when phase +0x368 == 1, which runs inside the player's
tool Tick loop, i.e. after Depenetrate.

**Geometry** (V = the player view xform at player+0x34a4)

| item | value |
|---|---|
| start | eye (player+0x34cc) + V.up*20 + V.right*(+/-10). The side comes from handedness byte +0x1fc. |
| delta | V.fwd*100 - V.up*60 (forward 100, down 60, ~117 units), from `FUN_0049e770` = forward axis |
| box | +/-6 (12-unit cube), contents 0x43, flags 2 |

**What it writes:** a fresh hit on whatever is in front of and below the player.
- Swinging at a wall or door gives a **WALL plane**. Swinging at the floor gives a floor.
- If the start is in solid, it is stale with no write.

---

## 5. ENTITY THINK ORDER

### 5a. GameSession::TickSubsystems 0x00429520: four passes, fixed order

Each pass is a list object embedded in the session (ctor GameSession::Construct 0x0042b860). Lists
are populated by GameSession::RegisterListener 0x00429490, which tests entity mask `vtbl+0x1c`
(= `[ent+0x14]`, 0x00407bd0) against each list's mask.

| # | session+ | list vtbl | mask | per-entity call | role |
|---|---|---|---|---|---|
| 1 | +0x08 | 0x004e4148 | 1 | `vtbl+0x30`, then IsDead (`vtbl+4` = byte +0x10) and removal | pre-pass (player: 0x0049db80, clears +0xd4) |
| 2 | **+0x50** | 0x004e4164 | 2 | **`vtbl+0x14`** (0x0049c3d0) | **THINK** |
| 3 | +0x98 | 0x004e4150 | 8 | `vtbl+0x50`, then `vtbl+0x4c` | post-pass |
| 4 | **+0xe0** | 0x004e4124 | 0x20 | 0x0049bd20 (see below) | **pairwise Interact** |

After the four passes: FlushSpawnQueue x2, ROCSoundSystem::Frame, then FlushSpawnQueue again. Newly
spawned spread fires and herodown victims are registered here, at the **end** of the frame.

**Pass 2 (THINK) chain**
- Player: `vtbl+0x14` = PlayerRescueEnt::FrameUpdate 0x0044fe20, then (if `+0x3438 == 0`)
  ScriptActor::Tick(player+0x140) 0x00470740, then `[secondary vtbl 0x004e1c40][0]` =
  **PlayerRescueEnt::Think 0x00452570**.
- Fire: `vtbl+0x14` = 0x0043afe0, then ScriptActor::Tick(fire+0x24), then FireRescueEnt::Think
  0x00449fe0.
- Victim: the same mechanism, ending in VictimRescueEnt::Think (secondary at +0xd4).
- ScriptActor::Tick skips Think while its `+0x4c == 1` (timed script wait).

**Pass 4 (InteractionMgr 0x0049bd20)**
1. It first purges dead entities.
2. Then, for each registered entity E in list order and each class bucket i (0..8) with bit i set
   in E+0x20, it calls `E->vtbl[+0x48](other)` for every entity in bucket i.
3. Bucket index = entity class `+0x18`:
   - player = **1** (0x0045402e);
   - victim = 2 (0x0045df35);
   - fire = 6 (0x0044a429).
4. Fire and victim both set **only bit 1** (`0x0049d330(1)` at 0x0044a430 / 0x0045df3c).
5. So each fire and each victim interacts with **the player only**, once per frame:
   - fire: 0x004483a0, which does the spread trace and the heat LOS;
   - victim: its slot +0x48, the pickup proximity test, no trace (UNVERIFIED that it never traces).

### 5b. List order = registration order = spawn-queue order

- Lists append at the tail (WeakRefList::AppendNode 0x0042b1a0; InteractionMgr add 0x0049bc90) and
  iterate from the head.
- FlushSpawnQueue 0x0049c670 creates and registers each queued entity in queue order (0x0049c6f9),
  and only then runs the Spawn and SpawnAtSpot passes.
- Per the KB, World_LoadBSP queues entities in `.bsp` entity-lump order. **So think order = `.bsp`
  order** for map-placed entities. Runtime-spawned fires and herodown victims go to the tail.

**Player position relative to victims/fires** (raw `classname` scan of each map's entity lump; the
lump-order equals queue-order step is from the KB):

| map | player index | victims / fires **before** the player | victims / fires after |
|---|---|---|---|
| bank, bar_r, boat_a, gas_r, mbase, subway_a, subway_b | **0 (first)** | 0 / 0 | all |
| training | 11 | 0 / 0 | 2 / 7 |
| boat_b | 52 of 87 | **3 / 29** | 2 / 13 |
| oil_rig_A | 42 of 59 | **3 / 12** | 0 / 3 |
| oil_rig_B | 61 of 65 | **3 / 14** | 0 / 0 |
| oil_rig_C | 121 of 132 | **6 / 24** | 0 / 0 |
| oil_rig_D | 40 of 72 | **1 / 7** | 2 / 9 |

### 5c. Consequences for same-frame versus next-frame visibility

Within frame N, the order is:

1. pass 1;
2. pass 2: entities before the player think; **then the player's Think**, which runs:
   - Depenetrate (the player's sweeps);
   - tool Tick (axe trace);
   - tool PostTick (rope, then water);
   - RescueBot LOS;
3. pass 2: entities after the player think;
4. pass 3;
5. **pass 4: every fire's spread trace and heat LOS**;
6. FlushSpawnQueue (new fires register; their Spawn and SpawnAtSpot run here, and FloorMe from
   GotoPathPoint is possible);
7. **RenderFrame** (engine floor-light rays and lightmap rays).

Which writes reach the player's sweep:

| writer | reaches the player's sweep in |
|---|---|
| **Victim writes, victim listed before the player** (boat_b, oil_rig_A/B/C/D) | **frame N**. Only the Thinks of entities between that victim and the player can overwrite. **No render phase in between.** |
| **Victim writes, victim listed after the player** (all other maps) | frame N+1, after the rest of pass 2, pass 3, pass 4 (fire LOS and spread) and the render phase |
| **Fire writes (pass 4)** | **always N+1**, behind the whole render phase. A fire trace can never precede the player's sweep in the same frame. |
| Fire **Think** traces (FloorMe through AdvanceSpread) | same rule as victims: before or after the player by list order |
| Hose rope/water and axe | inside the player's own Think, **after** Depenetrate, so N+1 behind everything listed above |

**The last fire/victim write before render** is from whichever entity traced last in list order. In
pass 4 that is the last fire within 500 (LOS) or 750 (spread) of the player, in list order.
Runtime-spawned fires are at the tail, so the **newest spread fires write last**.

---

## 6. THREADS

**FDNYFirefighter.exe**
- Its import table has **no** CreateThread, _beginthread(ex), timeSetEvent, CreateTimerQueue or
  QueueUserAPC. The only thread-related imports are GetCurrentThread / GetCurrentThreadId.
- No "CreateThread" / "_beginthread" string exists in the exe, so nothing resolves them through
  GetProcAddress.
- So **the game code itself creates no threads.**

**Genesis.dll**
- Kernel32 imports include no CreateThread (only GetCurrentThreadId and DisableThreadLibraryCalls).
- **Genesis creates no threads.**

**fmod.dll**
- It is UPX-packed: the visible imports are only LoadLibraryA / GetProcAddress, and the real imports
  are hidden.
- FMOD 3 is known to run its own mixer/stream thread (UNVERIFIED for this build).
- The exe registers **no FMOD callbacks**. It imports no `*Callback*`, `*DSP*` or
  `File_SetCallbacks` exports (checked by name against the exe IAT). So an FMOD thread has no path
  into exe code, and therefore none to geWorld_Collision.

**dinput8.dll**
- It is the ASI loader (2.3 MB). It imports CreateThread, SuspendThread, Get/SetThreadContext and
  others.
- Any threads it or loaded ASIs (including the overlay and FDNYVictimSpawns.asi) create are
  **third-party, not game code**.
- An ASI that calls game functions from its own thread would be the only concurrency risk.

**D3D8 / D3D9 / DDraw / D3DImm** (dgVoodoo wrappers)
- These may run internal threads, but they never call Genesis collision.

**Level loading**
- LoadLevel (and with it World_LoadBSP, FlushSpawnQueue, SpawnAtSpot and SnapToFloor) runs
  **synchronously on the main thread**, inside RunFrame at 0x0042aebb (when `session+0x68c` is set).

**Conclusion:** no game, engine or FMOD thread can reach geWorld_Collision. Every slot write comes
from the main thread, in the frame order of section 5.

---

## 7. TICK: GameSession::RunFrame 0x0042aea0

### 7a. Call path

1. MainWindow::RunMessageLoop 0x00499410:
   - PeekMessage; if a message is pending, Get, Translate and Dispatch it, and **no frame** runs
     on that iteration.
   - If no message is pending, it calls `[app+0x40]->vtbl+0x74` = EverGame::FrameUpdate 0x0049a310
     (0x00499608), then `+0xa0` = FrameRender 0x00499e60.
2. FrameUpdate ticks the UI children and then **tail-jumps `vtbl+0x60`** (0x0049a3eb). That is
   0x0042bc60 (`ECX = [ECX+0xb4]`), which JMPs to RunFrame.

So **RunFrame runs exactly once per idle loop iteration.** Its frame cap is GameClock::Tick and
ComputeFrameDelta 0x0042bf70 (a `Sleep(1)` busy-wait plus the 0.1 s clamp).

### 7b. Inside RunFrame

1. Optional LoadLevel.
2. GameClock::Tick (0x00502ab8).
3. `[g_everGame]->vtbl+0x14` (input).
4. Then one of two paths:
   - **`session+0x64c != 0`:** call 0x0042d8c0 (named ReturnToFrontend in the DB; called every
     frame here, so probably the menu update), then ROCSoundSystem::Frame, then **return**. There is
     **no TickSubsystems and no RenderFrame.**
   - **Otherwise:** TickSubsystems (0x0042af0a), then RenderFrame (0x0042af19), in a strict 1:1 pair.

### 7c. Pausing

**The ESC menu does take the `+0x64c` path.**
- `+0x64c` is MenuManager(+0x3c8)+0x284.
- It is set to 1 at 0x0042d55c (in 0x0042d480, called by OnKey ESC at 0x00429654/0x0042967d).
- It is set to 0 at 0x0042d1cb (0x0042d1b0, ESC-resume at 0x0042963f).
- So **while the ESC menu is up, RunFrame keeps being called every loop iteration, but no
  simulation, no render-phase traces and no slot writes happen.** That is UNVERIFIED for menu
  screens that may themselves render 3D, but none of RenderFrame's paths run.

**Alt-tab** (WM_ACTIVATEAPP, msg 0x1c, wParam 0):
- GameClock::Pause (0x004994f8), then a call through `[0x4dc1d8](app+0x74, 0)`.
- Then a **nested blocking GetMessage loop** (0x00499510-0x0049954a, with Sleep(500) on a 0
  return) until WM_ACTIVATEAPP(TRUE). After that: GameClock::Resume (0x004995d3).
- **RunFrame is not called at all while the game is deactivated.**

### 7d. Where the overlay should count frames

| hook | what it counts |
|---|---|
| **0x0042aea0** (RunFrame entry) | ESC-menu frames as well as game frames |
| **0x0042af0f** (post-tick / pre-render, HOOK_SITES H7) | simulated frames only, exactly one per TickSubsystems+RenderFrame pair. **This is the better frame counter.** |

Alt-tab periods are invisible to both hooks, which is fine.

---

## 8. Overlay cheat-sheet: identifying the writer from the engine-entry stack

| writer | `[esp]` at geWorld_Collision entry | discriminator | plane type expected |
|---|---|---|---|
| Player sweeps | 0x00492da6 | `[esp+0x90]` in 0x0043f732/7e0/8bf/920/9b1 | — |
| **Fire heat LOS** | 0x0048ff27 | `[esp+0x30]` = **0x004484cd** | **wall** if an obstacle is between player and fire |
| Fire spread drop | 0x00492da6 | `[esp+0x90]` = 0x00447ccf | floor |
| Fire FloorMe | **0x004486f3** | — | floor |
| **Victim reach sweep** | 0x00492da6 | `[esp+0x90]` = **0x0045aca0**; `[esp+0x30]` = victim | **wall** (horizontal box) |
| Victim ground rays | 0x0048ffc6 | `[esp+0x34]` = 0x0045ade3 / 0x0045ae11 | floor |
| Victim to player LOS | 0x0048ff27 | `[esp+0x30]` = 0x0049014d and `[esp+0x44]` = **0x0045afc3** | wall (ends in the player's wall) |
| Axe | 0x00492da6 | `[esp+0x90]` = 0x00460dd6 | wall or floor (one per swing) |
| Hose rope | 0x004900de | `[esp+0x30]` = 0x00482779, `[esp+0x54]` = 0x00486317 | floor at rest, wall when dragged |
| Hose water | 0x0046cbd8 | — | wherever the stream lands |
| RescueBot LOS | 0x0048ff27 | `[esp+0x30]` = 0x0049014d, `[esp+0x44]` in RescueBot::TryStartRescue | — |
