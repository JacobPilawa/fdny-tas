# Collision "last hit" logger -- hook sites (Genesis.dll + FDNYFirefighter.exe)

Research date 2026-10-16. Read-only. Everything below was taken from instruction bytes, not from
the decompiler alone, unless it is marked **UNVERIFIED** or **(decompile)**.

**How this was obtained.**
- **Genesis.dll:** the copy in the Ghidra project is **unanalyzed (0 functions)**. To get xrefs and
  function bodies I imported a *separate copy* of `FDNY SP 1.1\Genesis.dll` into a throwaway headless
  project in the session scratchpad and ran auto-analysis there. Nothing was written to the real
  project except one side effect: an early `disassemble_at 0x10001e30` (5 bytes) created a single
  instruction code unit in the live Genesis DB. It added no names, comments or patches.
- **FDNYFirefighter.exe:** bytes and listings come from the live GUI DB through MCP (read-only
  calls), plus a read-only headless run on a file copy of the project.
- **The on-disk exe DB is behind the GUI session.** In the copy, 0x00492ce0 is still `FUN_`, and
  0x0043f810 and 0x00452570 have no function at all. The labeling loop's names exist only in the open
  CodeBrowser, so they are not saved yet. Someone should save the project.

Image bases: exe 0x00400000 (no relocations needed). Genesis 0x10000000 is the preferred base.
**Genesis has relocations.** If it loads elsewhere, rebase every Genesis address by
`actual_base - 0x10000000`. Any absolute operands inside displaced bytes are already relocated in
live memory, so a cave must **copy the displaced bytes from the live process** rather than from
this file.

Every function involved is **FPO (no EBP frame chain)**, in the engine and in the exe. See section 6.

---

## 1. geWorld_Collision

### 1a. The export is a 5-byte thunk

```
10001e30  e9 db 6d 03 00        JMP 0x10038c10        ; geWorld_Collision export (ordinal 504) = ILT thunk
```

The real body is **FUN_10038c10** (body 0x10038c10-0x10038dc2).

Who uses what:
- The exe's IAT slot 0x004dc0bc points at the thunk.
- Engine-internal callers CALL 0x10038c10 directly and skip the thunk:
  - 0x1001224e: actor shadow, FUN_100121d0
  - 0x10002fd0: geSound3D_GetConfig, a sound-occlusion ray
  - 0x10039162: geWorld_TestModelMove body FUN_10038dd0

So:
- **To see only exe callers:** patch the thunk. Its 5 bytes are a relative JMP. The cave must end with
  `JMP 0x10038c10` (rel32 recomputed).
- **To see every caller:** patch the body prologue.

### 1b. Body prologue (patch site, no relative branches or calls)

```
10038c10  83 ec 24              SUB  ESP,0x24
10038c13  8b 4c 24 28           MOV  ECX,[ESP+0x28]        ; World
10038c17  8b 51 18              MOV  EDX,[ECX+0x18]
10038c1a  8b 44 24 3c           MOV  EAX,[ESP+0x3c]        ; Contents
10038c1e  81 c2 c8 00 00 00     ADD  EDX,0xc8
10038c24  56                    PUSH ESI
10038c25  8b 74 24 54           MOV  ESI,[ESP+0x54]        ; ESI = Col* for the rest of the function
10038c29  a3 5c 2f 36 10        MOV  [0x10362f5c],EAX      ; g_contentsMask = Contents
10038c2e  33 c0                 XOR  EAX,EAX
10038c30  89 15 1c 2f 36 10     MOV  [0x10362f1c],EDX
```

**Recommended entry patch:** 0x10038c10, 7 bytes `83ec24 8b4c2428` (SUB, MOV). Both are position
independent. For a 5-byte JMP plus 2 NOPs, re-emit both in the cave, then `JMP 0x10038c17`.

### 1c. Arguments (cdecl, caller cleans 0x2c)

Offsets are from ESP at entry, with [esp] = return address. The Genesis 1.x prototype matches the
code.

| ESP+ | arg | evidence |
|---|---|---|
| 0x04 | `geWorld *World` | `[World+0x18]+0xc8` -> 0x10362f1c (BSP) |
| 0x08 | `const geVec3d *Mins` | Mins==0 or Maxs==0 selects the ray path |
| 0x0c | `const geVec3d *Maxs` | |
| 0x10 | `const geVec3d *Front` (start) | |
| 0x14 | `const geVec3d *Back` (end) | |
| 0x18 | `uint32 Contents` | stored to 0x10362f5c (the exe passes 0x43, or 0x01 for FloorMe, TraceGroundHeight and SweepWorldBox) |
| 0x1c | `uint32 CollideFlags` | bit 4 = actors (0x100372cc), bit 2 = models (0x10037420), bit 8 = world model only, stop loop (0x100377ce). The exe always passes 2, or 0/2 in SegmentClearLOS. |
| 0x20 | `uint32 UserFlags` | |
| 0x24 | `GE_CollisionCB *CB` | called at 0x100374a4 |
| 0x28 | `void *Context` | |
| 0x2c | `GE_Collision *Col` | the out struct |

Mode selection:
- `Mins && Maxs`: **box path**. Increments 0x10362f28, then calls FUN_100372a0 (box sweep) at 0x10038ca3.
- Otherwise: **ray path**. Increments 0x10362ec4, then calls FUN_10038470 (ray sweep) at 0x10038d52.

