"""VM-only exploration for the item-9 snapshot: read the candidate fields live.

python -I probe_state.py [--kinds]
"""
import struct
import sys

import pymem

pm = pymem.Pymem("FDNYFirefighter.exe")
u32 = lambda a: struct.unpack("<I", pm.read_bytes(a, 4))[0]
i32 = lambda a: struct.unpack("<i", pm.read_bytes(a, 4))[0]
f32 = lambda a: struct.unpack("<f", pm.read_bytes(a, 4))[0]

gs = u32(0x00502AAC)
pre = u32(u32(0x00503A78) + 0x30)
t = f32(pre + 0x343C)
print(f"timer PRE+0x343C = {t:.2f} s = {int(t)//60}:{int(t)%60:02d}   state+0x3438={i32(pre+0x3438)}")
print(f"vel = ({f32(pre+0xFC):.2f}, {f32(pre+0x100):.2f}, {f32(pre+0x104):.2f})  disp = ({f32(pre+0x90):.3f}, {f32(pre+0x94):.3f}, {f32(pre+0x98):.3f})")
print(f"pitch = {f32(pre+0x3430):.2f}  yaw = {f32(pre+0x3434):.2f}  state = {i32(pre+0x18C)} ground = {pm.read_bytes(pre+0x12C,1)[0]}")
mm = gs + 0x3C8
cur = u32(mm + 0x10)
arr, cnt = u32(mm + 0x158), i32(mm + 0x15C)
idx = next((i for i in range(min(cnt, 32)) if u32(arr + 4 * i) == cur), None)
print(f"menu: current screen ptr {cur:#x}, index {idx} of {cnt}; game.state = {i32(u32(gs + 0x6F0) if False else gs + 0x6F0)}")

if "--kinds" in sys.argv:
    kinds = {}
    node = u32(gs + 0x64)
    while node:
        x = u32(node)
        k = i32(x + 0x18)
        name = ""
        try:
            name = pm.read_bytes(u32(u32(x + 0x150) + 0x10), 24).split(b"\0", 1)[0].decode("latin-1")
        except Exception:
            pass
        kinds.setdefault(k, []).append((x, name))
        vt = u32(x)
        if vt in (0x004E1780, 0x004E1774):
            print(f"FireRescueEnt kind={k} elem={x:#x} vptr={vt:#x} pos=({f32(x+0x4C):.1f},{f32(x+0x50):.1f},{f32(x+0x54):.1f}) mode+0x70={i32(x+0x70)} intensity+0x128={f32(x+0x128):.3f}")
        node = u32(node + 4)
    for k, v in sorted(kinds.items()):
        names = sorted({n for _, n in v})[:8]
        vts = sorted({u32(x) for x, _ in v})[:6]
        print(f"kind {k}: {len(v)}  vptrs {[hex(a) for a in vts]}")
        if k in (6, 7):
            for x, n in v[:3]:
                print(f"    {n:16} pos=({f32(x+0x4C):.1f},{f32(x+0x50):.1f},{f32(x+0x54):.1f}) "
                      f"+0x70={i32(x+0x70)} +0x128={f32(x+0x128):.3f} +0x258={f32(x+0x258):.3f}")
