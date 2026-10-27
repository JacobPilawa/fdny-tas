# Hose traces, frame order, render-phase slot writers, plane orientation

Research date 2026-10-16. Read-only. Nothing was written to the Ghidra DB. Companion to
`HOOK_SITES.md` and `WARP_CONDITIONS.md`.

**Method.**
- **Exe:** MCP decompile/xrefs on the live DB, plus capstone disassembly of the on-disk
  `FDNYFirefighter.exe`. Tool_Hose::Tick/PostTick are not defined functions in the live DB.
- **Genesis.dll:** capstone disassembly of the on-disk file at its preferred base 0x10000000.
- **Map data:** a direct parse of the plane chunk in every `Data\Maps\*.bsp`.

The helper scripts are in the session scratchpad (`scripts\dis.py`, `callers.py`, `iat.py`,
`exports.py`, `bsp_planes.py`).

**UNVERIFIED** marks inference. Everything else comes from instruction bytes.

The "slot" means the Genesis globals that hold the last hit: impact at 0x10362e9c..ea4 and plane
at 0x10362f48 (normal x/y/z, Dist at f54, Type at f58).

---

## 0. Two engine facts that drive everything below

### 0a. A ray that starts in solid returns 1 and writes nothing

This is the ray clip, FUN_100363f0 (0x100363f0-0x10036653).

- **Leaf case** (0x100363fb-0x10036420): it returns `(leaf.contents & mask) != 0` and writes nothing.
- **Split node:**
  - side = (start-side distance < 0) (0x1003652e-0x10036546).
  - It first recurses into the **near** child for start..mid (0x10036597).
  - **If the near child returns 1, it returns 1 immediately with no write** (`JNZ 0x1003663d` at
    0x100365a1).
  - Only a hit in the **far** child for mid..end writes the slot, and only if latch f44==0
    (0x100365cd-0x10036633).
- **Consequence:** a ray whose start point lies in a solid leaf returns 1 without writing at every
  level of the recursion. The caller sees a hit carrying whatever the slot already held.

Two callers handle this differently:
- **Actor lighting, FUN_100389e0, guards against it.** It sets `f60 = -1` on entry (0x10038a0a). On
  a hit with f60 still -1 it returns 0 (0x10038ba2-0x10038bab).
- **geWorld_Collision's ray sweep, FUN_10038470, has no such guard.** It never writes or reads f60
  (all of its global references are listed in section 4). A start-in-solid ray therefore comes back
  as "hit" with stale impact and plane, and the caller copies them into its Col. That is the same
  stale-hit behaviour the box path has.

### 0b. Every BSP plane in FDNY is stored with its dominant axis positive

`bsp_planes.py` walked chunk 10 (PLANES, 20-byte GFX_Plane) of all 17 maps. In every map, 100% of
planes satisfy:
- Type 0/1/2 (axial): the axis component is **positive**.
- Type 3/4/5 (dominant X/Y/Z): the dominant component is **positive**.

Example, bank.bsp:
- type0 + 2828, type1 + 1267, type2 + 2598
- type3 domX + 761, type4 domY + 384, type5 domZ + 641
- **zero** negatives

The ray clip copies this stored plane into the global verbatim (`REP MOVSD` from the plane array,
0x100365e2-0x100365fd). FUN_10038470 negates only its **local** copy when side f64 != 0
(0x1003889d-0x100388be).

The box sweep FUN_100372a0 copies the **global** out to the caller:
- It rotates it in place by the model xform (0x100376a7).
- It then does `REP MOVSD` from 0x10362f48 (0x100377a7-0x100377b4).
- There is no side test and no negation.

**So when the player's box sweep reuses a stale value written by a ray, it gets the plane as stored:**

| surface the ray hit | stale normal.y the player sees |
|---|---|
| flat floor (hit from above) | **+1.0** |
| flat ceiling (hit from below) | **+1.0**. The same type-1 plane; the global is not flipped. |
| Y-dominant slope (up to ~45 degrees) | **> 0** (between ~0.707 and 1) |
| wall, axial X/Z (type 0/2) | **0.0** |
| sloped wall (type 3/5) | \|y\| < the dominant component, so below 0.71; the sign can be either |

