# When a collision trace result becomes a player position commit ("warp")

Research date 2026-10-16. Read-only (no DB writes). Everything below comes from instruction bytes
read through GhidrAssistMCP (`get_code` disassembly, `disassemble_at`, `get_data_at`,
`search_bytes`). Anything marked **UNVERIFIED** was not proven from bytes. Companion to
`HOOK_SITES.md` section 4.

Stack offsets are given against a fixed reference point (S or T, defined per function) so they
don't drift with pushes.

---

## 0. Things this pass verified that correct earlier notes

- **Player primary vtable = 0x004e1c48.** PlayerRescueEnt::Construct 0x00453d98 does
  `MOV [ESI],0x4e1c48`. Its `[ESI+0x140]` subobject gets 0x004e1c40, and `[ESI+0x24]` gets 0x004dfc30.
- Relevant slots of 0x004e1c48:

  | slot | target | meaning |
  |---|---|---|
  | +0x0c | 0x0049db90 = `JMP 0x0049d790` | ModelEntity::GetCollisionBlock. Writes this+0x68 = xform(this+0x24) applied to (0,0,0), i.e. the origin. Returns **this+0x68**, never NULL for a valid `this`. |
  | +0x30 | 0x0049db80 | `MOV byte [ECX+0xd4],0 ; RET`, a stub |
  | **+0x38** | **0x0043f810** | **BModelEnt::Think** |
  | +0x64 | 0x0043f680 | `CALL 0x0049dbc0 (IntegrateVelocity) ; MOV byte [ESI+0x130],0 ; RET` |

- **The DB plate comment on DepenetrateFromWorld is wrong.** It says "[vtbl+0x38] push-out (stub
  0x0049db80, RET 4 - no-op)". In fact 0x0049db80 is slot **+0x30**, and +0x38 is the real
  BModelEnt::Think 0x0043f810. So for the player, Depenetrate **does** call the step/commit code.
  The comment's "FAILS SAFE" conclusion does not hold.
- **The step-up latch +0x130 is cleared once per frame.** PlayerRescueEnt::Think 0x004537ca-cc runs
  `MOV EAX,[EDI] ; CALL [EAX+0x64]` (= 0x0043f680) right before the spectator test and the
  Depenetrate call at 0x004537db.
  - The only other write to +0x130 is the commit at 0x0043f9bb, which sets it to 1. Byte search:
    `c6 ?? 30 01 00 00` and `88 ?? 30 01 00 00` each have exactly one hit.
  - So **at most one Think commit happens per frame.**
  - The +0x64 call happens unconditionally inside that block. Whether the block itself is always
    reached is UNVERIFIED.
- **Step height this+0x138 = 16.0.** BModelEnt::Construct 0x0043f634 does
  `MOV dword [ESI+0x138],0x41800000`. The only reader is Think 0x0043f849. The other byte-pattern
  hits (ParticleNode, WarnTimer, FUN_0048d330) are other classes; that none of them writes to a
  BModelEnt is UNVERIFIED.

---

## 1. ROCRender::TraceToFloor 0x00492ce0-0x00492dee

`__thiscall(ROCRender*, start, mins, maxs, delta, impact_out, normal_out)`, `RET 0x18`.

| step | addr | what it does |
|---|---|---|
| 1 | 0x00492ce4-0x00492cfc | Picks the world: `[this+0xb4+0x78]` if byte `[this+0x160]` is set, else `[this+0x8+0x78]`. **If the world is NULL, returns 0** (0x00492de6) without tracing. |
| 2 | 0x00492d02-0x00492d68 | Builds locals: end = start + delta (component-wise FADD), a start copy, and mins/maxs copies. |
| 3 | 0x00492d7c-0x00492da0 | `geWorld_Collision(World, &minsCopy, &maxsCopy, &startCopy, &end, 0x43, 2, 0, NULL, NULL, &Col)`, with Col at local `[ESP+0x34]`. Mins and Maxs are local addresses, so they are never NULL: this is **always the box path**. |
| 4 | 0x00492da9 | `TEST EAX,EAX ; JZ` checks all 32 bits. On 0 it returns AL=0, and impact_out / normal_out are **not written**. |
| 5 | 0x00492dad-0x00492ddd | On nonzero it copies **Col.Impact (+0x0c) to impact_out**, then **Col.Plane.Normal (+0x1c) to normal_out**, and returns AL=1. |

