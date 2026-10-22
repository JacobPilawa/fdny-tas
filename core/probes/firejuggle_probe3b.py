"""Fire-juggling probe 3b: the burn cool-down (msg 0x7530) under bank's two
MiscEmitterFXImpl (type 2) emitters. The emitter position is impl+0x28 (Xform
translation; impl = ent+0x78), found live in round 3 (impl+0x74 reads zero). Run
against a game already on bank (probe_firejuggle3.yaml holds it open).

python -I firejuggle_probe3b.py <out.log>
"""
import struct
import sys
import time

import pymem

GS_SLOT, EVERGAME, VT_EMITTER = 0x00502AAC, 0x00503A78, 0x004E1158
P_POS, P_DISP, P_VEL, P_IMPULSE, P_STATE, P_BURN = 0x4C, 0x90, 0xFC, 0x5B8, 0x18C, 0x3538

out = open(sys.argv[1], "w", encoding="utf-8")


def log(msg):
    line = f"{time.strftime('%H:%M:%S')} {msg}"
    print(line, flush=True)
    out.write(line + "\n")
    out.flush()


pm = pymem.Pymem("FDNYFirefighter.exe")
u32 = lambda a: struct.unpack("<I", pm.read_bytes(a, 4))[0]
i32 = lambda a: struct.unpack("<i", pm.read_bytes(a, 4))[0]
f32 = lambda a: struct.unpack("<f", pm.read_bytes(a, 4))[0]
vec = lambda a: struct.unpack("<3f", pm.read_bytes(a, 12))
wvec = lambda a, v: pm.write_bytes(a, struct.pack("<3f", *v), 12)
fmt = lambda v: "(" + ", ".join(f"{c:.1f}" for c in v) + ")"
pl = u32(u32(EVERGAME) + 0x30)


def teleport(p):
    wvec(pl + P_POS, p)
    for off in (P_VEL, P_DISP, P_IMPULSE):
        wvec(pl + off, (0.0, 0.0, 0.0))


def ents():
    node = u32(u32(GS_SLOT) + 0x64)
    while node:
        yield u32(node)
        node = u32(node + 4)


fire = None
home = vec(pl + P_POS)
for e in ents():
    if i32(e + 0x18) == 6 and vec(e + 0x7C) != (0.0, 0.0, 0.0):
        fp = vec(e + 0x7C)
        teleport((fp[0], fp[1] + 30.0, fp[2]))
        b0 = f32(pl + P_BURN)
        time.sleep(0.6)
        if f32(pl + P_BURN) > b0 + 0.02:
            fire = e
            break
log(f"fire {fire:#x} at {fmt(vec(fire + 0x7C))}")
fp = vec(fire + 0x7C)


def measure(p, tag):
    teleport((fp[0], fp[1] + 30.0, fp[2]))
    t = time.time()
    while f32(pl + P_BURN) < 0.6 and time.time() - t < 3:
        time.sleep(0.005)
    teleport(p)
    time.sleep(0.3)
    b0, t0 = f32(pl + P_BURN), time.time()
    time.sleep(1.5)
    b1, t1 = f32(pl + P_BURN), time.time()
    log(f"  {tag}: player {fmt(vec(pl + P_POS))} burn {b0:.3f} -> {b1:.3f} = {(b0 - b1) / (t1 - t0):.3f}/s")
    if i32(pl + P_STATE) == 6:
        while i32(pl + P_STATE) == 6:
            time.sleep(0.05)


measure(home, "control at spawn")
for e in ents():
    if u32(e) != VT_EMITTER:
        continue
    typ = i32(u32(e + 0x84) + 8)
    impl = u32(e + 0x78)
    pos = vec(impl + 0x28)
    log(f"emitter {e:#x} type {typ} at {fmt(pos)} enable={pm.read_bytes(impl + 0x45, 1)[0]}")
    if typ != 2:
        continue
    for dy in (-170.0, -120.0, -60.0):
        for dx, dz in ((0, 0), (60, 0), (0, 60)):
            measure((pos[0] + dx, pos[1] + dy, pos[2] + dz), f"offset ({dx},{dy:.0f},{dz})")
teleport(home)
log("DONE")
