"""Fire-juggling probe (spike, not part of the suite).

Runs beside tests/probes/probe_firejuggle.yaml, which loads bank (RescueBot off) and
holds the game open. Reads/writes game memory with pymem to check, live, the static
reading of "fire juggling":

  - Think (0x00452570) copies the player position into +0x3400 each frame only when
    grounded (+0x12C), O2 (+0x3450) > 0.5, no fire flinch flag (+0x353C..F) and the
    burn meter (+0x3538) < 0.1;
  - TakeFireDamage adds to the burn meter; > 1.0 -> PassOut(1) -> state +0x18C = 6;
  - ~4 s later Verb_Reset copies +0x3400 into the position, DropAtPlayer puts the
    carried victim (+0x3534) there (SnapToFloor: +48 up, sweep 1000 down), -30 on X;
    then ResetLocomotionState moves the player to the checkpoint +0x534.

Phases: A plain pass-out drop; B juggle (burn, step out, burn again); C control (step
out until the meter cools, the spot moves); D floor search (+0x3400 written while
passed out: 600 u above a floor, then outside the map).

The player is moved by writing position and zeroing velocity / displacement / impulse
(what fd_tp_load does). Writing +0x3400 in phase D is a probe-only test input.

python -I firejuggle_probe.py <out.log>
"""
import math
import struct
import sys
import time

import pymem

GS_SLOT, EVERGAME = 0x00502AAC, 0x00503A78
P_POS, P_DISP, P_VEL, P_GROUND, P_STATE = 0x4C, 0x90, 0xFC, 0x12C, 0x18C
P_IMPULSE, P_CKPT, P_SNAP, P_O2 = 0x5B8, 0x534, 0x3400, 0x3450
P_CARRY, P_BURN, P_FLINCH, P_RESULT = 0x3534, 0x3538, 0x353C, 0x3438
V_POS, V_STATE, F_MODE, F_INT, F_POS = 0x4C, 0x120, 0x70, 0x128, 0x7C  # fire centre +0x7C (FireRescueEnt touch 0x004483A0)

out = open(sys.argv[1] if len(sys.argv) > 1 else "firejuggle.log", "w", encoding="utf-8")


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


def wvec(a, v):
    pm.write_bytes(a, struct.pack("<3f", *v), 12)


def fmt(v):
    return "(" + ", ".join(f"{c:.1f}" for c in v) + ")"


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def map_name():
    try:
        p = u32(u32(u32(GS_SLOT) + 0x688))
        return rb(p + 0xC, 16).split(b"\0", 1)[0].decode("latin-1")
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


def pstate(pl):
    return dict(pos=vec(pl + P_POS), snap=vec(pl + P_SNAP), ckpt=vec(pl + P_CKPT),
                burn=f32(pl + P_BURN), flinch=rb(pl + P_FLINCH, 4), state=i32(pl + P_STATE),
                ground=rb(pl + P_GROUND, 1)[0], o2=f32(pl + P_O2), carry=u32(pl + P_CARRY),
                result=i32(pl + P_RESULT))


def pline(pl, tag=""):
    s = pstate(pl)
    return (f"{tag} pos={fmt(s['pos'])} snap={fmt(s['snap'])} burn={s['burn']:.3f} "
            f"flinch={s['flinch'].hex()} state={s['state']} ground={s['ground']} o2={s['o2']:.2f} "
            f"carry={s['carry']:#x} ckpt={fmt(s['ckpt'])}")


def teleport(pl, p):
    wvec(pl + P_POS, p)
    for off in (P_VEL, P_DISP, P_IMPULSE):
        wvec(pl + off, (0.0, 0.0, 0.0))


def watch(pl, secs, every=0.25, tag="", until=None):
    """Log the player fields every `every` s for `secs` s (or until until(state))."""
    t0 = time.time()
    last = -1
    while time.time() - t0 < secs:
        s = pstate(pl)
        if until and until(s):
            return s
        k = int((time.time() - t0) / every)
        if k != last:
            last = k
            log(pline(pl, f"  {tag} t={time.time() - t0:5.2f}"))
        time.sleep(0.005)
    return pstate(pl)