What it does **not** do:
- No Ratio test, no distance test, no back-off or epsilon nudge of the impact.
- No contents check. There is no call to geWorld_GetContents; 0x00492e6e lies outside this function.
- Plane.Dist and Col.Model are not passed out.

The result is exactly geWorld_Collision's return (as a bool) plus a raw copy of Impact and Normal.
If geWorld_Collision returns 1 with stale globals, TraceToFloor passes the stale values through
unchanged.

---

## 2. BModelEnt::DepenetrateFromWorld 0x0043f6d0-0x0043f803 (`__thiscall`, plain RET)

Reference point: S = ESP after `PUSH EDI` at 0x0043f6d7.

**Setup**
- 0x0043f6d8: `MoveReq` local at S+0x30 is zeroed by FUN_0045e6a0 (9 dwords).
- 0x0043f6e7: `EDI = this->vtbl[+0x0c]()` = GetCollisionBlock. This recomputes and returns
  `&this+0x68`. **Early out:** if EDI==0, jump to 0x0043f7fd and return. This cannot happen for the
  player.
- Block layout relative to EDI = this+0x68:

  | offset | absolute | field |
  |---|---|---|
  | +0x00 | this+0x68 | start |
  | +0x10 | this+0x78 | mins |
  | +0x1c | this+0x84 | maxs |
  | +0x28 | this+0x90 | **displacement** |

**Sweep #1** (0x0043f6f4-0x0043f72d)
- Call: `TraceToFloor(start=this+0x68, mins, maxs, delta=this+0x90, impact=S+0x18, normal=S+0x0c)`.
- The impact and normal locals are zeroed first.
- So the sweep runs from the current origin to origin+disp. **Nothing checks whether disp is
  nonzero.** A zero disp gives a zero-length box sweep, which still calls the engine.
  - Whether a zero-length sweep can return 1 is engine behaviour (UNVERIFIED).

**Sweep #1 result**
- **Returns 0:** `JZ 0x0043f7fc`, return. **Think is never called and ClipVelocitySimple never
  runs.** PlayerRescueEnt then does `pos += disp` unchanged.
- **Returns 1:** zero S+0x24..S+0x2c (0x0043f73a-42), then enter the loop at 0x0043f750.

**Loop body** (0x0043f750-0x0043f7b5), with EBX = iteration count starting at 0. It builds the
MoveReq at S+0x30:

| MoveReq + | value | source |
|---|---|---|
| 0x00..0x08 | impact x, y, z | S+0x18..0x20, from the last sweep |
| 0x0c..0x14 | normal x, y, z | S+0x0c..0x14, from the last sweep. **+0x10 = normal.y** is what Think tests. |
| 0x18..0x20 | 0, 0, 0 | S+0x24..0x2c, zeroed once |
| 0x24, 0x28 | 0, 0 | immediates |
| 0x2c | this | |

Then `CALL [vtbl+0x38](&MoveReq)` = BModelEnt::Think. **Think's return value is ignored**: there is
no TEST after 0x0043f7b5.

**Loop control**
- 0x0043f7b8: `INC EBX ; CMP EBX,5 ; JGE 0x0043f7f0`.
- Otherwise run **sweep #n** (0x0043f7be-0x0043f7db): the same call as sweep #1, re-reading
  this+0x68 and this+0x90. These may have changed:
  - Think's commit rewrites this+0x68.
  - ClipVelocityToPlanes rewrites this+0x90.
  - The impact and normal locals are **not** re-zeroed, but a miss exits anyway.
- Sweep result:
  - **1:** `JNZ 0x0043f750`, loop again.
  - **0:** plain return at 0x0043f7e8-ef, with **no** ClipVelocitySimple.
- **After 5 Think calls** (0x0043f7f0): `ClipVelocitySimple(&MoveReq)`, where MoveReq still holds
  the 5th hit's impact and normal. Then return.
- So there are at most 5 sweeps and 5 Think calls, and ClipVelocitySimple runs **only when 5
  consecutive sweeps all returned 1**.

