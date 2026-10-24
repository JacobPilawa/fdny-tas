"""Fire-juggling probe, round 2: the unknowns left by firejuggle_probe.py.

Runs beside tests/probes/probe_firejuggle2.yaml, which installs zz_juggleprobe.asi
(in-game hooks that log, per frame, the order of Think / fire touch / damage / reset /
drop / cool-down) and holds bank open with RescueBot off.

  E  frame order: teleport into a fire (the asi log shows the order), then a walk-in
     with W held and no teleport, so the arrival frame is the game's own;
  F  crouchbug: duck (C), pass out, respawn; read stance +0x18C and box top +0x88;
     then C twice;
  G  herodown: difficulty [[0x502AAC]+0x6E4]+0x2C set to 2 (hardest) for one
     pass-out; count kind-2 entities afterwards; the value is restored;
  H  actor catch: while passed out, put the safe spot +0x3400 120 u above another
     victim; does the drop land on that victim (actor) or on the floor?

python -I firejuggle_probe2.py <out.log>
"""
import math
import os
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from fdh import inputs  # noqa: E402

import pymem  # noqa: E402

GS_SLOT, EVERGAME = 0x00502AAC, 0x00503A78
P_POS, P_DISP, P_VEL, P_GROUND, P_STATE, P_TOP = 0x4C, 0x90, 0xFC, 0x12C, 0x18C, 0x88
P_IMPULSE, P_CKPT, P_SNAP, P_O2, P_YAW = 0x5B8, 0x534, 0x3400, 0x3450, 0x3434
P_CARRY, P_BURN, P_FLINCH, P_TOTAL = 0x3534, 0x3538, 0x353C, 0x3528
V_POS, V_STATE, V_DEF, F_POS = 0x4C, 0x120, 0x150, 0x7C
FIRE = 0x8CB3548  # placeholder; the fire is found by burning, below

out = open(sys.argv[1] if len(sys.argv) > 1 else "firejuggle2.log", "w", encoding="utf-8")


def log(msg):
    line = f"{time.strftime('%H:%M:%S')} {msg}"
    print(line, flush=True)
    out.write(line + "\n")
    out.flush()


pm = None
for _ in range(600):
    try:
        pm = pymem.Pymem("FDNYFirefighter.exe")
        break
    except Exception:
        time.sleep(1)
if pm is None:
    log("FAIL no game process")
    sys.exit(1)

rb = lambda a, n: pm.read_bytes(a, n)
u32 = lambda a: struct.unpack("<I", rb(a, 4))[0]
i32 = lambda a: struct.unpack("<i", rb(a, 4))[0]
f32 = lambda a: struct.unpack("<f", rb(a, 4))[0]
vec = lambda a: struct.unpack("<3f", rb(a, 12))
wvec = lambda a, v: pm.write_bytes(a, struct.pack("<3f", *v), 12)
fmt = lambda v: "(" + ", ".join(f"{c:.1f}" for c in v) + ")"
dist = lambda a, b: math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def cstr(p, n=32):
    try:
        return rb(p, n).split(b"\0", 1)[0].decode("latin-1") if p else ""
    except Exception:
        return "?"


def map_name():
    try:
        return cstr(u32(u32(u32(GS_SLOT) + 0x688)) + 0xC, 16)
    except Exception:
        return ""


def player():
    try:
        return u32(u32(EVERGAME) + 0x30)
    except Exception:
        return 0


def entities():
    node, n, res = u32(u32(GS_SLOT) + 0x64), 0, []
    while node and n < 5000:
        e = u32(node)
        res.append((e, i32(e + 0x18)))
        node = u32(node + 4)
        n += 1
    return res


def victim_names(v):
    d = u32(v + V_DEF)
    return cstr(u32(d + 0x10)) if d else "", cstr(u32(d + 0x14)) if d else ""


def pline(tag):
    return (f"{tag} pos={fmt(vec(pl + P_POS))} snap={fmt(vec(pl + P_SNAP))} burn={f32(pl + P_BURN):.3f} "
            f"flinch={rb(pl + P_FLINCH, 4).hex()} state={i32(pl + P_STATE)} top={f32(pl + P_TOP):.1f} "
            f"ground={rb(pl + P_GROUND, 1)[0]} carry={u32(pl + P_CARRY):#x}")


