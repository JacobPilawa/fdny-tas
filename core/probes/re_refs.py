"""RE helper (demos spike): code references to a data address (any instruction embedding
it), named from research.db.  python -I re_refs.py <hexaddr> ..."""
import bisect
import sqlite3
import struct
import sys

import pefile

pe = pefile.PE(r"C:\fdny-decomp\FDNY SP 1.1\FDNYFirefighter.exe")
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
db = sqlite3.connect(r"C:\fdny-decomp\research\research.db")
rows = sorted((int(a, 16), n) for a, n in db.execute(
    "select address, name from symbols where kind='function' and address like '0x%'"))
keys = [r[0] for r in rows]
for t in sys.argv[1:]:
    v = int(t, 16)
    print(f"== refs to 0x{v:08x}")
    pat = struct.pack("<I", v)
    for s in pe.sections:
        if not s.Characteristics & 0x20000000:
            continue
        lo, hi = s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize
        i = img.find(pat, lo, hi)
        while i >= 0:
            k = bisect.bisect_right(keys, base + i) - 1
            print(f"  0x{base + i:08x}  {rows[k][1]}+0x{base + i - rows[k][0]:x}")
            i = img.find(pat, i + 1, hi)