### 1d. GE_Collision layout (confirmed from the writes at 0x10038cb3-0x10038d02 and from the exe's reads)

| off | field | written from |
|---|---|---|
| +0x00 | Model (`geWorld_Model*`) | inner out-param (`[ESP+0x44]` local) |
| +0x04 | Mesh (per the Genesis 1.x header; UNVERIFIED semantics) | inner out-param, zeroed on entry |
| +0x08 | Actor (per the Genesis 1.x header; UNVERIFIED semantics) | inner out-param, zeroed on entry |
| +0x0c | Impact x,y,z | sweep's local impact copy |
| +0x18 | Ratio | **read straight from global 0x10362e94** (0x10038cef / 0x10038da1) |
| +0x1c | Plane.Normal x,y,z | sweep's local plane copy |
| +0x28 | Plane.Dist | |

Only +0, +4 and +8 are zeroed on entry. Impact, Ratio and Plane are written **only when the function
returns 1**.

How the exe reads it, which confirms the offsets:
- TraceToFloor: Col at local `[ESP+0x34]`, impact at `[ESP+0x40]` (+0xc), normal at `[ESP+0x50]` (+0x1c).
- TraceGroundHeight: Col = 0x005031b0, reads 0x005031c0 (Impact.y).
- roc_SweepWorldBox: Col = 0x005031f4, reads 0x00503200 (Impact) and 0x00503210 (Normal).

### 1e. Every return path of FUN_10038c10 (all plain `RET`, opcode C3, no imm16)

| RET at | path | EAX | 5-byte relocatable tail you can hook |
|---|---|---|---|
| **0x10038d0e** | box hit | 1 | 0x10038d0a `5e 83c424 c3` (POP ESI; ADD ESP,0x24; RET) |
| **0x10038dba** | ray hit | 1 | 0x10038db6 `5e 83c424 c3` |
| **0x10038dc2** | miss (either path) | 0 | 0x10038dbe `5e 83c424 c3`. 0x10038dbb is the JZ target, so start at 0x10038dbe. |

State at any of the three tail hooks:
- EDI is already popped. ESI = `Col*`. EAX = result.
- `[esp]` = saved ESI, `[esp+0x28]` = return address, `[esp+0x2c]` = World, ..., `[esp+0x54]` = Col.
- The cave re-emits `POP ESI; ADD ESP,0x24; RET`, so no JMP back is needed.

The inner sweeps also return through plain RETs:
- FUN_100372a0 (box sweep): RET 0x10037893 (1) and 0x100378a0 (0).
- FUN_10038470 (ray sweep): RET 0x100389d1 (1) and 0x100389de (0).

---

## 2. The process-wide globals (sizes and layout)

| addr | size | meaning |
|---|---|---|
| 0x10362e90 | 4 | **box-path hit flag.** Set to 1 at 0x100371bd (fresh) and 0x100371d1 (no-write hit, see section 3). Reset to 0 per model at 0x10037508 (box sweep), 0x10037ac0 (ModelCollision) and 0x1003900b (TestModelMove). **The ray path never touches it.** |
| 0x10362e94 | 4 (float) | "Ratio" copied to Col+0x18. Box path (0x1003717a): it is the **sqrt distance from the model-space sweep start (0x10362ecc..ed4) to the impact**, not a 0..1 fraction. Ray path (0x1003662d): it is the split fraction `[ESP+0x10]` *of the current recursion sub-segment* (computed at 0x10036550), not of the whole ray. (asm-derived, semantics UNVERIFIED) |
| 0x10362e9c / ea0 / ea4 | 3x4 (float) | Impact x / y / z |
| 0x10362f48 | **20 bytes** | Plane as the BSP's GFX_Plane: Normal x,y,z at f48/f4c/f50, Dist at **f54**, Type (int) at **f58**. Every copy into or out of it is `MOVSD.REP` with ECX=5: 0x10037198, 0x100365fd, 0x100377b4, 0x100388ba. The BSP plane array stride is 0x14 (0x100365e9). Only the first 16 bytes reach Col->Plane. |
| 0x10362f44 | 4 | "hit already found" latch. Set to 1 by both clip routines. Reset per leaf (0x10037271), per ray model (0x1003870a) and per lighting model (0x10038afb). **It gates whether the ray clip writes at all** (0x100365cd). |
| 0x10362f18 | 4 (float) | best (closest) box distance so far. Init 9999.0 (0x461c3c00) per model. |
| 0x10362f5c | 4 | contents mask for the current trace. **Not part of the plane.** |
| 0x10362f60 / f64 | 4 / 4 | ray hit node index / side. f64 != 0 makes FUN_10038470 **negate its local plane copy and set Type=6** (0x100388be). The global is not negated, so for ray traces Col->Plane can be the negation of global f48. |
| 0x10362ec8 | 4 | ray-hit companion value (read back by 100389e0 into its param 5) |

Confirmed: impact 0x10362e9c/ea0/ea4 (3 floats), plane 0x10362f48 (20 bytes), ratio 0x10362e94
(float), hit flag 0x10362e90 (dword).

---

## 3. Every write to impact (e9c..ea4) and plane (f48..f58)