def teleport(p):
    wvec(pl + P_POS, p)
    for off in (P_VEL, P_DISP, P_IMPULSE):
        wvec(pl + off, (0.0, 0.0, 0.0))


def wait_for(cond, secs):
    t = time.time()
    while time.time() - t < secs:
        if cond():
            return True
        time.sleep(0.005)
    return False


def pick_up(v):
    vp = vec(v + V_POS)
    for dy in (20.0, 40.0, 60.0):
        teleport((vp[0], vp[1] + dy, vp[2]))
        if wait_for(lambda: u32(pl + P_CARRY) == v, 1.5):
            log(f"picked up {v:#x} at {fmt(vp)}")
            return True
    log(f"could not pick up {v:#x}")
    return False


def into_fire():
    fp = vec(fire + F_POS)
    teleport((fp[0], fp[1] + 30.0, fp[2]))


def pass_out_and_reset(tag, on_out=None):
    if not wait_for(lambda: i32(pl + P_STATE) == 6, 8):
        log(f"{tag}: never passed out")
        return None
    log(pline(f"{tag} PASSED OUT"))
    snap = vec(pl + P_SNAP)
    if on_out:
        snap = on_out() or snap
    wait_for(lambda: i32(pl + P_STATE) != 6, 10)
    time.sleep(0.4)
    log(pline(f"{tag} RESET"))
    return snap


# ---- wait for bank ---------------------------------------------------------------------
if not wait_for(lambda: map_name().lower() == "bank" and player(), 600):
    log(f"FAIL map never became bank ({map_name()!r})")
    sys.exit(1)
log("bank loaded; settling 8 s")
time.sleep(8)
pl = player()
home = vec(pl + P_POS)
log(pline("start"))
victims = [e for e, k in entities() if k == 2]
fires = [e for e, k in entities() if k == 6]
for v in victims:
    log(f"  victim {v:#x} pos={fmt(vec(v + V_POS))} state={i32(v + V_STATE)} names={victim_names(v)}")

# find a fire that burns (as round 1: the first that raises the meter)
fire = None
for f in fires:
    fp = vec(f + F_POS)
    teleport((fp[0], fp[1] + 30.0, fp[2]))
    b0 = f32(pl + P_BURN)
    if wait_for(lambda: f32(pl + P_BURN) > b0 + 0.02, 0.6):
        fire = f
        break
if fire is None:
    log("FAIL no fire burns")
    sys.exit(1)
log(f"fire {fire:#x} at {fmt(vec(fire + F_POS))}")
teleport(home)
wait_for(lambda: f32(pl + P_BURN) < 0.001, 15)
time.sleep(1.0)

V = victims[0]

# ---- E1: teleport in (asi log: frame order) -----------------------------------------------
log("=== E1: carry, teleport into the fire, pass out (see zz_juggleprobe.log)")
if pick_up(V):
    time.sleep(1.5)
    log(pline("E1 before"))
    into_fire()
    pass_out_and_reset("E1")
    log(f"E1: victim at {fmt(vec(V + V_POS))}")

# ---- E2: walk in --------------------------------------------------------------------------
log("=== E2: walk into the fire with W (no teleport at the fire)")
time.sleep(1.0)
fp = vec(fire + F_POS)
best = None
for start_dx, start_dz in ((150, 0), (-150, 0), (0, 150), (0, -150)):
    for yaw in (0.0, 90.0, 180.0, 270.0):
        teleport((fp[0] + start_dx, fp[1] + 30.0, fp[2] + start_dz))
        time.sleep(0.6)
        if f32(pl + P_BURN) > 0.001 or not rb(pl + P_GROUND, 1)[0]:
            continue
        p0 = vec(pl + P_POS)
        pm.write_bytes(pl + P_YAW, struct.pack("<f", yaw), 4)
        inputs.key_down("w")
        time.sleep(0.25)
        inputs.key_up("w")
        time.sleep(0.2)
        p1 = vec(pl + P_POS)
        moved = (p1[0] - p0[0], p1[2] - p0[2])
        want = (fp[0] - p0[0], fp[2] - p0[2])
        n1, n2 = math.hypot(*moved), math.hypot(*want)
        cos = (moved[0] * want[0] + moved[1] * want[1]) / (n1 * n2) if n1 > 5 and n2 > 5 else -1
        log(f"  E2 calib start=({start_dx},{start_dz}) yaw={yaw:.0f} moved={n1:.0f} cos={cos:.2f}")
        if cos > 0.95:
            best = (start_dx, start_dz, yaw)
            break
    if best:
        break