**ClipVelocitySimple 0x0049e040**
- `FLD MoveReq.normal.y ; FCOMP double [0x004de6f8]=0.5 ; TEST AH,1 ; JNZ`.
- **normal.y >= 0.5:** set `this+0x12c=1` (grounded) and return. **disp is left as is.**
- **normal.y < 0.5, or NaN** (unordered sets C0): `this+0x90 = (0,0,0)` and `this+0xfc = (0,0,0)`
  (via Vec3_Set 0x0042d280).

---

## 3. BModelEnt::Think 0x0043f810-0x0043fa06 (`__thiscall(this, MoveReq*)`, RET 4)

Reference point: T = ESP after `PUSH EDI` at 0x0043f820. The arg is `[T+0x80]`.

Register roles:
- ESI = this
- EBP = this+0x68 (start), set at 0x0043f8b6
- EBX = this+0x78 (mins)
- EDI = this+0x84 (maxs)

There are **3 exits**:
- **A**, 0x0043f9ee: ClipVelocityToPlanes(MoveReq), RET 4.
- **B**, 0x0043f924: ClipVelocityToPlanes(MoveReq), RET 4. Same as A.
- **C**, 0x0043f9b5..0x0043f9eb: the **commit**, RET 4 **without** any velocity clip.

The function is `void` in effect. EAX at return is junk (exit C leaves this+0x68 from
0x00441e90), and Depenetrate ignores it.

### 3a. Pre-step conditions

1. 0x0043f821: `this+0x13c = 0` always ("stepped" flag cleared on every call).
2. 0x0043f818/0x0043f828: **if byte `this+0x130` != 0, exit A.**
   - The flag is set only by a commit and cleared once per frame by vtbl+0x64.
   - So after one commit, every later Think in the same frame only clips.
3. 0x0043f82e-0x0043f843: `FLD float [MoveReq+0x10]` (normal.y), then
   `FCOMP double [0x004e0eb8]`. The constant there is bytes `cd cc cc cc cc cc ec 3f` =
   **0.9 (double 0x3FECCCCCCCCCCCCD)**. Then `FNSTSW AX ; TEST AH,0x41 ; JZ exit A`.
   - AH&0x41 tests C0|C3. The x87 compare outcomes:

     | case | flags | AH&0x41 |
     |---|---|---|
     | ST > src | C0=C3=0 | 0 |
     | ST < src | C0=1 | nonzero |
     | equal | C3=1 | nonzero |
     | unordered (NaN) | C0=C2=C3=1 | nonzero |

   - **normal.y > 0.9** (strictly, ordered): exit A, a floor-like plane, so just clip.
   - **normal.y <= 0.9, or NaN:** go to the step attempt. This covers walls, steep slopes,
     ceilings (negative Y), exactly 0.9, and NaN.

### 3b. ClipVelocityToPlanes 0x0049e0a0 (exits A and B)

The plane is always the **MoveReq plane**: Depenetrate's sweep result, never a step sweep's.

1. If `normal.y >= 0.5` (`[0x004de6f8]`, `TEST AH,1` pattern), set `this+0x12c=1`.
2. If `dot(this+0xfc, -N) > 0` (`[0x004dc50c]`=0.0, `TEST AH,0x41`), subtract the into-plane
   component from velocity this+0xfc.
3. Do the same for this+0x108 against this+0x120.
4. **Recompute this+0x90 (disp)** from velocity and dt `[0x00502b04]`:
   `disp = dt*vel + (0.5*dt*dt)*(+0x120)`. These semantics are from the decompile-level reading of
   0x00447710/0x00446390 and are UNVERIFIED.
5. It never writes this+0x4c or this+0x68.

### 3c. Step branch (only when +0x130==0 and normal.y <= 0.9 or NaN)

**Step-up sweep** (0x0043f849-0x0043f8ba)
- Zeroes impact_out (T+0x10..0x18) and normal_out (T+0x34..0x3c).
- Call: `TraceToFloor(start=this+0x68, mins, maxs, delta=T+0x28=(0, +[this+0x138]=16.0, 0), T+0x10, T+0x34)`.
- `TEST AL,AL ; JNZ 0x0043f9ee`: **a HIT aborts the whole step** (exit A, clip only). The impact is
  **not** used or clamped.