### 3a. Fresh writes: the low-level clip routines

**Box clip, FUN_10036ea0** (recursive BSP box clip). This is the fresh-hit epilogue, entered by
falling through from 0x1003716c, which only runs when the new hit is closer than f18 *and* f44 == 0.

```
1003716c  8b 4c 24 14           MOV ECX,[ESP+0x14]          ; impact.y
10037170  d9 15 18 2f 36 10     FST  [0x10362f18]           ; best dist
10037176  8b 44 24 10           MOV EAX,[ESP+0x10]          ; impact.x
1003717a  d9 1d 94 2e 36 10     FSTP [0x10362e94]           ; RATIO (=distance)
10037180  8b 54 24 18           MOV EDX,[ESP+0x18]          ; impact.z
10037184  89 0d a0 2e 36 10     MOV [0x10362ea0],ECX        ; IMPACT.Y
1003718a  b9 05 00 00 00        MOV ECX,5
1003718f  8d 74 24 28           LEA ESI,[ESP+0x28]
10037193  bf 48 2f 36 10        MOV EDI,0x10362f48
10037198  f3 a5                 REP MOVSD                   ; PLANE (20 bytes)
1003719a  5f 5e                 POP EDI / POP ESI
1003719c  a3 9c 2e 36 10        MOV [0x10362e9c],EAX        ; IMPACT.X
100371a1  b8 01 00 00 00        MOV EAX,1
100371a6  5d                    POP EBP
100371a7  89 1d 10 2f 36 10     MOV [0x10362f10],EBX
100371ad  89 15 a4 2e 36 10     MOV [0x10362ea4],EDX        ; IMPACT.Z
100371b3  c7 05 44 2f 36 10 01 00 00 00   MOV [0x10362f44],1
100371bd  a3 90 2e 36 10        MOV [0x10362e90],EAX        ; HIT FLAG = 1
100371c2  5b                    POP EBX                     ; <-- HOOK: 100371c2 "5b 83c42c c3" (5 bytes)
100371c3  83 c4 2c              ADD ESP,0x2c
100371c6  c3                    RET
```

So the KB's list "0x10037184 / 0x10037198 / 0x1003719c / 0x100371bd" breaks down as:
- 0x10037184 = Impact.y
- 0x10037198 = Plane (REP MOVSD)
- 0x1003719c = Impact.x
- 0x100371bd = hit flag
- The KB omitted **Impact.z at 0x100371ad** and **Ratio at 0x1003717a**.

Values are in **model space** (for the world model that is world space). The sweep later
re-transforms them (section 3b).

The **hit-with-no-write** exit of the same function:

```
100371c7  dd d8                 FSTP ST0          ; from 0x10037161 (JP: not closer than best) and 0x1003716a (f44 already set)
100371c9  5f 5e                 POP EDI/ESI       ; <- also the JNZ target at 0x100370f7 (a child recursion returned 1)
100371cb  b8 01 00 00 00        MOV EAX,1
100371d0  5d                    POP EBP
100371d1  a3 90 2e 36 10        MOV [0x10362e90],EAX   ; flag=1, NO impact/plane write
100371d6  5b 83 c4 2c c3        POP EBX; ADD ESP,0x2c; RET   ; hookable 5-byte tail, but recursion
                                                             ; unwinds through here once per level -> overcounts
100371db  5f 5e 5d 33 c0 5b 83 c4 2c c3                      ; return 0
```

Other sites:
- 0x10036eab-0x10036eb0 is an early `return 0`.
- 0x10036ea0 prologue: `8b442414 83ec2c` (7 bytes).
- Callers of FUN_10036ea0: FUN_100371f0 at 0x1003728c (leaf box test, resets f44 at 0x10037271
  first), plus self-recursion at 0x10036fff, 0x10037036, 0x100370ed and 0x1003710d.

The KB says this path is the "starts in solid" stale-hit return. **UNVERIFIED here:** the
asm shows 0x100371c7 is also reached for "a farther hit than the best so far", and 0x100371c9 for
recursion unwind. Section 7 gives a stale detector that does not depend on which one it is.

**Ray clip, FUN_100363f0** (recursive BSP ray clip). The fresh write happens only when
`[0x10362f44]==0` (0x100365cd-0x100365d4).

```
100365d6  a1 1c 2f 36 10        MOV EAX,[0x10362f1c]
...
100365e5  8b 44 24 14           MOV EAX,[ESP+0x14]        ; impact.x (split point)
100365ef  8b 54 24 1c           MOV EDX,[ESP+0x1c]        ; impact.z
100365f3  b9 05 00 00 00        MOV ECX,5
100365f8  bf 48 2f 36 10        MOV EDI,0x10362f48
100365fd  f3 a5                 REP MOVSD                 ; PLANE (20 bytes, from BSP plane array)
100365ff  8b 4c 24 18           MOV ECX,[ESP+0x18]        ; impact.y
10036603  a3 9c 2e 36 10        MOV [0x10362e9c],EAX      ; IMPACT.X
10036608  8b 44 24 2c           MOV EAX,[ESP+0x2c]
1003660c  89 0d a0 2e 36 10     MOV [0x10362ea0],ECX      ; IMPACT.Y
10036612  8b 4c 24 10           MOV ECX,[ESP+0x10]        ; fraction
10036616  89 1d 60 2f 36 10     MOV [0x10362f60],EBX
1003661c  89 2d 64 2f 36 10     MOV [0x10362f64],EBP
10036622  89 15 a4 2e 36 10     MOV [0x10362ea4],EDX      ; IMPACT.Z
10036628  a3 c8 2e 36 10        MOV [0x10362ec8],EAX
1003662d  89 0d 94 2e 36 10     MOV [0x10362e94],ECX      ; RATIO
10036633  c7 05 44 2f 36 10 01 00 00 00   MOV [0x10362f44],1   ; <-- HOOK here (10 bytes, absolute operand only;
                                                               ;     not a branch target)
1003663d  5d 5b 5f              POP EBP/EBX/EDI           ; branch target (0x100365a1, 0x100365d4) -- do not cover
10036640  b8 01 00 00 00 5e 83 c4 10 c3                     ; return 1
1003664a  5d 5b 5f 33 c0 5e 83 c4 10 c3                     ; return 0
```