teleport(home)
wait_for(lambda: f32(pl + P_BURN) < 0.001, 15)
if best and pick_up(V):
    time.sleep(1.0)
    sdx, sdz, yaw = best
    teleport((fp[0] + sdx, fp[1] + 30.0, fp[2] + sdz))
    time.sleep(1.0)
    pm.write_bytes(pl + P_YAW, struct.pack("<f", yaw), 4)
    log(pline("E2 start"))
    inputs.key_down("w")
    wait_for(lambda: f32(pl + P_BURN) > 0.001, 4)
    log(pline("E2 first burn"))
    time.sleep(0.4)
    inputs.key_up("w")
    pass_out_and_reset("E2")
    log(f"E2: victim at {fmt(vec(V + V_POS))}")
else:
    log("E2: no straight approach found; skipped")

# ---- F: crouchbug -------------------------------------------------------------------------
log("=== F: duck, pass out, respawn")
time.sleep(1.0)
teleport(home)
time.sleep(1.0)
inputs.tap("c", 0.1)
time.sleep(0.5)
log(pline("F ducked"))
into_fire()
pass_out_and_reset("F")
time.sleep(0.5)
log(pline("F after respawn"))
p0 = vec(pl + P_POS)
inputs.key_down("w")
time.sleep(0.5)
inputs.key_up("w")
log(f"F: moved {dist(p0, vec(pl + P_POS)):.0f} u in 0.5 s with W")
inputs.tap("c", 0.1)
time.sleep(0.5)
log(pline("F after C #1"))
inputs.tap("c", 0.1)
time.sleep(0.5)
log(pline("F after C #2"))

# ---- G: herodown --------------------------------------------------------------------------
log("=== G: difficulty field = 2 for one pass-out")
time.sleep(1.0)
dfield = u32(u32(GS_SLOT) + 0x6E4) + 0x2C
orig = i32(dfield)
log(f"G: difficulty field {dfield:#x} = {orig}; total victims +0x3528 = {i32(pl + P_TOTAL)}; "
    f"kind-2 entities = {sum(1 for e, k in entities() if k == 2)}")
teleport(home)
time.sleep(1.0)
pm.write_bytes(dfield, struct.pack("<i", 2), 4)
into_fire()
pass_out_and_reset("G")
pm.write_bytes(dfield, struct.pack("<i", orig), 4)
vs = [e for e, k in entities() if k == 2]
log(f"G: restored field to {orig}; total victims +0x3528 = {i32(pl + P_TOTAL)}; kind-2 entities = {len(vs)}")
for v in vs:
    if v not in victims:
        log(f"  NEW victim {v:#x} pos={fmt(vec(v + V_POS))} state={i32(v + V_STATE)} names={victim_names(v)}")
log(f"G: player respawned at {fmt(vec(pl + P_POS))}")

# ---- H: actor catch -----------------------------------------------------------------------
log("=== H: safe spot 120 u above another victim while passed out")
time.sleep(1.0)
others = [v for v in victims if v != V and i32(v + V_STATE) == 4]
if others and pick_up(V):
    B = others[0]
    bp = vec(B + V_POS)
    log(f"H: target victim {B:#x} at {fmt(bp)}")
    time.sleep(1.0)
    into_fire()

    def put():
        target = (bp[0], bp[1] + 120.0, bp[2])
        wvec(pl + P_SNAP, target)
        log(f"H: wrote snapshot {fmt(target)}")
        return target

    pass_out_and_reset("H", put)
    vp = vec(V + V_POS)
    log(f"H: dropped victim at {fmt(vp)}; target victim top ~{bp[1]:.1f}+height; "
        f"dy over target = {vp[1] - bp[1]:.1f} (0 = floor, > 0 = on the actor)")
else:
    log("H: skipped (no idle second victim or no pickup)")

log(pline("end"))
log("DONE")
