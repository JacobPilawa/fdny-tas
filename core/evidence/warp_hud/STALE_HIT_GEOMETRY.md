# Genesis box path: exactly when a sweep is a fresh hit, a stale hit, or a clean miss

Research date 2026-10-16. Read-only. Source: capstone disassembly of
`C:\fdny-decomp\FDNY SP 1.1\Genesis.dll` (scratchpad `scripts\dis.py`, run with `python -I`; the
script name shadows stdlib `dis` otherwise). Constants were read from the file bytes. BSP-data
statistics come from parsing the shipped `.bsp` files (scratchpad `scripts\leafsides.py`).
Anything not proven from instruction bytes or file bytes is marked **UNVERIFIED**.
Companion to `HOOK_SITES.md` section 3, `WARP_CONDITIONS.md` section 4 and `HOSE_AND_FRAME_ORDER.md`.

Notation:
- **S** and **E** are the sweep start and end (the box ORIGIN positions), in model space.
- A **solid leaf** is a BSP leaf with `(contents & mask) != 0` and `NumSides > 0`. The mask is
  0x43 for the player.
- **H(L)** is the box-expanded hull of leaf L (defined in section 1b).
- "Inside H(L)" means **strictly inside**: `dot(N,p) - Dist' < 0` for every expanded side.
  A point exactly on an expanded plane counts as **outside**.

---

## 0. Short answer

**Per solid leaf L visited by the BSP walk** (FUN_10036ea0 called from FUN_100371f0):

| S vs H(L) | E vs H(L) | segment | result for this leaf |
|---|---|---|---|
| outside | outside | misses H(L) | 0, nothing |
| outside | outside | passes through H(L) (thin brush) | **FRESH** write at the entry point |
| outside | inside | | **FRESH** write at the entry point |
| inside | inside | (always, by convexity) | returns 1 at 0x10036ecf **without setting e90**, so it counts as **nothing** |
| inside | outside | | **e90 = 1, no write** (0x100371c9 then 0x100371d1). This is the only **stale** source |

**Per model** (FUN_100372a0 resets e90=0 and best f18=9999 before each model):
- **Miss:** no visited leaf set e90.
- **Fresh hit:** at least one leaf wrote. The globals hold the closest fresh entry among that
  model's leaves. Any stale-type leaves in the same model are harmless.
- **Stale hit:** e90=1, but **no** leaf of the model wrote, so only stale-type leaves fired. The
  globals are whatever an earlier trace left, and the sweep accepts them.

**Whole call:**

> **geWorld_Collision (box path) returns 1 with no fresh write iff** no model wrote a fresh hit in
> this call, and at least one model has a visited solid leaf L with **S inside H(L) and E not
> inside H(L)**. In other words, the box starts overlapping solid and the sweep leaves that
> leaf's expanded hull without entering any other hull. Two further requirements: the stale
> impact must not be NaN, and it must be closer than 99999 to the start.
>
> **It returns 0 iff** for every visited solid leaf L of every model, the segment misses H(L) or
> lies **entirely inside** H(L).

Note: **a zero-length box sweep always returns 0.**

**Warp** (step-up MISS, forward MISS, step-down STALE). This requires the player box to **already
overlap a solid leaf L at its current origin (S0 inside H(L))**:
- S0+(0,16,0) and S0+(dx,16,dz) must also be inside H(L).
- S0+(dx,-1,dz) must be outside H(L). This forces the exit side to face **downward (N.y < 0)**.
  In practice the **top of the player box is poking at most about 1 unit (plus a slope term)
  past the underside of a solid leaf**. That underside can be a real ceiling or an internal leaf
  boundary inside a solid mass.
- None of the three sweeps may freshly enter any other hull. In particular there must be **no
  floor within 1 unit under the step-down's end box**.

**Empty space:** a player whose box is in empty space can **not** get a stale step-down. The one
theoretical exception is "phantom" hull regions near non-axial edges, which have no edge bevels
(UNVERIFIED whether any exist in shipped maps).

**Impact semantics:**
- **Box path:** the impact is the **box origin** at contact, backed off 0.1 plane-units outside
  the expanded plane. It is not a surface point.
- **Ray path:** the impact is the exact **surface point** on the unexpanded node plane.
- So warping onto a stale ray impact puts the player **origin on the surface**, with the box
  overlapping the solid by its full half-extent (30 horizontally on a wall; 30 below a floor
  point).

---