- Only a **MISS** continues.

**Raised point** (0x0043f8c7-0x0043f8d2)
- `Vec3_Add(T+0x4c, this+0x68, T+0x28)`, so raised = start + (0,16,0).
- It is computed from start plus the step vector. The step-up impact is not used (it is still zero,
  since the sweep missed).

**Forward sweep** (0x0043f8d7-0x0043f91b)
- delta = T+0x1c = (disp.x, **0**, disp.z), from this+0x90 with Y forced to 0 at 0x0043f90f.
- Call: `TraceToFloor(start=raised T+0x4c, mins, maxs, delta, T+0x10, T+0x34)`.
- `TEST AL,AL ; JZ 0x0043f93d`: a **HIT means exit B** (clip only, no step). A **MISS** continues.

**Step-down setup** (0x0043f93d-0x0043f985)
- fwdEnd T+0x64 = raised + (dx,0,dz) (Vec3_Add 0x0043f94c).
- T+0x40 = (0, -1.0 (0xbf800000), 0).
- Vec3_Negate 0x0042d2b0 of (0,16,0) goes to T+0x70 = (-0, -16, -0).
- Vec3_Add 0x0043f985: down delta T+0x58 = **(0, -(16+1), 0) = (0,-17,0)**.

**Step-down sweep** (0x0043f996-0x0043f9ac)
- Call: `TraceToFloor(start=fwdEnd = origin+(dx,16,dz), mins, maxs, delta=(0,-17,0), T+0x10, T+0x34)`.
- The sweep end is origin+(dx,-1,dz).
- `TEST AL,AL ; JZ 0x0043f9ee`: a **MISS means exit A** (clip, no commit). A **HIT means commit**.

**Commit** (0x0043f9b5-0x0043f9df)
- `this+0x130 = 1` and `this+0x13c = 1`.
- `this+0x4c..0x54 = T+0x10..0x18`: **exactly the step-down sweep's impact_out**, with no offset,
  epsilon or clamp.
- Then `0x00441e90(ECX=this+0x68, src=this+0x4c)` copies the new position into this+0x68.
- RET 4 with **no** velocity clip, so this+0x90 is unchanged.

**There is no check of any kind on the step-down result before the commit.**
- The step-down normal (T+0x34) is written by TraceToFloor but **never read** by Think.
- No normal.y test, no height-difference test (impact.y versus the start), no distance test, no
  Ratio test, no contents test.
- The **only** gate is TraceToFloor's AL, i.e. geWorld_Collision != 0.

---

## 4. Key question: the complete condition set for a stale impact to land in player+0x4c

The only player-path write of this+0x4c inside Depenetrate/Think is the commit at 0x0043f9c7. For
the step-down sweep's (stale) Col.Impact to be committed, **all** of the following must hold in one
Depenetrate call:

1. **PlayerRescueEnt gate.** `entity+0x380` (spectator) == 0 at 0x004537d5, so Depenetrate is
   called. vtbl+0x64 has just cleared +0x130 and recomputed disp.
2. **Depenetrate sweep (iteration k, 1 <= k <= 5) returns 1.** The sweep is origin to origin+disp.
   This can be a fresh hit or a stale one. Any disp, including zero, qualifies; nothing requires it
   to be nonzero.
   - Every earlier iteration in this call must also have returned 1 (a 0 ends the loop).
   - No earlier iteration may have committed (see 3).
3. **Latch:** `this+0x130 == 0`, so no commit yet this frame. This means the commit is necessarily
   the **first and only** commit of the frame.
4. **That sweep's plane normal.y is NOT > 0.9:** normal.y <= 0.9, or NaN. A stale plane with
   normal.y > 0.9 (a flat floor) blocks the step path entirely and only clips.
5. **Step-up sweep returns 0 (MISS):** origin to origin+(0,16,0).
6. **Forward sweep returns 0 (MISS):** origin+(0,16,0) to origin+(dx,16,dz), using the current
   this+0x90. That is either the frame's integrated disp or the disp after the earlier iterations'
   ClipVelocityToPlanes.
