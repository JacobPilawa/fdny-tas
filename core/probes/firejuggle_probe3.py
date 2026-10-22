"""Fire-juggling probe, round 3: the two unknowns left after round 2.

Runs beside tests/probes/probe_firejuggle3.yaml (zz_juggleprobe.asi installed, bank,
RescueBot off; the test holds the game ~15 min).

  I  liftable catch: while passed out, put the safe spot +0x3400 150 u above a
     BModelEnt_Liftable (vtable 0x004E0DF8, kind 4, centre = Xform translation +0xD0);
     does the dropped survivor land on it (a BSP sub-model) or on the floor?
  J  the 0x7530 cool-down: find MiscEmitterEnt (vtable 0x004E1158) whose type
     ([[ent+0x84]+8]) is 2 = MiscEmitterFXImpl (impl at ent+0x78, position impl+0x74),
     on bank or, failing that, on the other maps (typed `loadmap`). There: burn the
     player, stand at points around the emitter and measure the cooling rate
     (normal: 0.1 per second); the asi logs COOL / ADDHANDLER calls.

python -I firejuggle_probe3.py <out.log>
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
P_POS, P_DISP, P_VEL, P_GROUND, P_STATE = 0x4C, 0x90, 0xFC, 0x12C, 0x18C
P_IMPULSE, P_SNAP, P_CARRY, P_BURN, P_FLINCH = 0x5B8, 0x3400, 0x3534, 0x3538, 0x353C
V_POS, V_STATE, F_POS = 0x4C, 0x120, 0x7C
VT_LIFTABLE, VT_EMITTER = 0x004E0DF8, 0x004E1158
MAPS = ["bank", "bar_r", "boat_a", "boat_b", "City_h", "const_h", "dock_h", "gas_r", "mbase",
        "oil_rig_A", "oil_rig_B", "oil_rig_C", "oil_rig_D", "oil_rig_h", "subway_a", "subway_b", "training"]

out = open(sys.argv[1] if len(sys.argv) > 1 else "firejuggle3.log", "w", encoding="utf-8")


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
        res.append(e)
        node = u32(node + 4)
        n += 1
    return res


def wait_for(cond, secs):
    t = time.time()
    while time.time() - t < secs:
        try:
            if cond():
                return True
        except Exception:
            pass
        time.sleep(0.005)
    return False


def teleport(p):
    wvec(pl + P_POS, p)
    for off in (P_VEL, P_DISP, P_IMPULSE):
        wvec(pl + off, (0.0, 0.0, 0.0))


def pline(tag):
    return (f"{tag} pos={fmt(vec(pl + P_POS))} snap={fmt(vec(pl + P_SNAP))} burn={f32(pl + P_BURN):.3f} "
            f"flinch={rb(pl + P_FLINCH, 4).hex()} state={i32(pl + P_STATE)} ground={rb(pl + P_GROUND, 1)[0]} "
            f"carry={u32(pl + P_CARRY):#x}")


def console(text):
    inputs.type_text("`")
    time.sleep(0.2)
    inputs.type_text(text)
    inputs.tap("enter")
    time.sleep(0.1)
    inputs.type_text("`")


def find_fire():
    for e in entities():
        if i32(e + 0x18) != 6:
            continue
        fp = vec(e + F_POS)
        if fp == (0.0, 0.0, 0.0):
            continue
        teleport((fp[0], fp[1] + 30.0, fp[2]))
        b0 = f32(pl + P_BURN)
        if wait_for(lambda: f32(pl + P_BURN) > b0 + 0.02 or i32(pl + P_STATE) == 6, 0.6):
            return e
    return None


def emitters():
    res = []
    for e in entities():
        if u32(e) != VT_EMITTER:
            continue
        try:
            typ = i32(u32(e + 0x84) + 8)
        except Exception:
            typ = None
        impl = u32(e + 0x78)
        pos = vec(impl + 0x74) if impl else None
        res.append((e, typ, impl, pos))
    return res


def load(m):
    console(f"loadmap {m}")
    ok = wait_for(lambda: map_name().lower() == m.lower() and player(), 90)
    time.sleep(10)
    return ok


# ---- wait for bank ---------------------------------------------------------------------
if not wait_for(lambda: map_name().lower() == "bank" and player(), 600):
    log(f"FAIL map never became bank ({map_name()!r})")
    sys.exit(1)
log("bank loaded; settling 8 s")
time.sleep(8)
pl = player()
home = vec(pl + P_POS)

# ---- I: liftable catch --------------------------------------------------------------------
log("=== I: liftables on bank")
lifts = [e for e in entities() if u32(e) == VT_LIFTABLE]
for e in lifts:
    log(f"  liftable {e:#x} kind={i32(e + 0x18)} centre(+0xD0)={fmt(vec(e + 0xD0))} +0x4C={fmt(vec(e + 0x4C))}")
victims = [e for e in entities() if i32(e + 0x18) == 2]
fire = find_fire()
log(f"fire {fire:#x} at {fmt(vec(fire + F_POS))}" if fire else "no fire")
teleport(home)
wait_for(lambda: f32(pl + P_BURN) < 0.001, 15)
time.sleep(1.0)
V = victims[0]
if lifts and fire:
    for L in lifts[:3]:
        lc = vec(L + 0xD0)
        vp = vec(V + V_POS)
        teleport((vp[0], vp[1] + 20.0, vp[2]))
        if not wait_for(lambda: u32(pl + P_CARRY) == V, 2):
            log("I: pickup failed")
            break
        time.sleep(1.0)
        fp = vec(fire + F_POS)
        teleport((fp[0], fp[1] + 30.0, fp[2]))
        if not wait_for(lambda: i32(pl + P_STATE) == 6, 8):
            log("I: never passed out")
            break
        lc = vec(L + 0xD0)
        target = (lc[0], lc[1] + 150.0, lc[2])
        wvec(pl + P_SNAP, target)
        log(f"I: liftable {L:#x} centre {fmt(lc)}; wrote snapshot {fmt(target)}")
        wait_for(lambda: i32(pl + P_STATE) != 6, 10)
        time.sleep(0.4)
        dp = vec(V + V_POS)
        log(f"I: survivor at {fmt(dp)}: {dp[1] - lc[1]:+.1f} u relative to the liftable centre; "
            f"liftable now {fmt(vec(L + 0xD0))}")
        teleport(home)
        time.sleep(1.5)
else:
    log("I: skipped (no liftable or no fire)")

# ---- J: the 0x7530 cool-down ---------------------------------------------------------------
log("=== J: MiscEmitterFXImpl (type 2) search")
found = None
for m in MAPS:
    if m != "bank":
        log(f"loading {m}")
        if not load(m):
            log(f"  {m}: did not load (map={map_name()!r})")
            continue
        pl = player()
    ems = emitters()
    types = {}
    for e, t, impl, pos in ems:
        types[t] = types.get(t, 0) + 1
    log(f"  {m}: {len(ems)} MiscEmitterEnt, types {dict(sorted(types.items(), key=lambda kv: str(kv[0])))}")
    fx = [x for x in ems if x[1] == 2]
    for e, t, impl, pos in fx[:10]:
        log(f"    type 2 emitter {e:#x} impl={impl:#x} pos={fmt(pos)} enable(+0x45)={rb(impl + 0x45, 1)[0]}")
    if fx:
        found = (m, fx)
        break

if found:
    m, fx = found
    home = vec(pl + P_POS)
    fire = find_fire()
    if not fire:
        log(f"J: no fire burns on {m}; cannot measure")
    else:
        log(f"J: fire {fire:#x} on {m}")
        for e, t, impl, pos in fx[:4]:
            for off in ((0, 0, 0), (0, -60, 0), (100, 0, 0), (-100, 0, 0), (0, 0, 100), (0, 0, -100), (0, 100, 0)):
                fp = vec(fire + F_POS)
                teleport((fp[0], fp[1] + 30.0, fp[2]))
                wait_for(lambda: f32(pl + P_BURN) > 0.6, 3)
                p = (pos[0] + off[0], pos[1] + off[1], pos[2] + off[2])
                teleport(p)
                time.sleep(0.2)
                b0, t0 = f32(pl + P_BURN), time.time()
                time.sleep(1.5)
                b1, t1 = f32(pl + P_BURN), time.time()
                rate = (b0 - b1) / (t1 - t0)
                log(f"  J emitter {e:#x} offset {off}: player {fmt(vec(pl + P_POS))} burn {b0:.3f} -> {b1:.3f} "
                    f"= {rate:.3f}/s {'FASTER' if rate > 0.15 else ''}")
                if i32(pl + P_STATE) == 6:
                    wait_for(lambda: i32(pl + P_STATE) != 6, 10)
        teleport(home)
        time.sleep(1.0)
        b0, t0 = f32(pl + P_BURN), time.time()
        log(f"J control at spawn: burn {b0:.3f}")
else:
    log("J: no type-2 emitter on any map")

log("DONE")