## 1. FUN_10036ea0: per-leaf convex-hull box clip (recursive over leaf SIDES, not BSP nodes)

`int __cdecl BoxClipLeaf(vec3 *Front, vec3 *Back, int Leaf, int Side, int PSide)`. The callers
pop the 0x14 bytes.

This is Genesis3D's `IntersectLeafSides_r`. It does **not** walk BSP nodes. It walks the
`NumSides` side planes of **one** leaf, one recursion level per side. The BSP node walk is
FUN_100371f0 (section 2).

### 1a. Data structures (from address arithmetic; matches the GBSP file layout)

| global | what | stride |
|---|---|---|
| 0x10362ef0 | GFX_Leaf array | 0x3c. +0x00 Contents, +0x34 FirstSide, +0x38 NumSides |
| 0x10362f0c | GFX_LeafSide array | 8. +0 PlaneNum, +4 PlaneSide |
| 0x10362ef8 | GFX_Plane array | 0x14. N.x, N.y, N.z, Dist, Type |
| 0x10362f24 | GFX_Node array | 0x2c. +0 front child, +4 back child, +0x10 PlaneNum |
| 0x10362e78 / 0x10362f00 | box mins / maxs | |
| 0x10362ecc / 0x10362ea8 | model-space sweep start / end | |
| 0x10362ed8 / 0x10362ee4 | model-space swept AABB min / max | |

The 0x3c leaf size and the 8-byte side size are confirmed by `leafsides.py` against the shipped
maps' chunk 4 and chunk 8 record sizes.

### 1b. Body, annotated

```
10036ea0  if (PSide == 0) return 0;                          ; 0x10036ea7-eb0. No e90 write.
10036ec8  if (Side >= Leafs[Leaf].NumSides) return 1;        ; 0x10036ecf. "Behind all sides" =
                                                              ; inside. **No e90 write.**
10036edc  plane = Planes[LeafSides[First+Side].PlaneNum];    ; 20-byte copy to local [esp+0x28]
10036f07  plane.Type = 6;                                    ; forces the full dot product below
10036f0f  if (LeafSides[..].PlaneSide) negate N and Dist;    ; 0x10036f11-f35
10036f48  FUN_10035320(&plane, mins=0x10362e78, maxs=0x10362f00);   ; box expansion:
          ;   for each axis i: Dist -= N[i] * (N[i] > 0 ? mins[i] : maxs[i])
          ;   (0x10035320-0x10035396; "N[i] > 0" is tested as not (N<=0) via TEST AH,0x41)
          ;   This is the Minkowski support offset, so Dist' = Dist + support of the box along -N.
10036f65  Fd = dot(N, *Front) - Dist';   Bd = dot(N, *Back) - Dist';    (Type==6 path)
```

**Classification.** Zero is 0.0 at 0x100598d4.

| condition | asm | action |
|---|---|---|
| `Fd >= 0 && Bd >= 0` | 0x10036fd8-0x10036ff2: TEST AH,1, so "< 0" means C0 | `return BoxClipLeaf(Front, Back, Leaf, Side+1, 0)`, which is **0** at once (tail call 0x10036fff). The whole segment is in front of one hull side, so outside. |
| `Fd < 0 && Bd < 0` | 0x1003700f-0x10037029: TEST AH,5 / JP, so "not < 0" jumps | `return BoxClipLeaf(Front, Back, Leaf, Side+1, 1)` (tail call 0x10037036, result passed through). **No e90 write.** |
| otherwise (crossing) | | see below |

**Crossing** (0x10037046-0x100370eb):
- `Side2 = (Fd < 0)` is held in EBP.
- `t = (Fd < 0 ? Fd + 0.1 : Fd - 0.1) / (Fd - Bd)`. The ON_EPSILON 0.1 is at 0x10051330
  (bytes `cdcccc3d`).
- Then clamp: `t < 0 -> 0` (0x10037085), `t > 1 -> 1` (0x1003709c, using 1.0 at 0x1005118c).
- `I = Front + t*(Back - Front)`, stored to local `[esp+0x10..0x18]`.

Effect of the epsilon:
- **Entering crossing (Fd >= 0):** I lies at expanded-plane distance **+0.1**, just outside. If
  Fd < 0.1 then t clamps to 0 and **I = Front**.
- **Exiting crossing (Fd < 0):** I lies at distance -0.1, just inside, or I = Front when |Fd| < 0.1.

Then:

```
100370ed  if (BoxClipLeaf(Front, &I, Leaf, Side+1, Side2))  goto RET1_NOWRITE;  ; JNZ 0x100371c9
1003710d  if (!BoxClipLeaf(&I, Back, Leaf, Side+1, !Side2)) return 0;           ; JE 0x100371db
1003711d  dist = sqrt(|I - GFront(0x10362ecc)|^2)   ; 0x10029b70 is a dot product, then FSQRT
10037156  if (!(dist < f18))  goto RET1_NOWRITE_POP;     ; JP 0x100371c7. "Not closer" or NaN.
10037163  if (f44 != 0)       goto RET1_NOWRITE_POP;     ; JNE 0x100371c7
1003716c  FRESH WRITE: f18 = e94 = dist; e9c..ea4 = I; f48..f58 = expanded plane (Type 6);
          f10 = Leaf; f44 = 1; e90 = 1; return 1        ; 0x1003716c-0x100371c6
100371c7  RET1_NOWRITE_POP: FSTP ST0
100371c9  RET1_NOWRITE:     e90 = 1; return 1           ; 0x100371cb-0x100371da
100371db  return 0
```

### 1c. Every return path and what reaches it

| exit | e90 | write | reached when |
|---|---|---|---|
| 0x10036eab | - | - | `PSide == 0`. The caller's side is the empty/front side of a convex hull, so there is nothing there. |
| 0x10036ecf (ret 1) | **not set** | - | Recursed past the last side: the current sub-segment is behind (inside) every remaining side. |
| tail 0x10036fff / 0x10037036 | passthrough | - | both-front (returns 0) / both-back (passes the deeper result through) |
| 0x100371db | - | - | the crossing's back part (`I..Back` with PSide=!Side2) returned 0 |
| **0x1003716c-c6 (a) fresh** | 1 | **impact, plane, ratio** | an entering crossing whose `I..Back` part lies inside the remaining sides, I closer than f18, and no deeper write in this leaf (f44==0) |
| **0x100371c7 (b1)** | 1 | none here | same as (a), but I is not closer than f18 (a **closer fresh hit from an earlier leaf of the same model** already sits in the globals), or f44=1 (a **deeper level of the same leaf already wrote**). **Never stale.** |
| **0x100371c9 via 0x100370f7 (b2)** | 1 | none here | the crossing's **front** part `Front..I` (PSide=Side2) returned 1. That has two sub-cases: (i) a deeper level wrote fresh (only possible if Front is outside some later side), so not stale; (ii) **Front is inside every side and the chain ended at 0x10036ecf**, so **nothing was written anywhere**. That is the stale source. |
| (c) return 0 | - | - | segment misses H(L) |

### 1d. Proven classification per leaf (from the recursion structure above)

Let H(L) be the set of points with `dot(N_k,p) - Dist'_k < 0` for every side k of L. This is a
convex set, so a segment crosses each side at most once.

1. **S inside H(L).** Every level sees `Fd < 0` with Front still equal to S, because only the
   Front-preserving recursions with PSide=1 continue (`Front..I` for exits; `Front..Back` for
   both-back). So no entering crossing ever happens and **nothing is ever written**.
   - **E inside H(L):** every side is both-back, giving 0x10036ecf. **Returns 1 with e90
     untouched.** FUN_100371f0 ignores the return value (0x10037291 just pops), so **this leaf
     contributes nothing.** This is the KB's "entirely inside solid, no plane crossing, clean
     miss", now verified.
   - **E outside H(L)** (some side has `Bd >= 0`): the first such side, in side order, is an exit
     crossing. Its `Front..I` part is inside every remaining side and returns 1, so it goes to
     0x100371c9. **e90 = 1, no write.** This is the KB's "starts inside solid, returns 1 without
     writing", now verified. It is the **only** way a leaf yields e90 without a fresh write
     having happened in the same model call.
2. **S outside H(L)** (some side has `Fd >= 0`). On any path that reaches 0x10036ecf, the first
   such side is met with Front == S or with an I from an earlier entering split. It is therefore
   either both-front (0) or an entering crossing, so any 1-return comes with a fresh write in this
   leaf or a closer one already in the model. So **S outside H(L) never produces a stale result**:
   - empty start to solid end: **fresh**.
   - empty, then solid, then empty (thin brush): **fresh**, at the entry. The box path cannot
     tunnel through thin brushes, because the hull is thickened by the box extents.
   - segment misses: 0.