7. **Step-down sweep returns 1 (HIT):** origin+(dx,16,dz) to origin+(dx,-1,dz). Its impact is
   whatever geWorld_Collision copies into Col, which is **the stale globals if the hit is stale**.

There are no conditions on:
- the step-down plane's normal
- the step-down impact's height or distance from the origin
- Ratio
- contents
- whether disp is nonzero

### 4a. "Player in solid, every sweep this frame returns the same stale impact+plane"

1. vtbl+0x64 clears +0x130. Sweep #1 returns 1 (stale) with stale normal N.
2. **If N.y > 0.9:** every Think takes exit A, i.e. ClipVelocityToPlanes with N. That sets
   grounded, removes the into-plane velocity, and rewrites disp.
   - Sweep #2..#5 also return stale 1, giving 5 Thinks, then ClipVelocitySimple(N).
   - N.y >= 0.5 sets grounded and **keeps disp**.
   - Back in PlayerRescueEnt, `pos += disp`. **No commit to the stale impact.**
3. **If N.y <= 0.9 (or NaN):** Think tries the step.
   - **Step-up returns stale 1, which is a HIT, so exit A immediately.**
   - Forward and step-down never run. **No commit.**
   - This repeats on iterations 2-5 (latch still 0, same stale N). Then ClipVelocitySimple(N):
     - N.y < 0.5 or NaN: **disp and velocity are zeroed**, so the player freezes in place.
     - 0.5 <= N.y <= 0.9: disp is kept, and `pos += disp` follows.

**So under "all sweeps stale" the Think commit can never fire**, because the step-up hit aborts it.
A warp through 0x0043f9c7 requires the three step sweeps to **disagree**: step-up MISS and forward
MISS (no hit reported at all), and then step-down HIT returning stale data.
- For example, the step-down start origin+(dx,16,dz) is reported in solid while the forward sweep
  ending at that same point reported no hit.
- Or the step-down box path hits a model or brush the others didn't.
- (Analysis, UNVERIFIED at runtime: what makes the engine's box path return stale on one sweep but
  0 on its neighbours is engine-side; see HOOK_SITES section 3a, 0x100371c7/0x100371c9.)

Where the stale value comes from (inference, UNVERIFIED at runtime):
- Within this Think, step-up and forward missed, so they wrote nothing.
- The globals therefore hold the **last fresh write before the step-down**. That is Depenetrate's
  sweep for this iteration if that sweep was fresh, which puts it near the player (low risk).
- If that sweep was itself stale, the value is older still: an earlier frame's player trace, or a
  **render-phase engine trace from the previous frame**. Those are the actor shadow and lighting
  rays and the lightmap rays, which write the same e9c/f48 globals; see HOOK_SITES section 3c.
- So the commit can be an arbitrary far-away point.

### 4b. Exact committed value and what happens after

- **At 0x0043f9cc-d9 the committed position is exactly the stale Col.Impact** (copied verbatim from
  0x10362e9c..ea4 by geWorld_Collision, then by TraceToFloor). There is no offset.
  - "Impact" here is the engine's box-path impact, which is model space transformed to world
    (HOOK_SITES section 3b).
- After the commit, in the same frame:
  1. this+0x68 = impact.
  2. If k < 5, Depenetrate re-sweeps from the **new** origin by the unchanged disp.
     - A hit: further Thinks see +0x130=1 and only clip (ClipVelocityToPlanes rewrites disp). If
       all 5 iterations are used, ClipVelocitySimple runs and may zero disp.
     - A miss: return immediately.
  3. PlayerRescueEnt 0x004537e0-0x00453815 then does `pos += disp`.
- **So the end-of-frame position (HOOK_SITES hook H6, 0x00453816) is stale impact + the remaining
  disp**, not the exact impact. It equals the impact only when disp is zero or was zeroed (wall
  plane with normal.y < 0.5 hit 5 times).
- The H5 hook at 0x0043f9c7 sees the exact impact.

### 4c. Other player position writes not covered here

- ModelEntity::SnapToFloor 0x0049da00 (via ResetLocomotionState, the Verb_Reset path) also traces
  through TraceToFloor and may commit. It was **not analysed in this pass** (UNVERIFIED).
- The `pos += disp` itself never consults a trace.