# ---- wait for bank -------------------------------------------------------------------
t0 = time.time()
while time.time() - t0 < 600:
    if map_name().lower() == "bank" and player():
        break
    time.sleep(1)
else:
    log(f"FAIL map never became bank (map={map_name()!r})")
    sys.exit(1)
log("bank loaded; settling 8 s")
time.sleep(8)
pl = player()
log(pline(pl, "start"))

victims = [e for e, k in entities() if k == 2]
fires = [e for e, k in entities() if k == 6]
log(f"victims={len(victims)} fires={len(fires)}")
for v in victims:
    log(f"  victim {v:#x} pos={fmt(vec(v + V_POS))} state={i32(v + V_STATE)}")
modes = {}
for f in fires:
    modes[i32(f + F_MODE)] = modes.get(i32(f + F_MODE), 0) + 1
log(f"fire modes: {dict(sorted(modes.items()))}")
# Mode 4 (advancing) first, then by intensity; whichever actually burns is used.
burning = sorted((f for f in fires if i32(f + F_MODE) != 5),
                 key=lambda f: (i32(f + F_MODE) != 4, -f32(f + F_INT)))
for f in burning[:30]:
    log(f"  fire {f:#x} pos={fmt(vec(f + F_POS))} mode={i32(f + F_MODE)} intensity={f32(f + F_INT):.2f}")

home = vec(pl + P_POS)  # level spawn, a known grounded spot
V = victims[0]


def pick_up(v):
    vp = vec(v + V_POS)
    for dy in (20.0, 40.0, 60.0):
        teleport(pl, (vp[0], vp[1] + dy, vp[2]))
        t = time.time()
        while time.time() - t < 1.5:
            if u32(pl + P_CARRY) == v:
                log(f"picked up {v:#x} at {fmt(vp)}")
                return True
            time.sleep(0.01)
    log(f"could not pick up {v:#x} (carry={u32(pl + P_CARRY):#x})")
    return False


def find_hot_fire(exclude=()):
    """Teleport onto burning fires until the burn meter rises; return the fire."""
    for f in burning:
        if f in exclude:
            continue
        fp = vec(f + F_POS)
        before = f32(pl + P_BURN)
        teleport(pl, (fp[0], fp[1] + 30.0, fp[2]))
        t = time.time()
        while time.time() - t < 0.6:
            if f32(pl + P_BURN) > before + 0.02 or i32(pl + P_STATE) == 6:
                log(f"fire {f:#x} at {fmt(fp)} burns (burn {before:.3f} -> {f32(pl + P_BURN):.3f})")
                return f
            time.sleep(0.01)
    return None


def stand(p, secs, tag):
    teleport(pl, p)
    return watch(pl, secs, 0.25, tag)


def wait_reset(tag):
    s = watch(pl, 8, 0.5, tag + " passing-out", until=lambda s: s["state"] == 6)
    if s["state"] != 6:
        log(f"{tag}: never passed out")
        return None
    log(pline(pl, f"{tag} PASSED OUT"))
    snap = vec(pl + P_SNAP)
    s = watch(pl, 10, 0.5, tag + " out", until=lambda s: s["state"] != 6)
    time.sleep(0.3)
    log(pline(pl, f"{tag} RESET"))
    return snap


def report_drop(tag, v, expect):
    vp = vec(v + V_POS)
    log(f"{tag}: victim {v:#x} state={i32(v + V_STATE)} at {fmt(vp)}; "
        f"snapshot at pass-out {fmt(expect)}; dx={vp[0] - expect[0]:.1f} dy={vp[1] - expect[1]:.1f} "
        f"dz={vp[2] - expect[2]:.1f}; player now {fmt(vec(pl + P_POS))} ckpt {fmt(vec(pl + P_CKPT))}")


# ---- A: plain pass-out ---------------------------------------------------------------
log("=== A: carry, stand, walk into fire, pass out")
if not pick_up(V):
    sys.exit(1)