3. **Solid-to-solid boundary.** Each leaf is judged independently:
   - Box starts overlapping L1 and moves into L2, starting outside H(L2): L2 gives a **fresh
     entry**, with the impact at H(L2)'s boundary (geometrically inside L1). The result is
     **fresh**, with L2's side plane as the normal. That can be an internal bevel or other internal
     plane, i.e. a normal of "nothing visible".
   - Box already overlaps both L1 and L2 at S: L2 is inside-inside (nothing) or inside-outside
     (stale-type), so the result is **stale or miss**.
4. **NaN.** Fd=NaN fails the first "< 0" test as if negative, but passes the TEST AH,5/JP tests
   as if ">= 0". It ends in the crossing branch with a NaN t that the clamp does not fix, so I is
   NaN. The `dist < f18` test is unordered, so it goes to b1 (no write). Not analysed further
   (UNVERIFIED reachability).

### 1e. What the hull is (BSP data, verified by `leafsides.py` over all 17 shipped maps)

- **Every** solid leaf in every map has side planes in all 6 axial directions, and in all but 3
  of about 45,000 leaves these lie exactly at the leaf's own AABB. So the leaf sides include
  **axial bevels at the leaf bounds**, and H(L) is never larger than the leaf AABB expanded by
  the box.
- Non-axial sides are expanded **without edge bevels**. Near non-axial edges where two
  non-axial planes meet, H(L) can therefore extend slightly past the true Minkowski sum: a
  "phantom" zone where the box does not touch L but counts as inside H(L). In 2D cross-sections
  the axial bevels make it exact. Whether such edges exist in shipped maps is UNVERIFIED.
- Whether leaf sides include planes shared with **other solid leaves**, or only empty-facing ones
  (the GBSP builder normally adds only empty-facing portals plus bevels), is UNVERIFIED. The
  bevels bound H(L) either way.
- Solid-mask contents seen: 0x1, 0x21, 0x60, 0x61, 0x460, 0x468, 0xa1, 0xaa and others. These all
  have NumSides>0. Contents without the mask bits (0x4, 0x24, 0x80, 0x120, ...) have NumSides=0.

---

## 2. FUN_100371f0: the BSP walk that picks leaves (`__cdecl(int node)`, called at 0x10037687)

```
loop: if (node < 0) goto leaf;
      side = FUN_10035210(swept_min=0x10362ed8, swept_max=0x10362ee4, &Planes[Nodes[node].PlaneNum]);
            ; returns bit1 if max-corner dist >= Dist, bit2 if min-corner dist < Dist.
            ; Axial planes (Type<3) are tested on the AABB coordinate; others use the support corner.
            ; Unexpanded node plane, no epsilon.
      if (side & 1) FUN_100371f0(Nodes[node].Front);     ; recursive (0x10037232)
      if (side & 2) { node = Nodes[node].Back; goto loop; } else return;
leaf: L = -1 - node;
      if (!(Leafs[L].Contents & [0x10362f5c])) return;   ; mask 0x43 for the player
      f44 = 0;                                           ; 0x10037271, BEFORE the NumSides test
      if (Leafs[L].NumSides == 0) return;
      FUN_10036ea0(&GFront 0x10362ecc, &GBack 0x10362ea8, L, Side=0, PSide=1);  ; return value IGNORED
```

- The walk visits **every** solid leaf whose BSP cell intersects the **swept AABB**. That AABB is
  built by FUN_10035470 (0x10037677):
  `min = min(S,E) + mins - 1.0`, `max = max(S,E) + maxs + 1.0`, with 1.0 at 0x1005118c and a
  **1-unit margin**.
- The walk is conservative: a leaf whose cell meets the AABB is always visited. So if the box at
  S or E really overlaps L, L is visited. Only phantom zones (section 1e) can be "inside H(L)"
  without L being visited.
- The **only output** of the walk is the side-effect set (e90, e9c.., f48.., f18, f44, f10).
- e90 is **not** reset per leaf: it is OR-accumulated across all leaves of the model.
  f44 **is** reset per leaf. f18 (best) is **per model**.

---

## 3. FUN_100372a0: box sweep, how models combine

Reference: locals after the prologue. `[esp+0x14]` is the world-space best distance, initialised
**once** to 99999.0 (0x47c34f80) at 0x100372e6. `[esp+0x1c]` is the overall hit flag (0).

1. 0x1003737b-0x10037437: copies mins, maxs and world start/end into the globals, and loads the
   world BSP arrays.
