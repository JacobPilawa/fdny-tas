"""VM-only exploration: walk GameSession's view list in a running game and
report each element's +0x18 and the geCamera* it leads to.

python -I probe_camera_list.py      (game running, in a level)
"""
import struct
import sys

import pymem

pm = pymem.Pymem("FDNYFirefighter.exe")
u32 = lambda a: struct.unpack("<I", pm.read_bytes(a, 4))[0]
f32 = lambda a: struct.unpack("<f", pm.read_bytes(a, 4))[0]

gs = u32(0x00502AAC)
print(f"GS={gs:#x}")
for head_off in (0x54, 0x58, 0x5C, 0x60, 0x64, 0x68):
    try:
        print(f"  [GS+{head_off:#x}] = {u32(gs + head_off):#x}")
    except Exception as e:
        print(f"  [GS+{head_off:#x}] unreadable {e}")
node = u32(gs + 0x64)
for i in range(0):
    if not node:
        break
    try:
        x = u32(node)
        nxt = u32(node + 4)
        line = f"node {i}: node={node:#x} X={x:#x} next={nxt:#x}"
        try:
            st = u32(x + 0x18)
            c = u32(x + 0x7C)
            g = u32(c + 0x5C) if c else 0
            fov = f32(g) if g else None
            deg = f32(x + 0x80)
            line += f" X+0x18={st} C={c:#x} G={g:#x} [G+0]={fov} deg={deg}"
        except Exception as e:
            line += f" (elem read failed: {e})"
        print(line)
        node = nxt
    except Exception as e:
        print(f"node {i}: {node:#x} unreadable {e}")
        break
eg = u32(0x00503A78)
player = u32(eg + 0x30)
try:
    c = u32(player + 0x7C)
    g = u32(c + 0x5C)
    print(f"player={player:#x} player+0x18={u32(player + 0x18)} C=[player+0x7C]={c:#x} G=[C+0x5C]={g:#x} [G+0]={f32(g)} deg=[player+0x80]={f32(player + 0x80)}")
except Exception as e:
    print(f"player chain failed: {e}")
sys.stdout.flush()