Ray-path cave: re-emit the 10 live bytes from 0x10036633, log, then `JMP 0x1003663d`.
FUN_100363f0's prologue is `83ec10 56 57 8b7c2424` (SUB, PUSH, PUSH, MOV = 9 bytes).

### 3b. Re-writes by the sweeps (model space to world space; same hit, new value)

**Box sweep FUN_100372a0.** After FUN_100371f0 (0x10037687) returns with e90 == 1:
- 0x100376a7: CALL 0x10001180(xform, &f48, &f48) rotates the **Plane.Normal in place**.
- 0x100376bf / 0x100376cf / 0x100376de: impact -= model origin.
- 0x100376fb / 0x10037708 / 0x10037715: impact = rotated + origin (**world-space impact**).
- 0x10037743: **Plane.Dist** = dot(N, impact).
- 0x100377a7-0x100377b4 then copies the plane out to the caller.

**Ray sweep FUN_10038470.** After FUN_100363f0 (0x10038785) returns 1:
- 0x100387a0: CALL 0x10001180 (plane normal, in place).
- 0x100387b9 / c8 / d7: impact -= origin.
- 0x100387dd: CALL 0x100010f0(xform, &e9c, &e9c) rotates the **impact in place**.
- 0x100387f6 / 0x10038805 / 0x10038814: impact += origin.
- 0x10038842: Plane.Dist.

Not reachable from the exe. The KB says the exe does not import these exports; not re-verified here.
- **FUN_100378b0** (geWorld_ModelCollision, via FUN_10037c10 at 0x10037d2c) writes impact at
  0x10037b07/1a/2c, 0x10037b48/58, 0x10037ba4/bc2/bd6, and Dist at 0x10037b8c. Plane in place via
  0x10037ae1/e6 pushes.
- **FUN_10038dd0** (geWorld_TestModelMove body) writes impact at 0x100390ae/0x100390e2/0x10039104
  and 0x1003912c/44/5c, and Dist at 0x10039115. It calls 0x10038c10 at 0x10039162.

### 3c. Engine-internal traces: what path each takes

| writer | path into the globals | call sites |
|---|---|---|
| **FUN_100121d0, actor shadow.** Called from FUN_10012f80 at 0x100137ac, which is called from geActor_Render 0x10009210 at 0x1000927a / 0x100092c4. | **Calls FUN_10038c10 directly** at **0x1001224e** (ray: Mins=Maxs=0, Contents 0x43, flags 2), then FUN_10038470, then FUN_100363f0. Bypasses the export thunk, so an entry hook at 0x10038c10 sees it (return address 0x10012253). | 0x1001224e |
| **FUN_10011940, actor lighting.** Called from FUN_10012810 at 0x10012a9b (from geActor_RenderThroughFrustum at 0x100091cf) and FUN_10012f80 at 0x10013583. | **Does not use geWorld_Collision.** Calls FUN_100389e0 at **0x100119bf** and **0x10011c29**. FUN_100389e0 forces contents 0x43, sets f60=-1, resets f44=0 per model (0x10038afb), and calls FUN_100363f0 at **0x10038b6c**. So it **does** perform fresh ray-clip writes of impact and plane, which only the 0x10036633 hook catches. Its results are not transformed back. 0x10011c29 is gated by `[light+0x78]==1` (decompile); 0x100119bf's gating is UNVERIFIED. | 0x100119bf, 0x10011c29, 0x10038b6c |
| **FUN_10038290, dynamic-light lightmap rays.** Called from FUN_10032b00 at 0x10032ce1, which is called from FUN_10033d60 at 0x10033f77. FUN_10033d60 is called from FUN_10011940 at 0x10011a45, FUN_10025770 at 0x100257db, and FUN_10025e00 (data ref 0x10025fa4). | **Calls FUN_100363f0 directly** at 0x1003837e, 0x100383b4, 0x10038421 and 0x1003844e. Sets contents 0x43 and **never resets f44**, so it writes impact and plane only if f44 is still 0 from whatever ran before. (decompile+asm; runtime UNVERIFIED) | 0x1003837e, 0x100383b4, 0x10038421, 0x1003844e |
| geSound3D_GetConfig 0x10002ee0 (export). Sound occlusion. | Calls 0x10038c10 at 0x10002fd0 (ray, 0x43, 2). The exe does not import it (per the KB); not checked whether fmod or anything else does. | 0x10002fd0 |