2. If CollideFlags & 2 (the exe passes 2):
   - FUN_10035470 builds the **world-space swept AABB** into `[esp+0x68]` (min) and `[esp+0x5c]`
     (max).
   - Then it loops over all models, stride 0xd4, count `[world+0xa4]`, with EBP = model+0x6c.
     Model 0 is presumably the world model (Genesis convention, UNVERIFIED).
3. Per model:
   - Skip if `[model+0x6c+0x60] & 0x10`, a non-solid model.
   - Call the optional callback.
   - **AABB reject** (0x100374b7-0x100374e9): skip unless model bounds `[+0x20..]` / `[+0x2c..]`
     overlap the swept AABB on all 3 axes.
   - `f18 = 9999.0` (0x100374fe) and **`e90 = 0`** (0x10037508).
   - Start/end go to model space: subtract the pivot (+0x38), subtract the translation (+0x6c-4..),
     rotate by the transpose of the 3x3 (+0x6c-0x28..), add the pivot. Mins/maxs are **not
     rotated**, so the box stays axis-aligned in model space.
   - Model-space swept AABB, then `FUN_100371f0(root node of this model)` (0x10037687).
   - **`if (e90 == 0) next model`** (0x10037696).
     - **The sweep checks only e90. It never knows whether a write happened.**
   - e90 == 1, so it **transforms the globals in place**, whether fresh or stale:
     - normal: 0x10001180
     - impact: minus pivot, 0x100010f0, plus pivot (exact transform semantics UNVERIFIED)
     - **Plane.Dist = dot(N, impact)** (0x10037743)
   - `d = |impact - worldStart|` (0x1003776f, FSQRT).
   - **if `d < best`** (strict; NaN is rejected by JP at 0x1003777f):
     - best = d
     - save impact to `[esp+0x38]`, plane to `[esp+0x7c]`, and the model to `[esp+0x28]`
     - hit = 1
   - If CollideFlags & 8, stop after the first model.
4. Return 1 (0x10037893) iff hit, copying the saved impact and plane out. Otherwise 0 (0x100378a0).

**Consequences**
- A model whose only e90 source is a stale-type leaf **does** produce a final hit. The sweep feeds
  it the stale globals, transformed by that model's xform, as long as their world distance from
  the start beats 99999 and every earlier model's best.
  - For the world model the in-place transform is presumably identity (UNVERIFIED), so a stale
    value arrives **unchanged apart from Plane.Dist being recomputed**.
- Mixed models in one call:
  - A fresh world hit followed by a stale-type entity model: the entity model re-transforms the
    already-world-space globals. With an identity xform that gives equal distance, which is not
    `<`, so the fresh hit is kept.
  - With a moved or rotated entity model (doors, lifts), the re-transformed value can land
    **closer** and win: a garbage result built from this call's fresh data (UNVERIFIED in
    practice).
  - A stale world model followed by a fresh entity hit: the fresh one wins only if it is closer
    than the stale point.
- Within one model, fresh beats stale outright: any fresh write in a model makes that model's
  result fresh, and stale-type leaves in it only set e90.

---

## 4. Applying this to the warp (Think's three sweeps)

These come from WARP_CONDITIONS section 3c:
- step-up `S0 -> S0+(0,16,0)`
- forward `S0+(0,16,0) -> S1 = S0+(dx,16,dz)`
- step-down `S1 -> E1 = S0+(dx,-1,dz)`

The warp needs MISS, MISS, then STALE.

### 4a. Derivation (world geometry, no phantom zones)

- The step-down is stale, so there is a visited L with **S1 inside H(L)** and **E1 not inside
  H(L)**, and no fresh entry anywhere in that model.
- Forward is a MISS and its end S1 is inside H(L), so its start must be too: otherwise there is an
  entering crossing and a fresh write (section 1d case 2). So **S0+(0,16,0) is inside H(L)**.
  L is visited by the forward sweep because box(S1) overlaps L and the walk is conservative.
- Step-up is a MISS and its end is inside H(L), so **S0 is inside H(L)** by the same argument.
- Let P be the side of L that E1 exits through: `dot(N,S1) < D'` and `dot(N,E1) >= D'`. Then
  `dot(N, E1 - S1) = dot(N,(0,-17,0)) = -17*N.y > 0`, so **N.y < 0**: P faces **downward**. It is
  the underside of the solid leaf as seen by the box top.