Against the Think step gate (`normal.y > 0.9` means floor, clip only, no step):
- Floor and ceiling hits **block** the warp path.
- Walls, and slopes steeper than about 25.8 degrees (normal.y <= 0.9), **open** it.

For a stale value written by a **box** clip (the rope, or other TraceToFloor users), the plane comes
from the box clip's local copy (0x1003718f). Whether that copy is ever side-flipped was not checked
(UNVERIFIED). The ray-written case above is the one that matters for the hose.

---

## 1. The hose's traces

### 1a. HoseGfx::RebuildGeometry 0x0046c5a0 traces the WATER STREAM

This is the water stream (where the spray lands), not the hose body. Evidence:
- It walks a `HoseTrail` of water points (HoseTrail::Eval 0x0046bc10 on this+0x168).
- It builds a `StreamTube` (this+0x2f4).
- On a hit it spawns splash particles (ParticlePool::SpawnParticle on this+1000), positions an
  emitter at the impact (this+0x43c), and draws `WaterImpactFx` (this+0x24).

**One geWorld_Collision call per stream segment, in a loop, stopping at the first hit.**
- The loop runs i = 0 .. this+4 (the trail point count).
- Segment 0 is never traced; it only adds the first ring.
- For i >= 1 the segment is traced only if all of these hold:
  - no hit yet (`local_e0 == 0`);
  - Eval succeeded;
  - this point's width `[0x502eac]` > 1.25 x this+0x374 (the minimum width; compare at
    0x0046c9d7-0x0046c9dd, else CollapseSegment).

**Arguments** (0x0046cb7d-0x0046cbd2):

```
geWorld_Collision(World, Mins=NULL, Maxs=NULL, Front=0x502e68 (point i-1), Back=0x502e74 (point i),
                  Contents=0x43, CollideFlags=2, UserFlags=0, CB=NULL, Ctx=NULL, Col=0x00502e80)
```

- **RAY** (Mins and Maxs are pushed as 0).
- The start and end points are consecutive points of the trail, nozzle outward along the arc.
- World comes from `[g_everGame+4]` (+0xb4 or +8 by byte +0x160) +0x78, the same selection as
  TraceToFloor.

**On a hit** (`TEST EAX,EAX` at 0x0046cbdb):
- It copies Col.Impact (0x502e8c..94) to the static 0x502ebc..c4 and to this+0xc..0x14.
- It copies Col.Normal (0x502e9c..a4) to 0x502eb0..b8. This is the local, possibly negated normal,
  used to reflect the splash direction.
- It sets this+0x1c=1 and `local_e0=1`. Every later segment then collapses, so **at most one hit
  per call**.
- It does not do anything else that touches collision.

A miss writes nothing.

**When does a call write the slot?**
- Only the hitting segment writes, and only if that segment's start point is **not** in solid
  (section 0a).
- If the stream's first traced segment starts in solid (nozzle inside geometry):
  - the call returns a stale hit;
  - HoseGfx draws its splash **at the stale slot position**;
  - the slot is not changed.
- So whenever there is a hit, the water's visible landing point equals the slot value at that
  moment: either a fresh write, or the stale value being displayed.

### 1b. When RebuildGeometry runs

- **UpdateEx 0x0046d1b0** (`thiscall(this, pos*, dir*, char spraying)`) always ends with
  `RebuildGeometry(this)`.
- Spraying flag behaviour:
  - Flow this+0x3b0 eases toward this+0x3b4 while the flag is set.
  - Otherwise it decays, `flow += -flow*0.2` per frame (0x0046d2b5).
- The width of each new trail point is `lerp(this+0x374, this+0x394, flow)`, pushed by HoseTrail::Push.
- Without spraying, new points get minimum width and fail the 1.25x gate.
- **Net (inferred from that gate, UNVERIFIED that Eval returns the per-point width):** the water
  rays run only while water is flowing. That means while the spray button is held, plus a short
  tail after release while flow decays and the wide points already in flight expire.

