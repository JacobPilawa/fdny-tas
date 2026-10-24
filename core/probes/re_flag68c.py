"""RE helper (autosplitter): every instruction that writes or tests GameSession+0x68C (the
pending level-change byte), with the containing function's research.db name.

    python -I core/probes/re_flag68c.py
"""
import bisect
import sqlite3

import capstone
import pefile

pe = pefile.PE(r"C:\fdny-decomp\FDNY SP 1.1\FDNYFirefighter.exe")
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
db = sqlite3.connect(r"C:\fdny-decomp\research\research.db")
rows = sorted((int(a, 16), n) for a, n in db.execute(
    "select address, name from symbols where kind='function' and address like '0x%'"))
keys = [r[0] for r in rows]


def owner(v):
    k = bisect.bisect_right(keys, v) - 1
    return f"{rows[k][1]}+0x{v - rows[k][0]:x}"


md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
for s in pe.sections:
    if not s.Characteristics & 0x20000000:
        continue
    lo, hi = s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize
    # linear sweep from every byte that starts a disp32 of 0x68C (cheap: find the bytes first)
    i = img.find(b"\x8c\x06\x00\x00", lo, hi)
    seen = set()
    while i >= 0:
        for back in range(1, 4):  # opcode + modrm (+ sib) before the displacement
            start = i - back - 1
            for ins in md.disasm(bytes(img[start:start + 15]), base + start, 1):
                if "0x68c" in ins.op_str and ins.address not in seen and ins.address + ins.size > base + i:
                    seen.add(ins.address)
                    print(f"0x{ins.address:08x}  {ins.mnemonic} {ins.op_str:40s} {owner(ins.address)}")
        i = img.find(b"\x8c\x06\x00\x00", i + 1, hi)
