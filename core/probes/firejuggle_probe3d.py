"""Fire-juggling probe 3d: does a BModelEnt_Liftable (BSP sub-model) catch the drop?
For each of a few liftables: a pass-out drop with the safe spot 100 u above the
liftable centre (+0xD0), and a control drop 150 u to the side (same floor). Run
against a game already on bank.

python -I firejuggle_probe3d.py <out.log>
"""
import struct
import sys
import time

import pymem

GS_SLOT, EVERGAME, VT_LIFTABLE = 0x00502AAC, 0x00503A78, 0x004E0DF8
P_POS, P_DISP, P_VEL, P_IMPULSE, P_STATE, P_SNAP, P_CARRY = 0x4C, 0x90, 0xFC, 0x5B8, 0x18C, 0x3400, 0x3534

out = open(sys.argv[1], "w", encoding="utf-8")


def log(msg):
    line = f"{time.strftime('%H:%M:%S')} {msg}"
    print(line, flush=True)
    out.write(line + "\n")
    out.flush()


pm = pymem.Pymem("FDNYFirefighter.exe")
u32 = lambda a: struct.unpack("<I", pm.read_bytes(a, 4))[0]
i32 = lambda a: struct.unpack("<i", pm.read_bytes(a, 4))[0]
vec = lambda a: struct.unpack("<3f", pm.read_bytes(a, 12))
wvec = lambda a, v: pm.write_bytes(a, struct.pack("<3f", *v), 12)
fmt = lambda v: "(" + ", ".join(f"{c:.1f}" for c in v) + ")"
pl = u32(u32(EVERGAME) + 0x30)
FIRE = (144.0, 250.0, -1324.0)  # a burning spot on bank (rounds 1-3)
HOME = (-697.0, -47.8, 1196.0)


def teleport(p):
    wvec(pl + P_POS, p)
    for off in (P_VEL, P_DISP, P_IMPULSE):
        wvec(pl + off, (0.0, 0.0, 0.0))


def ents():
    node = u32(u32(GS_SLOT) + 0x64)
    while node:
        yield u32(node)
        node = u32(node + 4)


def wait(cond, secs):
    t = time.time()
    while time.time() - t < secs:
        if cond():
            return True
        time.sleep(0.005)
    return False


victim = next(e for e in ents() if i32(e + 0x18) == 2 and i32(e + 0x120) == 4)


def drop_at(spot):
    vp = vec(victim + 0x4C)
    teleport((vp[0], vp[1] + 20.0, vp[2]))
    if not wait(lambda: u32(pl + P_CARRY) == victim, 2):
        return None
    time.sleep(0.5)
    teleport(FIRE)
    if not wait(lambda: i32(pl + P_STATE) == 6, 8):
        return None
    wvec(pl + P_SNAP, spot)
    wait(lambda: i32(pl + P_STATE) != 6, 10)
    time.sleep(0.4)
    r = vec(victim + 0x4C)
    teleport(HOME)
    time.sleep(1.0)
    return r


lifts = [e for e in ents() if u32(e) == VT_LIFTABLE]
picks = [L for L in lifts if abs(vec(L + 0xD0)[1]) < 30][:5]
for L in picks:
    c = vec(L + 0xD0)
    on = drop_at((c[0], c[1] + 100.0, c[2]))
    side = drop_at((c[0] + 150.0, c[1] + 100.0, c[2]))
    log(f"liftable {L:#x} centre {fmt(c)}: drop above -> {fmt(on) if on else 'n/a'}; "
        f"control 150 u aside -> {fmt(side) if side else 'n/a'}; "
        f"height difference {on[1] - side[1]:+.1f} u" if on and side else f"liftable {L:#x}: failed")
log("DONE")
