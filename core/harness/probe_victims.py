"""VM-only exploration: in a running level, list entities of kind 2 (victims,
per PlayerRescueEnt::EndMission 0x0044FBA0) from GameSession's entity list
(+0x64) with their positions/state, the level's victim count, and the two
candidate RescueBot enabled bytes.

python -I probe_victims.py
"""
import struct

import pymem

pm = pymem.Pymem("FDNYFirefighter.exe")
u32 = lambda a: struct.unpack("<I", pm.read_bytes(a, 4))[0]
i32 = lambda a: struct.unpack("<i", pm.read_bytes(a, 4))[0]
f32 = lambda a: struct.unpack("<f", pm.read_bytes(a, 4))[0]

gs = u32(0x00502AAC)
pre = u32(u32(0x00503A78) + 0x30)
print(f"PRE={pre:#x} total_victims={i32(pre + 0x3528)} rescued={i32(pre + 0x352C)} bot_claimed={i32(pre + 0x3530)}")
for off in (0x3424, 0x32E4):
    print(f"  candidate RescueBot enabled byte PRE+{off:#x} = {pm.read_bytes(pre + off, 1)[0]}")

kinds = {}
node, n = u32(gs + 0x64), 0
while node and n < 5000:
    try:
        x = u32(node)
        k = i32(x + 0x18)
        kinds[k] = kinds.get(k, 0) + 1
        if k == 2:
            pos = (f32(x + 0x4C), f32(x + 0x50), f32(x + 0x54))
            name = ""
            try:
                d = u32(x + 0x150)
                s = u32(d + 0x10)
                name = pm.read_bytes(s, 32).split(b"\0", 1)[0].decode("latin-1")
            except Exception:
                pass
            print(f"victim #{n} elem={x:#x} pos=({pos[0]:.2f},{pos[1]:.2f},{pos[2]:.2f}) "
                  f"state={i32(x + 0x120)} spots={i32(x + 0x204)} name={name!r}")
        node = u32(node + 4)
        n += 1
    except Exception as e:
        print(f"stop at node {n}: {e}")
        break
print(f"walked {n} nodes; kinds: {dict(sorted(kinds.items()))}")
