"""Spike probe (demos / ghosts): every code site that touches the MT19937 cursor directly
(g_mtWordsRemaining 0x004FF370, g_mtNextPtr 0x005023EC), i.e. draws that inline the
generator instead of calling MT19937_NextU32 0x00404A70, plus every call to NextU32 and
to the Rand_* wrappers, with the containing function's research.db name. Read-only.

    python -I core/probes/demo_rngsites.py > core/evidence/demos/rng_sites.txt
"""
import bisect
import os
import sqlite3
import struct

import pefile

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
pe = pefile.PE(os.path.join(REPO, "FDNY SP 1.1", "FDNYFirefighter.exe"))
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
text = [(s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize) for s in pe.sections if s.Characteristics & 0x20000000]
db = sqlite3.connect(os.path.join(REPO, "research", "research.db"))
names = sorted((int(a, 16), n) for a, n in db.execute(
    "select address, name from symbols where kind='function' and address like '0x%'"))
keys = [a for a, _ in names]


def owner(va):
    k = bisect.bisect_right(keys, va) - 1
    a, n = names[k]
    return f"{n}+0x{va - a:x}" + (" (far: unnamed function?)" if va - a > 0x800 else "")


def refs(va):
    pat = struct.pack("<I", va)
    for lo, hi in text:
        i = img.find(pat, lo, hi)
        while i >= 0:
            yield base + i
            i = img.find(pat, i + 1, hi)


def calls(target):
    for lo, hi in text:
        for i in range(lo, hi - 5):
            if img[i] == 0xE8 and (base + i + 5 + struct.unpack_from("<i", img, i + 1)[0]) == target:
                yield base + i


for label, va in (("g_mtWordsRemaining 0x004FF370", 0x004FF370), ("g_mtNextPtr 0x005023EC", 0x005023EC)):
    print(f"== code referencing {label}")
    for r in refs(va):
        print(f"  0x{r:08x}  {owner(r)}")
for label, va in (("MT19937_NextU32", 0x00404A70), ("Rand_PlusMinus1/Float01", 0x00404AC0),
                  ("Rand_Below", 0x00404B30), ("Rand_FloatRange", 0x00498B30), ("Rand_Seed", 0x00404B50)):
    sites = list(calls(va))
    print(f"== calls to {label} 0x{va:08x}: {len(sites)}")
    by = {}
    for s in sites:
        by.setdefault(owner(s).split("+")[0], []).append(s)
    for n, ss in sorted(by.items()):
        print(f"  {len(ss):3d}  {n}")