- With margin `m = D' - dot(N,S0) > 0` (how far S0 is inside P), the step-down exits iff
  **`m <= -N.y + N.x*dx + N.z*dz`**, and the forward end stays inside iff
  `16*N.y + N.x*dx + N.z*dz < m`. For a flat underside (N=(0,-1,0)) that reduces to
  **`0 < m <= 1`**.
  - Since `D' = D + maxs.y*|N.y|`-type expansion, that means **the top of the player box
    (S0.y + maxs.y) is above the underside of L by more than 0 and at most 1 unit**.
  - For a sloped underside, horizontal motion toward the slope widens the window by
    `N_h·(dx,dz)`.
- The side-order recursion picks the **first** exiting side in leaf-side order. That does not
  change the outcome, only that the result is stale.
- No other leaf of the model may be freshly entered by any of the three sweeps:
  - Step-up moves the box 16 up, so no new leaf above may be entered unless the box already
    overlaps it.
  - Forward moves horizontally.
  - **Step-down moves the box bottom to S0.y + mins.y - 1**, so a floor whose top is above that,
    and which the box was not already overlapping, gives a fresh hit and kills the stale result.
    The player must be **airborne or more than 1 unit above any floor** that its box does not
    already overlap.
- Separately, the Depenetrate sweep that invoked Think must have returned 1 with normal.y <= 0.9,
  and the latch must be clear (WARP_CONDITIONS section 4).
  - From inside H(L), that sweep is stale whenever `S0+disp` exits H(L), for example when falling
    makes the box top drop through P. So it is naturally also stale here.
  - Its "normal" is whatever the globals hold, so the 0.9 gate is a coin flip on the last fresh
    writer.

**Where P comes from:**
- **A real ceiling or overhang underside.** The box top is embedded 0 to 1 unit into it, with the
  leaf extending at least 16 more upward and covering the horizontal motion.
- **An internal leaf boundary inside a solid mass.** For example, the bottom bevel of a solid leaf
  L stacked on another solid leaf L'. When the box already overlaps both L and L', moving down
  only exits L, since L' stays inside-inside. A player whose box is buried in solid therefore gets
  a stale step-down whenever its box top lies within 1 unit above **any** downward-facing side
  of a leaf it overlaps.
  - If the player sinks through solid with per-frame vertical travel |dy| > 1, the first frame
    whose Depenetrate sweep crosses such a boundary lands in the 1-unit window with probability
    of about 1/|dy| (inference, UNVERIFIED at runtime). That is "works sometimes", and it is
    **framerate-dependent**, because |dy| scales with dt.

### 4b. Player in empty space: **No**

If the box at S0 is in empty space (S0 outside every H(L), modulo phantom zones):
- For the step-down to be stale, S1 must be inside some H(L).
- The forward sweep visits L, because box(S1) overlaps L, and starts outside H(L) (otherwise S0
  would have to be inside, by the step-up argument). So **the forward sweep is a fresh hit**:
  exit B, no step.
- If S0+16 is outside H(L) and S1 is outside too, the step-down starts outside and gives a fresh
  hit or a miss, never stale.

So the KB's "micro-clip onto your own wall-hit" idea cannot work:
- Fresh box impacts are committed at expanded-plane distance +0.1 (or exactly at the start when
  Fd < 0.1). Both are >= 0, which counts as **outside**.
- `pos += disp` after a clean sweep ends outside every visited hull by definition.
- Normal movement never puts the origin strictly inside a hull.

The only way in is an **unvalidated position write**. Candidates (exe-side, UNVERIFIED as actual
routes):
- a commit to a stale **ray** impact (section 5)
- Depenetrate's `pos += disp` after 5 consecutive hits with normal.y >= 0.5 (ClipVelocitySimple
  keeps disp; WARP_CONDITIONS section 2)
- a moving entity model closing on the player
- SnapToFloor, Verb_Reset, spawn or checkpoint placement
- an unchecked crouch-to-stand maxs.y change (25 to 85) under a low ceiling. Whether the
  un-crouch checks headroom is UNVERIFIED.
  - **UPDATE 2026-10-16: ruled out.** StandUp 0x0044bce0 is the only code that grows the box,
    and it first calls geWorld_GetContents (Genesis 0x10037e90 -> leaf test 0x10036d90) with the
    85-tall box. That test uses the same box-expanded sides (FUN_10035320) and the same strict
    "< 0 on every side" rule as the sweep, so a stand-up that succeeds never leaves the origin
    strictly inside a hull. The opposite desync does exist (state "standing" with the 25-tall
    crouched box, after leaving a climb volume or respawning while crouched), but it only makes the
    box smaller. research.db H235-H237.