Consequence for hook design:
- An entry/exit hook on 0x10038c10 sees the exe callers, the shadow traces and sound.
- Actor lighting and lightmap rays are visible only from the **low-level ray-clip hook at 0x10036633**.
- The box clip is reachable only through the box sweep or ModelCollision, so 0x100371c2 sees only
  geWorld_Collision box traces from the exe in this game.
- Render-time engine traces run **after** TickSubsystems in each frame (section 5).

---

## 4. FDNYFirefighter.exe: player position commit

### 4a. BModelEnt::Think 0x0043f810 (body 0x0043f810-0x0043fa06, `__thiscall`, RET 4)

- Prologue: `83ec6c 53 55 56 8bf1 8a8630010000 ...`
- **`this` = ESI** (`MOV ESI,ECX` at 0x0043f816).
- EBP = this+0x68 (sweep start, which equals the origin).
- EBX = this+0x78 (mins), EDI = this+0x84 (maxs).

The three TraceToFloor calls:

| call | ret addr | role |
|---|---|---|
| 0x0043f8ba | 0x0043f8bf | step-up |
| 0x0043f91b | 0x0043f920 | forward |
| 0x0043f9ac | 0x0043f9b1 | step-down |

Step-down call and commit:

```
0043f996  8d 44 24 34           LEA EAX,[ESP+0x34]   ; normal_out
0043f99a  50                    PUSH EAX
0043f99b  8d 54 24 14           LEA EDX,[ESP+0x14]   ; impact_out (= [ESP+0x10] after the call)
0043f99f  52                    PUSH EDX
0043f9a0  8d 44 24 60           LEA EAX,[ESP+0x60]   ; displacement
0043f9a4  50 57 53              PUSH EAX / PUSH EDI(maxs) / PUSH EBX(mins)
0043f9a7  8d 54 24 78           LEA EDX,[ESP+0x78]   ; start
0043f9ab  52                    PUSH EDX
0043f9ac  e8 2f 33 05 00        CALL 0x00492ce0      ; TraceToFloor (RET 0x18)
0043f9b1  84 c0                 TEST AL,AL
0043f9b3  74 39                 JZ   0x0043f9ee
0043f9b5  8b 54 24 10           MOV EDX,[ESP+0x10]   ; impact.x
0043f9b9  b0 01                 MOV AL,1
0043f9bb  88 86 30 01 00 00     MOV [ESI+0x130],AL
0043f9c1  88 86 3c 01 00 00     MOV [ESI+0x13c],AL
0043f9c7  8d 46 4c              LEA EAX,[ESI+0x4c]   ; <== COMMIT. HOOK HERE: 0x0043f9c7, 7 bytes "8d464c 8bc8 8911"
0043f9ca  8b c8                 MOV ECX,EAX
0043f9cc  89 11                 MOV [ECX],EDX        ; this+0x4c = impact.x
0043f9ce  8b 54 24 14           MOV EDX,[ESP+0x14]
0043f9d2  89 51 04              MOV [ECX+4],EDX      ; this+0x50 = impact.y
0043f9d5  8b 54 24 18           MOV EDX,[ESP+0x18]
0043f9d9  89 51 08              MOV [ECX+8],EDX      ; this+0x54 = impact.z
0043f9dc  50                    PUSH EAX
0043f9dd  8b cd                 MOV ECX,EBP
0043f9df  e8 ac 24 00 00        CALL 0x00441e90      ; sync this+0x68 from this+0x4c
0043f9e4  5f 5e 5d 5b 83 c4 6c c2 04 00
```

State at the hook 0x0043f9c7. The old position is still in place, and no instruction in
0x0043f9c7-0x0043f9cd is a branch target.

| item | location |
|---|---|
| `this` | ESI |
| new impact | EDX = x; `[ESP+0x10]`, `[ESP+0x14]`, `[ESP+0x18]` = x, y, z |
| hit normal | `[ESP+0x34]`, `[ESP+0x38]`, `[ESP+0x3c]` (normal.Y = `[ESP+0x38]`) |
| old position | `[ESI+0x4c]` .. `[ESI+0x54]` |

The cave re-emits the 7 bytes and JMPs to 0x0043f9ce.

Alternative pre-commit site: 0x0043f9b5 `8b542410 b001` (6 bytes, EDX not live yet).

`this` here is any BModelEnt. Compare ESI to the player pointer (section 7). The only caller of
DepenetrateFromWorld is PlayerRescueEnt::Think, but Think is a virtual (vtable+0x38), so it is
dispatched only through DepenetrateFromWorld 0x0043f7b5 **as far as the static xrefs show**
(UNVERIFIED for other dynamic callers).

### 4b. PlayerRescueEnt::Think 0x00452570, the `pos += disp` span

PlayerRescueEnt::Think body is 0x00452570-0x00453a68 (SEH frame, single RET at 0x00453a68).
- **ECX/ESI = entity+0x140** (secondary base).
- **EDI = entity base** (`LEA EDI,[ESI-0x140]` at 0x004537aa).