s = watch(pl, 1.5, 0.5, "A settle")
spotA = vec(pl + P_SNAP)
log(f"A: snapshot before the fire {fmt(spotA)} (player {fmt(vec(pl + P_POS))})")
F = find_hot_fire()
if F is None:
    log("FAIL no fire burns the player")
    sys.exit(1)
fp = vec(F + F_POS)
teleport(pl, (fp[0], fp[1] + 30.0, fp[2]))
snapA = wait_reset("A")
if snapA:
    log(f"A: snapshot unchanged by the fire: {dist(snapA, spotA) < 0.5} ({fmt(spotA)} -> {fmt(snapA)})")
    report_drop("A", V, snapA)

# ---- B: juggle -----------------------------------------------------------------------
log("=== B: juggle: burn, step out to the spawn, burn again")
time.sleep(1.0)
if pick_up(V):
    watch(pl, 1.5, 0.5, "B settle")
    spotB = vec(pl + P_SNAP)
    log(f"B: snapshot before the fire {fmt(spotB)}")
    teleport(pl, (fp[0], fp[1] + 30.0, fp[2]))
    t = time.time()
    while f32(pl + P_BURN) < 0.45 and time.time() - t < 3:
        time.sleep(0.005)
    log(pline(pl, "B burnt"))
    stand(home, 2.0, "B out-of-fire")
    log(f"B: snapshot after 2 s on clean ground at spawn {fmt(vec(pl + P_SNAP))} "
        f"(moved to spawn: {dist(vec(pl + P_SNAP), home) < 40})")
    teleport(pl, (fp[0], fp[1] + 30.0, fp[2]))
    snapB = wait_reset("B")
    if snapB:
        log(f"B: snapshot kept the pre-fire spot: {dist(snapB, spotB) < 0.5}")
        report_drop("B", V, snapB)

# ---- C: control, let the meter cool ---------------------------------------------------
log("=== C: control: burn, step out until the meter is < 0.1, burn again")
time.sleep(1.0)
if pick_up(V):
    watch(pl, 1.5, 0.5, "C settle")
    spotC = vec(pl + P_SNAP)
    teleport(pl, (fp[0], fp[1] + 30.0, fp[2]))
    t = time.time()
    while f32(pl + P_BURN) < 0.3 and time.time() - t < 3:
        time.sleep(0.005)
    log(pline(pl, "C burnt"))
    teleport(pl, home)
    t = time.time()
    moved_at = None
    while time.time() - t < 8:
        if moved_at is None and dist(vec(pl + P_SNAP), spotC) > 40:
            moved_at = time.time() - t
            log(pline(pl, f"C snapshot moved after {moved_at:.2f} s"))
        time.sleep(0.005)
    log(pline(pl, "C cooled"))
    teleport(pl, (fp[0], fp[1] + 30.0, fp[2]))
    snapC = wait_reset("C")
    if snapC:
        report_drop("C", V, snapC)

# ---- D: floor search -----------------------------------------------------------------
for tag, lift, far in (("D1", 600.0, False), ("D2", 0.0, True)):
    log(f"=== {tag}: pass out, then write +0x3400 = " + ("spawn + 600 up" if not far else "outside the map"))
    time.sleep(1.0)
    if not pick_up(V):
        continue
    watch(pl, 1.0, 0.5, f"{tag} settle")
    teleport(pl, (fp[0], fp[1] + 30.0, fp[2]))
    s = watch(pl, 8, 0.5, tag, until=lambda s: s["state"] == 6)
    if s["state"] != 6:
        log(f"{tag}: never passed out")
        continue
    target = (home[0], home[1] + lift, home[2]) if not far else (home[0] + 30000.0, home[1], home[2] + 30000.0)
    wvec(pl + P_SNAP, target)
    log(f"{tag}: wrote snapshot {fmt(target)}")
    watch(pl, 10, 0.5, tag + " out", until=lambda s: s["state"] != 6)
    time.sleep(0.3)
    report_drop(tag, V, target)

log(pline(pl, "end"))
log(f"victims at end: " + "; ".join(f"{v:#x} {fmt(vec(v + V_POS))} st={i32(v + V_STATE)}"
                                     for v, k in entities() if k == 2))
log("DONE")