Theoretical exception: **phantom zones** near non-axial edges (section 1e), where an origin counts
as inside H(L) though the box does not touch L. There the walk-visibility of L can differ between
sweeps, because the step-down's AABB reaches 17 units lower. Not observed and UNVERIFIED.

### 4c. Crouch vs stand

The rule is the same. The geometry it applies to differs:
- **For N.y<0 sides** (the relevant ones), FUN_10035320 expands with **maxs.y**: 85 standing, 25
  crouched. So the condition is about the **top of the box**, 85 or 25 above the origin: "box top
  within (0,1] above a downward-facing leaf side".
- Crouching moves the top 60 units lower, so a different set of ceilings and internal boundaries
  qualifies.
- Mins (-30) only matter for the "no floor within 1 unit" clause: step-down end box bottom at
  S0.y - 31.
- A crouched player has a shorter box, so it is less likely to also be overlapping a floor or
  ceiling elsewhere.
- Standing up under a ceiling, if unchecked, is a natural way to embed the box top by 0 to 60
  units (UNVERIFIED). **UPDATE 2026-10-16: it is checked (see 4b), so this does not happen.**
- **Crouchbug (2026-10-16, H235/H236):** stance state 3 with maxs.y still 25. The warp geometry
  follows the box (top at origin+25), but the camera (eye 70) and speed (400) follow the state,
  and Verb_Jump is allowed, so the short box can jump.

---

## 5. Impact semantics (item 4)

**Box path: confirmed.** The written impact (0x1003719c / 0x10037184 / 0x100371ad, from local
I) is `I = Front + t*(Back - Front)`. Front and Back are the sweep's **origin** positions in model
space. So the impact is the **box origin** at contact: where the box centre point (the origin, not
the geometric centre, since mins/maxs are asymmetric) sits when the box touches the expanded hull.
- It is backed off so the origin is **0.1 plane-units outside** the expanded plane, or is exactly
  S when the start was already within 0.1.
- It is **not** a point on the surface. The plane written is the **expanded** plane, Type 6, with
  Dist later recomputed by the sweep as `dot(N, impact)`.
- `Ratio` (e94) is the model-space distance |I - S|, not a fraction.

**Ray path: confirmed.** FUN_100363f0 (0x100363f0-0x1003664f) recurses the **BSP nodes**
(0x10362f1c+0x4c, stride 0x2c) against the **unexpanded node planes** (0x10362f1c+0x68):
- `t = Fd / (Fd - Bd)` with no epsilon (0x10036546-0x10036552).
- `I = Front + t*(Back - Front)` lies exactly on the node plane.
- The leaf test is `(Contents & mask) != 0` (0x100363fd-0x10036420).
- A fresh write happens only when f44 == 0 (0x100365cd).
- So a ray impact is a **point on the surface plane**.
- The ray path has the same stale property: a ray that starts in a solid leaf returns 1 through
  0x1003663d with no write.

**Committing a ray impact as the player origin** (e.g. hose water on a wall):
- The origin lands **on the wall plane**.
- The box (x/z +/-30; y -30..+85 or +25) overlaps the wall solid by **30 units** along the wall
  normal, or by T if the wall is thinner than 30.
- On a floor point the box bottom is 30 units into the floor.
- So the origin is **deep inside H(L_wall)**, by about 30, nowhere near the 0.1 boundary.

**Following frames, from inside H(L_wall):**
- **Sweeps that stay inside H(L_wall):** that leaf contributes nothing. The player moves freely
  through the wall solid (`pos += disp`), unless a sweep freshly **enters some other** hull. The
  floor is the typical case: a fresh hit with N.y=1 > 0.9, so Think exit A clips and sets
  grounded. So the player can stand half inside the wall on the floor.
- **Sweeps whose end leaves H(L_wall)** (backing out of the wall, or passing through its far
  side) are **stale hits**. The plane and impact are whatever the globals hold. With an active
  hose, that is the hose's latest surface point, since hose rays write the same globals every
  frame (HOSE_AND_FRAME_ORDER). The outcome then depends on the stale normal:
  - **N.y > 0.9:** exit A clip. Five stale hits lead to ClipVelocitySimple with N.y >= 0.5, which
    **keeps disp**, so `pos += disp` carries the player out of the wall anyway.
  - **N.y < 0.5 (a wall-like stale normal):** step-up (inside to inside) is a MISS and forward
    (exits the wall) is STALE, so exit B. Five iterations then **zero velocity and disp**: the
    player **freezes** in the wall until something changes the globals' normal.
  - **0.5 <= N.y <= 0.9:** disp is kept, so the player gets out.