```
004537d5  84 c0                 TEST AL,AL                  ; AL = [ESI+0x240] = entity+0x380 spectator
004537d7  75 07                 JNZ  0x004537e0
004537d9  8b cf                 MOV  ECX,EDI
004537db  e8 f0 be fe ff        CALL 0x0043f6d0             ; DepenetrateFromWorld(entity)  ret 0x004537e0
004537e0  d9 86 50 ff ff ff     FLD  [ESI-0xb0]             ; entity+0x90 disp.x     <- branch target; 6-byte relocatable hook site
004537e6  d8 86 0c ff ff ff     FADD [ESI-0xf4]             ; entity+0x4c pos.x
004537ec  d9 9e 0c ff ff ff     FSTP [ESI-0xf4]
004537f2  d9 86 54 ff ff ff     FLD  [ESI-0xac]             ; disp.y
004537f8  d8 86 10 ff ff ff     FADD [ESI-0xf0]
004537fe  d9 9e 10 ff ff ff     FSTP [ESI-0xf0]             ; pos.y
00453804  d9 86 58 ff ff ff     FLD  [ESI-0xa8]             ; disp.z
0045380a  d8 86 14 ff ff ff     FADD [ESI-0xec]
00453810  d9 9e 14 ff ff ff     FSTP [ESI-0xec]             ; pos.z  (last byte at 0x00453815)
00453816  d9 44 24 14           FLD   [ESP+0x14]            ; <- post-commit hook: 10 bytes "d9442414 dc1d101c4e00"
0045381a  dc 1d 10 1c 4e 00     FCOMP qword [0x004e1c10]    ;    (absolute operand only; not a branch target)
00453820  df e0 f6 c4 41 ...
```

Hook options:
- **0x004537e0** (6 bytes): sees the position after Depenetrate/Think and before `+= disp`.
- **0x00453816** (10 bytes): sees the final per-frame position.

In both, EDI = player entity base. 0x004537e0 is a branch target, which is fine because the JMP
starts exactly there.

---

## 5. Once-per-frame tick hook (GameSession::RunFrame 0x0042aea0, body 0x0042aea0-0x0042afb1)

Per the frame-loop KB article this is the PumpPendingState slot. Each frame it runs TickSubsystems
(simulation), then ROCRender::RenderFrame (render). Render is where the actor lighting and shadow
traces run.

```
0042aea0  56                    PUSH ESI                 ; ENTRY hook: 9 bytes "56 8bf1 8a868c060000"
0042aea1  8b f1                 MOV  ESI,ECX
0042aea3  8a 86 8c 06 00 00     MOV  AL,[ESI+0x68c]      ;  (cave re-emits, JMP 0x0042aea9)
0042aea9  84 c0 74 13 ...
...
0042aedf  74 17                 JZ   0x0042aef8          ; [ESI+0x64c] != 0 -> early-out path (JMP 0x004a0660 at 0x0042aef3)
0042af08  8b ce                 MOV  ECX,ESI
0042af0a  e8 11 e6 ff ff        CALL 0x00429520          ; GameSession::TickSubsystems (rel32 -- avoid)
0042af0f  a1 78 3a 50 00        MOV  EAX,[0x00503a78]    ; POST-TICK / PRE-RENDER hook: exactly 5 bytes, absolute only
0042af14  8b 48 04              MOV  ECX,[EAX+4]
0042af17  6a 00                 PUSH 0
0042af19  e8 52 97 06 00        CALL 0x00494670          ; ROCRender::RenderFrame
```

Recommendation:
- **0x0042aea0** (9 bytes) to count frames. It runs even on the [ESI+0x64c] early-out path, so tick
  and render are not both guaranteed in that case.
- **0x0042af0f** (5 bytes) as the "simulation done, render starts" marker. Tagging each logged write
  as sim-phase or render-phase splits gameplay traces from engine render traces.

Both are relocatable. Neither contains a branch target (0x0042af08 is the target and lies outside).

---

## 6. Caller classification (everything that reaches IAT 0x004dc0bc)

### 6a. Direct IAT callers (all `CALL [0x004dc0bc]` = `ff15bcc04d00`, 6 bytes)

| wrapper / function | body | engine call | ret addr | Col struct | contents / flags |
|---|---|---|---|---|---|
| ROCRender::TraceToFloor | 0x00492ce0-0x00492dee (RET 0x18) | 0x00492da0 | **0x00492da6** | stack `[ESP+0x34]` | 0x43 / 2 |
| World::TraceGroundHeight | 0x0048ff30-0x0048fff3 (cdecl) | **0x0048ffc0** | **0x0048ffc6** | 0x005031b0 | 0x01 / 2, Mins=Maxs=0 (ray) |
| roc_SegmentClearLOS | 0x0048fea0-0x0048ff2f (cdecl) | **0x0048ff21** | **0x0048ff27** | 0x0050316c | 0x43 / (arg4 ? 2 : 0), ray |
| roc_SweepWorldBox | 0x00490000-0x00490125 (cdecl) | 0x004900d8 | **0x004900de** | 0x005031f4 | 0x01 / 2 |
| FireRescueEnt::FloorMe | 0x00448630-0x0044896e | 0x004486ed | **0x004486f3** | 0x00502bec | 0x01 / 2, ray |
| HoseGfx::RebuildGeometry | 0x0046c5a0-0x0046d1a0 | 0x0046cbd2 | **0x0046cbd8** | 0x00502e80 (pushed earlier) | see the function |

