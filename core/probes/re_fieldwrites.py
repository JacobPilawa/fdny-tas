"""RE helper (demos spike): every instruction in an address range that reads or writes
[reg + <offset>], by linear disassembly, named from research.db.

    python -I re_fieldwrites.py <lo hex> <hi hex> <offset hex>
"""
import bisect
import sqlite3
import sys

import capstone
import pefile

pe = pefile.PE(r"C:\fdny-decomp\FDNY SP 1.1\FDNYFirefighter.exe")
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
db = sqlite3.connect(r"C:\fdny-decomp\research\research.db")
rows = sorted((int(a, 16), n) for a, n in db.execute(
    "select address, name from symbols where kind='function' and address like '0x%'"))
keys = [r[0] for r in rows]
lo, hi, off = int(sys.argv[1], 16), int(sys.argv[2], 16), int(sys.argv[3], 16)
needle = f"+ 0x{off:x}]"
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.skipdata = True
for ins in md.disasm(bytes(img[lo - base:hi - base]), lo):
    if needle in ins.op_str:
        k = bisect.bisect_right(keys, ins.address) - 1
        kind = "WRITE" if ins.op_str.split(",")[0].strip().endswith(needle) and ins.mnemonic.startswith(("mov", "and", "or", "inc", "dec", "add", "sub", "fst", "fistp")) else "read "
        print(f"{kind} 0x{ins.address:08x}  {ins.mnemonic} {ins.op_str:42s} {rows[k][1]}+0x{ins.address - rows[k][0]:x}")