**Callers of UpdateEx:**

| call site | function | when |
|---|---|---|
| 0x004643b5 | **Tool_Hose::PostTick 0x004640f0**, on HoseGfx at tool+0x608 | spraying flag = byte tool+0xc58 |
| 0x00466c00 | PlayerHose::UpdateAimAndSpray (HoseGfx at +0x1480) | mini-game only, see 1e |
| 0x0046d48e | HoseGfx::Update 0x0046d480 (spraying=0), called from PlayerHose::Think_DrainWait 0x00465fd3 and 0x00467e45 | mini-game only |

The tool+0xc58 flag:
- Tool_Hose::OnActionPress 0x004632d0 sets it to 1 (`MOV byte [ESI+0xc58],1`).
- OnActionRelease 0x00463300 clears it to 0.

**Tool_Hose::PostTick 0x004640f0 flow:**
1. `CALL 0x45f600`, the base PostTick.
2. **If state +0xc68 == 1:** only the rope runs. That is HoseGfx::Advance 0x4826b0 on tool+0xc70
   plus RebuildStreamTube; no water.
3. **Else, if byte +0xd08 == 0:** nothing runs.
4. **Else:**
   - If spraying, it does reaction/aim work plus Fire::EmitEmbers 0x00463ef0 (0x00464216-0x00464355).
   - Then, **every frame**, the rope:
     - `HoseGfx::Advance(tool+0xc70, playerOrigin)` at 0x00464383, then RebuildStreamTube.
   - Then, **every frame**, the water:
     - `HoseGfx::UpdateEx(tool+0x608, nozzlePos, dir, spraying=[+0xc58])` at **0x004643b5**.

The meanings of +0xc68 and +0xd08 (probably deploy state and "hose in hand") are UNVERIFIED.

**Tool_Hose::Tick 0x00463460 does not trace.** It only calls player vtbl+0x18 (xform) and
HoseRope::FindNodeWithin 0x004859e0, then sets +0xc59 and +0xc60.

None of the trace wrappers (TraceToFloor, TraceGroundHeight, SegmentClearLOS, SweepWorldBox) is
reached from Tick or from PostTick, except the rope path in 1c. Fire::EmitEmbers 0x00463ef0 calls
only:
- non-trace helpers;
- FireFx::GetHeatAt 0x0044a7a0;
- **FireRescueEnt::DouseFire 0x00448970**.

None of those is a caller of any of the six IAT sites or of the wrapper callers listed in
HOOK_SITES section 6b (TrySpawnSpreadFire's only caller is 0x0044838e; FUN_00448320's only caller is 0x0044844c).

### 1c. The hose ROPE collider: box sweeps, every PostTick, spraying or not

- **Setup:** HoseGfx::Construct 0x00482850 installs the collider object. Its vtable is 0x004e36b8
  (0x004828d7), and it is set through HoseRope::SetCollider (0x004828e4).
- **Collider 0x00482740:** `SweepWorldBox(start=copy(arg1), end=arg2, radius=15.0, impact_out, normal_out)`.
- **roc_SweepWorldBox 0x00490000:**
  - If radius < 0.0001 (`[0x4e16b8]`), Mins and Maxs are NULL (ray).
  - Otherwise it is a box of +/- radius*0.5 (0x00490017-0x0049004d), so for the rope a **box of
    +/-7.5**, contents 1, flags 2.
- **Calls per frame:** HoseRope::Step 0x00485ef0 calls the collider (`CALL [EAX]` at 0x00486315)
  for **each rope node, up to 2 times per node** (re-sweeps after a hit; loop counter at
  0x004864d2-0x004864da).
- **When:** it runs from HoseGfx::Advance on **every Tool_Hose::PostTick**, including the
  state==1 branch (0x0046416d). It does not depend on spraying.
- A rope node resting on the floor typically produces a fresh floor hit (normal.y=+1) each frame
  (UNVERIFIED at runtime).

### 1d. No other collision is involved

The exe imports exactly one collision query, geWorld_Collision (IAT 0x004dc0bc). Its six direct
call sites are:

| call site | caller |
|---|---|
| 0x004486ed | FloorMe |
| 0x0046cbd2 | HoseGfx |
| 0x0048ff21 | SegmentClearLOS |
| 0x0048ffc0 | TraceGroundHeight |
| 0x004900d8 | SweepWorldBox |
| 0x00492da0 | TraceToFloor |

The only other reference to the slot, 0x004a25be, is the `FF25` import thunk.

The exe does **not** import:
- geWorld_ModelCollision or geWorld_TestModelMove;
- anything with "Shadow" in it (no such string exists in the exe);
- geActor_SetStaticLightingOptions.

geWorld_GetContents (0x00492e6e) is imported, but it is a point-contents query (body 0x10037e90),
not a sweep.

Douse and the water particles do not collide in Genesis. Particles are exe-side (ParticlePool),
and the douse path is the per-fire DouseFire call, which does not trace.

### 1e. PlayerHose is the hose mini-game only

- `RCS_PlayerHose` entity **instances** exist only in the hose levels City_h, const_h, dock_h and
  oil_rig_h. Each of those has `classname RCS_PlayerHose ... FirstPathPoint RCS_HosePathNN` plus
  51-122 RCS_HosePath points.
- The other 13 maps contain only the entity-type definition
  (`%typedef% %typename% RCS_PlayerHose`) and five RCS_HosePath strings, and no instance.
- So PlayerHose::Think_Advance's TraceGroundHeight (0x00467817) and its own water HoseGfx (+0x1480)
  run **only in those mini-game levels**, not in normal rescue play.

---

## 2. Frame order

```
GameSession::RunFrame 0x0042aea0
  GameClock::Tick -> GameControls::PollFrame
  GameSession::TickSubsystems 0x00429520
     vtbl[0](0,0) on session+0x08, +0x50, +0x98, +0xe0          (4 lists; entity Think dispatch, see below)
     EntityManager::FlushSpawnQueue on [+0x3c0]+0x40 and [+0x3bc]+0x40
     ROCSoundSystem::Frame
     FlushSpawnQueue on [+0x3c4]+0x40
  ROCRender::RenderFrame 0x00494670  (render phase; geActor_Render / geEngine_RenderWorld)
```

### 2a. Inside PlayerRescueEnt::Think 0x00452570 (straight-line order)

1. 0x004537ca: `[vtbl+0x64]`, IntegrateVelocity, which clears the +0x130 latch and computes disp.
2. **0x004537db: DepenetrateFromWorld.** The player's sweeps. **The first sweep reads the slot as
   it was left by everything since the previous frame's Depenetrate.**
3. 0x004537e0-0x00453815: `pos += disp`.
4. **0x00453860: UpdateLocomotionAndTools 0x0044f340.**
   - **Tool Tick loop**, 0x0044f440-0x0044f4ef: `CALL [vtbl+0x20]` at 0x0044f4e3 for every
     non-null entry of player+0x3564 (count +0x3568), i.e. every carried tool. This covers
     Tool_Hose::Tick (vtbl 0x004e2998+0x20) and AxeSwing::Tick (vtbl 0x004e27f0+0x20).
   - **Tool PostTick loop**, 0x0044fb42-0x0044fb6d: `CALL [vtbl+0x24]` at 0x0044fb61 for every
     carried tool. This is where Tool_Hose::PostTick runs **rope sweeps, then the water rays**.
5. 0x0045386b StepScriptedSequence, 0x00453876 RescueBot::Think (its LOS rays), throw/drop, then
   UpdateO2.

The only other call of UpdateLocomotionAndTools is PlayerRescueEnt::UpdatePassedOut 0x00451136
(the passed-out state).

**So the hose water trace runs AFTER the player's Depenetrate in the same frame, and it is the last
hose trace of that player Think.** The player's next sweep is next frame's Depenetrate. A hose
write can reach it only if **no fresh write** happens in between. "In between" covers:
1. the rest of the player Think (RescueBot LOS);
2. every entity that thinks after the player this frame;
3. the whole render phase;
4. every entity that thinks before the player next frame.