- **Stale again, i.e. a chained warp:** this needs the section 4a configuration. The box top must
  be within about 1 unit above a downward-facing side of a leaf it overlaps, with no floor within
  1 unit below and forward and step-up staying inside.
  - On an ordinary vertical wall hit at mid-height this is **not** satisfied. The wall leaf's
    bottom bevel is at floor level, far below the box top, and its top is far above.
  - So the next frame is normally a clean miss (moving within the wall) or a non-warping stale
    hit (leaving the wall). A chained warp needs the box top to sit near a wall leaf's underside
    (doorway lintel, overhang, or an internal split), which is geometry-specific.

---

## 6. Epsilons and float sensitivities ("works sometimes")

| item | value | where | effect |
|---|---|---|---|
| side test zero | 0.0 (0x100598d4) | 0x10036fda.. | `>= 0` is outside. A point exactly on an expanded plane is outside, so "strictly inside" is needed for stale. |
| ON_EPSILON | **0.1** (0x10051330) | 0x1003706f/77 | entry impact sits +0.1 outside; t clamps to 0 (impact = start) when the start is within 0.1 |
| t clamp | [0, 1] (0x100598d4, 0x1005118c) | 0x10037085-0x100370ab | NaN passes the clamp unchanged |
| swept-AABB margin | +/-1.0 (0x1005118c) | FUN_10035470 | leaves within 1 unit of the swept box are visited, which harms nothing (the hull test is exact) |
| per-model best f18 | 9999.0 (0x461c3c00) | 0x100374fe | a fresh write is skipped if `dist >= best` |
| sweep best | 99999.0 (0x47c34f80) | 0x100372e6 | a model's (possibly stale) result is accepted only if `d < best` (strict); NaN rejected |
| step-down window | 1 unit (17 - 16), plus the `N_h·(dx,dz)` slope term | exe | the whole warp hinges on the box top being in (0, 1] above a downward side |

Why it is intermittent (inference from the rule; runtime UNVERIFIED):
1. **The 1-unit window versus per-frame travel.** The box top must land in (b, b+1] on the frame
   being evaluated. Bigger dt means bigger steps and fewer hits; this is framerate-dependent.
   Horizontal motion into a sloped underside widens the window.
2. **The stale normal.** It is whatever the last fresh writer stored: hose rays, render-phase
   shadow and light rays, or the previous player sweep. It decides whether the Depenetrate hit
   even enters the step branch (normal.y <= 0.9) and what ClipVelocity does.
3. **Any fresh entry in the same model cancels staleness.** A floor within 1 unit below the
   step-down end, a neighbouring leaf entered by the 16-unit step-up, or a side wall touched by
   the forward move all do this.
4. **Float rounding** of `S0 + (0,16,0) + (dx,0,dz) + (0,-17,0)` (three separate Vec3_Adds in the
   exe), and of the model-space transform, matters only at the window edges (about 1e-4 at
   coordinates in the thousands).
5. **Which leaf is the exit side** depends on BSP leaf splits that are invisible in-game (internal
   boundaries). So "the same spot" can behave differently by a few units.

---

## 7. Corrections to earlier notes

- **The "start in solid gives stale" claim needs a condition.** It only holds if the sweep also
  **leaves** that leaf's expanded hull. Start and end both inside is a clean miss, because
  0x10036ecf returns 1 without touching e90 and FUN_100371f0 ignores the return value.
- **0x100371c7 is never a stale source.** Its "farther than best" and "f44 set" paths both imply
  a fresh write earlier in the same model call.
- **0x100371c9 is the stale source only in sub-case (ii) of 1c.** That is the start-inside chain.
- **"Zero-length box sweep" (WARP_CONDITIONS section 2: "whether a zero-length sweep can return 1
  is UNVERIFIED") is now verified: it always returns 0** on the box path. S = E gives Fd = Bd for
  every side, so the segment is either both-front (0) or both-back down to 0x10036ecf (no e90).
- **FUN_100371f0 is a BSP node walk over the swept AABB, not a single-leaf test.** It resets f44
  for every solid-mask leaf it reaches.