**KB correction:** 0x0048ff21 belongs to **roc_SegmentClearLOS**, not TraceGroundHeight.
TraceGroundHeight's only engine call is 0x0048ffc0.

### 6b. Callers of the wrappers. "ret" is the return address pushed by the wrapper call.

**ROCRender::TraceToFloor 0x00492ce0 (10 call sites):**

| class | function | body | call | ret |
|---|---|---|---|---|
| **player** | BModelEnt::DepenetrateFromWorld | 0x0043f6d0-0x0043f803 | 0x0043f72d / 0x0043f7db | 0x0043f732 / 0x0043f7e0 |
| **player** | BModelEnt::Think (step-up / forward / step-down) | 0x0043f810-0x0043fa06 | 0x0043f8ba / 0x0043f91b / 0x0043f9ac | 0x0043f8bf / 0x0043f920 / **0x0043f9b1** |
| **fire** | FireRescueEnt::TrySpawnSpreadFire | 0x00447a90-0x0044806c | 0x00447cca | 0x00447ccf |
| **victim** | VictimRescueEnt::CanReachOffset | 0x0045ab40-0x0045ae46 | 0x0045ac9b | 0x0045aca0 |
| **axe** | AxeSwing::ResolveHit | 0x00460b90-0x004610df | 0x00460dd1 | 0x00460dd6 |
| snap (victim/player) | ModelEntity::SnapToFloor | 0x0049da00-0x0049db7d | 0x0049daa2 | 0x0049daa7 |
| other | **undefined function at 0x0049e3e0** (ROCWorldEntity vtable 0x004e4458 slot +0x34, data ref 0x004e448c). It is a DepenetrateFromWorld-like loop. Ghidra's nearest name, "ROCWorldEntity::VectorDeletingDtor 0x0049e3c0", is wrong: that function ends at 0x0049e3dd. Upper bound 0x0049e4cf. | 0x0049e3e0-? | 0x0049e447 | 0x0049e44c |

- The DepenetrateFromWorld and Think entries are player-only through DepenetrateFromWorld's only
  static caller, PlayerRescueEnt::Think 0x004537db (gated by spectator flag entity+0x380 == 0).
  Think is a virtual, so filter on ESI == player.
- SnapToFloor callers:
  - PlayerRescueEnt::ResetLocomotionState 0x0044cf69 (player, Verb_Reset path per the KB)
  - VictimRescueEnt::WarpToRescuerAndFade 0x0045c0ee
  - VictimRescueEnt::DropAtPlayer 0x0045c295
  - VictimRescueEnt::SpawnAtSpot 0x0045d719

**World::TraceGroundHeight 0x0048ff30:**

| class | function | call | ret |
|---|---|---|---|
| **victim** | VictimRescueEnt::CanReachOffset | 0x0045adde / 0x0045ae0c | 0x0045ade3 / 0x0045ae11 |
| **hose** | PlayerHose::Think_Advance 0x004677d0-0x00467e69 (called from PlayerHose::Think 0x00467e90 at 0x00467e9f) | 0x00467817 | 0x0046781c |

CanReachOffset (0x0045ab40) is called from:
- VictimRescueEnt::AdvanceRoamTarget 0x0045ae50: 0x0045af39, 0x0045b131, 0x0045b176, 0x0045b1ad
- VictimRescueEnt::ValidateSpawn 0x0045c7f0: 0x0045c873, 0x0045c89f, 0x0045c8d3, 0x0045c907

**roc_SegmentClearLOS 0x0048fea0:**

| class | function | call | ret |
|---|---|---|---|
| (wrapper) | roc_SegmentClearLOS_ActiveWorld 0x00490130-0x00490150 | 0x00490148 | **0x0049014d** |
| other (BlinkLight) | FUN_0043fd40 0x0043fd40-0x0043fda3 (LOS from a camera xform to this+0x4c). Its caller 0x0043febb is in undefined code before BlinkLightEnt::Spawn 0x0043ff10. | 0x0043fd97 | 0x0043fd9c |
| **fire** | **undefined function at 0x004483a0** (vtable data ref 0x004e17c8; probably a FireRescueEnt virtual, UNVERIFIED). Upper bound 0x0044854f. Also calls FUN_00448320 at 0x0044844c, which calls TrySpawnSpreadFire at 0x0044838e. | 0x004484c8 | 0x004484cd |

roc_SegmentClearLOS_ActiveWorld callers:
- victim: VictimRescueEnt::AdvanceRoamTarget, 0x0045afbe
- RescueBot: RescueBot::TryStartRescue 0x00455230, 0x004553d0
- other: BModelEnt_Liftable::Think 0x0043dc60, 0x0043debb

**roc_SweepWorldBox 0x00490000:**

| class | function | call | ret |
|---|---|---|---|
| **hose** (rope collider; KB) | **undefined function 0x00482740-0x0048277e** (SUB ESP,0xc ... RET 0x10; vtable data ref 0x004e36b8). Ghidra attributes it to "HoseGfx::Advance 0x004826b0", but that function ends at 0x004826d1. | 0x00482774 | 0x00482779 |