### 2b. Entity order: UNVERIFIED

- The four `vtbl[0](0,0)` calls on session+8/+0x50/+0x98/+0xe0 (objects spaced 0x48 apart) are
  the likely think lists.
- The concrete dispatch loop and the player's position in it were not pinned down in this pass.
- EntityManager::FlushSpawnQueue 0x0049c670 is **not** the think loop. It spawns queued entities
  and then calls their +0x20 and +0x24 init slots once.
- Whether list order equals spawn order, and where the player sits relative to victims and fires,
  remains open. A runtime log of H1 return addresses (HOOK_SITES) settles it in one frame.

---

## 3. Render-phase engine writers

| writer | gate | does it run in FDNY? |
|---|---|---|
| **Actor shadow** FUN_100121d0 (direct 0x10038c10 call at 0x1001224e) | Called at 0x100137ac only if `[puppet+0x5c] != 0` (0x1001378d-0x10013792). +0x5c is **DoShadow**. Puppet_Create 0x10013810 zeroes it (0x100138d2). Its only other writer is Puppet_SetShadow 0x100137d0 (0x100137f9), reached only from the export geActor_SetShadow 0x10008fd0 (no internal callers). | **Never.** The exe does not import geActor_SetShadow, and there is no "Shadow" string in the exe. |
| **Actor static-light ray tests** (FUN_100389e0 at 0x10011c29) | Needs `[puppet+0x74]` (AmbientLightFromStaticLights, tested at 0x10011b23) and `[puppet+0x78]==1` (TestRayCollision, 0x10011c14). Both are 0 from Puppet_Create (0x100138e4/e7). The only writer is Puppet_SetStaticLightingOptions 0x10011080, reached only from the export 0x10009340. | **Never.** That export is not imported by the exe. |
| **Actor floor-light ray** (FUN_100389e0 at **0x100119bf**) | `[puppet+0x54] != 0` (0x10011951), which is **AmbientLightFromFloor**. **Puppet_Create sets it to 1** (0x100138cf, ECX=1). The exe calls geActor_SetLightingOptions only 4 times (0x0042dc89 ReturnToFrontend menu actor, 0x004435f7 PathPointEnt, 0x0044600e MiscEmitterFX, 0x00446fbb undefined@0x446f60), and **all four pass AmbientLightFromFloor=0** (the four `PUSH 0` before the colours = PerBone, BoneName, MaxDynLights, FromFloor). | **Yes, every frame**, once per rendered puppet (victims, NPCs, the player's viewmodel/tools, and so on). It is called unconditionally once the puppet passes culling: all three branches join at 0x10013568, then call 0x10013583 (geActor_Render path); also 0x10012a9b (RenderThroughFrustum path). |
| **Lightmap shadow rays** FUN_10038290 (0x10032ce1 from FUN_10032b00 from FUN_10033d60) | Never resets latch f44, so it writes only when f44 is 0. Inside the floor-light path it runs right after a floor hit set f44=1 (0x10011a45), so it does not write there. From the world surface path (FUN_10025770/10025e00) it depends on the previous trace leaving f44=0. A dynamic light's CastShadow comes from DynamicLight+0x74 (DynamicLight::SyncToWorld 0x00492048). | Possible but latched. Runtime UNVERIFIED. |

### The floor-light ray in detail (FUN_10011940 to FUN_100389e0)

- **Geometry:** it runs from the actor's light reference point down **30000 units**
  (`[0x10051e28]` = 0x46ea6000 = 30000.0, 0x10011991-0x10011995). Contents 0x43.
- **Per model** (0x10038a30-0x10038b92) it resets f44=0 (0x10038afb), so **the latch does not
  stop it**, and calls the ray clip.
- **Outcome when the start is in open space:** a fresh write. The impact is the floor point under
  the actor (in model space, not transformed back). The plane is the floor plane, normal.y=+1.
- **Outcome when the start is in solid:** no write, and the f60 guard returns 0.

### Bottom line for a typical frame

**Yes. The last write before the next frame's player sweep is usually a render-phase floor-light
ray**, from whichever visible default-lit actor rendered last. It carries a floor plane with
**normal.y = +1.0**.

As a stale value that **blocks** the warp: Think takes exit A (clip only), and ClipVelocitySimple
keeps disp.

That is why the render traces do not usually cause warps, and why a hose write can survive:
1. When the player is out of bounds, the camera and the player's own viewmodel/tool actors are in
   solid. Their floor rays start in solid and **do not write** (f60 guard).
2. The same applies to everything else that traces from the player or camera: RescueBot and
   BlinkLight LOS from the camera, the hose nozzle itself, and the player's own box sweeps.
3. If no other default-lit actor is rendered, or all such actors are culled before lighting (the
   many early-outs to 0x100137b1), nothing overwrites the hose's value before the player's next
   Depenetrate.
4. The same holds if no victim or fire AI trace hits during that interval.

Which actors survive culling while the camera is inside solid (exe-side culling with
geWorld_MightSeeLeaf, and Genesis vis) is **UNVERIFIED**. That is the main thing a live run must
show.

---

## 4. Ray versus box per exe trace (confirmed from bytes)

| trace | path |
|---|---|
| HoseGfx::RebuildGeometry 0x0046cbd2 | **ray** (`PUSH 0; PUSH 0` for Mins/Maxs at 0x0046cba2/0x0046cbc4), 0x43, flags 2 |
| World::TraceGroundHeight 0x0048ffc0 | ray, 0x01 |
| roc_SegmentClearLOS 0x0048ff21 | ray, 0x43 |
| FireRescueEnt::FloorMe 0x004486ed | ray, 0x01 |
| roc_SweepWorldBox 0x004900d8 | box, unless radius < 0.0001. The rope passes 15.0, so it is a box. |
| ROCRender::TraceToFloor 0x00492da0 | always box |

FUN_10038470's global references (scan of 0x10038470-0x100389de):
- 0x10362f1c (BSP)
- f44 reset at 0x1003870a
- in-place transforms of e9c..ea4 and f48..f50
- Dist f54 at 0x10038842
- the f64 test at 0x1003889d

There is **no f60 write**. So geWorld_Collision rays have no start-in-solid guard.

---

## 5. The owner's question: every time, or only when the water hits a non-floor surface?

**Not every time. Only when the slot value the player's sweep reuses carries a non-floor plane
(normal.y <= 0.9).** In practice that means the water's landing point is on a **wall or steep
slope**.

Water landing on a floor **or a ceiling** leaves normal.y = **+1.0**, because FDNY's BSP stores
every Y-dominant plane with +Y and the global plane is never flipped. Think then only clips, so
there is no warp.

**Why "warps to where the water lands" (confirmed from code):**
- The water's drawn splash point and the player's stale step-down impact are **the same global**,
  0x10362e9c.
- A hose ray that starts in open space and hits writes it fresh.
- A hose ray that starts in solid reuses it and draws the splash there.
- Either way, the warp target equals the displayed landing spot.

**Conditions that must also hold (confirmed as necessary from code):**
1. Water is flowing. The rays run only for segments wider than 1.25x the minimum, i.e. while
   spraying plus a short tail (gate confirmed; the per-point-width meaning is inferred).
2. The stream actually hits something. At most one write per frame.
3. No fresh write lands between the hose ray (player Think, after Depenetrate) and next frame's
   first player sweep. Candidates:
   - render floor-light rays of visible default-lit actors (floor plane, blocks);
   - RescueBot LOS;
   - victim and fire AI traces;
   - entities that think before the player.
4. The WARP_CONDITIONS step pattern: Depenetrate sweep stale-hit, step-up MISS, forward MISS,
   step-down stale-HIT.

**Only a live run can settle these:**
- whether other actors' floor rays (or AI traces) overwrite the slot between frames while the player
  is out of bounds;
- the entity think order;
- how often the step-up/forward misses actually line up.

Running the HOOK_SITES H1/H4/H5 hooks with phase tagging (H7) while spraying a wall versus a floor
from out of bounds would answer all three. The prediction is: wall or steep slope means warps,
floor or ceiling means none.