**FireRescueEnt::FloorMe 0x00448630** (direct IAT caller):
- **fire:** FireRescueEnt::Spawn 0x00449340-0x00449a3b (the KB's "AdvanceSpread"), call 0x00449a24,
  ret 0x00449a29.
- **fire:** FireRescueEnt::GotoPathPoint 0x00448a90 (bound < 0x00449340), call 0x004492a6,
  ret 0x004492ab.
- These are called from FireRescueEnt::HandleScriptCommand 0x00449a80 at 0x00449ce1 and 0x0044a18f.

**HoseGfx::RebuildGeometry 0x0046c5a0** (direct): its only caller is HoseGfx::UpdateEx 0x0046d1b0 at
0x0046d46e.

**Extinguisher:** none of the six IAT call sites or wrapper callers is in Tool_Extinguisher
(0x00461630 and up). **No, it does not trace.** "Fire::EmitEmbers 0x00463ef0" calls HoseGfx::Advance
(0x0046416d, 0x00464383), and that does not reach the IAT.

### 6c. Stack walking: FPO everywhere, so use fixed offsets

There are no `PUSH EBP; MOV EBP,ESP` frames:
- In the engine (FUN_10038c10, 100372a0, 10038470, 10036ea0, 100363f0) and in TraceToFloor,
  DepenetrateFromWorld, Think and PlayerRescueEnt::Think, EBP is a general-purpose register.
- PlayerRescueEnt::Think and FloorMe have SEH frames, but those are not EBP chains.

**Walking EBP will not work.** Every frame size is fixed, so the outer caller can be read at a known
offset from ESP at the geWorld_Collision entry (the thunk or 0x10038c10; the JMP does not move ESP):

| `[esp]` at engine entry | wrapper | outer return address at |
|---|---|---|
| 0x00492da6 | TraceToFloor (SUB 0x5c + PUSH EBX + 11 args) | **`[esp+0x90]`**. The caller's EBX was saved at `[esp+0x30]` (CanReachOffset keeps the victim `this` in EBX). ESI/EDI pass through untouched: ESI = entity for Depenetrate/Think, ESI = victim? (UNVERIFIED) |
| 0x0048ffc6 | TraceGroundHeight (PUSH ESI + 11 args) | `[esp+0x34]` |
| 0x0048ff27 | roc_SegmentClearLOS (11 args) | `[esp+0x30]`. If that equals 0x0049014d (ActiveWorld wrapper, 4 args), the real caller is at `[esp+0x44]`. |
| 0x004900de | roc_SweepWorldBox (pushes balanced before args) | `[esp+0x30]` (= 0x00482779); the next level is `[esp+0x54]` |
| 0x004486f3 | FloorMe (3 SEH pushes + SUB 0x80 + 2 pushes + 11 args) | `[esp+0xc4]` (computed; UNVERIFIED live) |
| 0x0046cbd8 | HoseGfx::RebuildGeometry | the return address is enough (single caller) |
| 0x10012253 / 0x10002fd5 / 0x10039167 | engine shadow / sound / TestModelMove | (only at the 0x10038c10 hook) |

The pymem side can check the outer return address against the ret column in 6b to classify a trace.

---

## 7. Player pointer

Confirmed: **player entity = `*(*(uint32*)0x00503a78 + 0x30)`** (g_everGame, then +0x30). It is the
entity **base** (primary vtable).
- Evidence: VictimRescueEnt::AdvanceRoamTarget 0x0045af53 does `MOV ECX,[[0x503a78]+0x30]; CALL [vtbl+0x18]; read +0x28`,
  which is the xform translation at entity+0x24+0x28 = entity+0x4c.
- VictimRescueEnt::SpawnAtSpot 0x0045d847 writes `[player+0x3528]`, the same field that
  PlayerRescueEnt::Think reads as `[ESI+0x33e8]` with ESI = entity+0x140.
- **Position = player+0x4c / +0x50 / +0x54** (float xyz).

How the pointer shows up in each hook:
- PlayerRescueEnt::Think: ESI = player+0x140, EDI = player.
- BModelEnt::Think and DepenetrateFromWorld: ESI = player.

---

## Suggested hook set (summary)

| # | where | bytes displaced | purpose |
|---|---|---|---|
| H1 | Genesis 0x10038c10 | `83ec24 8b4c2428` (7) | entry: snapshot globals plus a "fresh write" counter; save `[esp]` and the outer return address |
| H2 | Genesis 0x10038d0a / 0x10038db6 / 0x10038dbe | `5e 83c424 c3` (5 each) | exit: EAX result, ESI = Col. **Stale hit = EAX==1 with no fresh write (H3/H4) since H1.** |
| H3 | Genesis 0x100371c2 | `5b 83c42c c3` (5) | fresh box-clip write (model space) |
| H4 | Genesis 0x10036633 | `c705442f3610 01000000` (10), then `JMP 0x1003663d` | fresh ray-clip write, including actor lighting, light rays and shadows |
| H5 | exe 0x0043f9c7 | `8d464c 8bc8 8911` (7), then `JMP 0x0043f9ce` | player step-down position commit (filter ESI == player) |
| H6 | exe 0x00453816 | `d9442414 dc1d101c4e00` (10) | final per-frame player position |
| H7 | exe 0x0042aea0 (or 0x0042af0f) | `56 8bf1 8a868c060000` (9) / `a1783a5000` (5) | frame counter / sim-to-render phase marker |
