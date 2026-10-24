"""RE helper (demos spike): for the clock / input imports, find the exe's own import thunks
(`jmp dword ptr [IAT]`) and every direct call to those thunks, named from research.db. The
first static scan (demo_scan.py) counted only `call [IAT]` sites and missed these.

    python -I re_thunkcallers.py
"""
import bisect
import sqlite3
import struct

import pefile

pe = pefile.PE(r"C:\fdny-decomp\FDNY SP 1.1\FDNYFirefighter.exe")
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
db = sqlite3.connect(r"C:\fdny-decomp\research\research.db")
rows = sorted((int(a, 16), n) for a, n in db.execute(
    "select address, name from symbols where kind='function' and address like '0x%'"))
keys = [r[0] for r in rows]
text = [(s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize) for s in pe.sections if s.Characteristics & 0x20000000]
WANT = {"timeGetTime", "GetTickCount", "QueryPerformanceCounter", "GetSystemTimeAsFileTime", "GetLocalTime",
        "GetSystemTime", "GetAsyncKeyState", "GetKeyState", "rand"}


def owner(v):
    k = bisect.bisect_right(keys, v) - 1
    return f"{rows[k][1]}+0x{v - rows[k][0]:x}"


iat = {imp.address: imp.name.decode() for d in pe.DIRECTORY_ENTRY_IMPORT for imp in d.imports
       if imp.name and imp.name.decode() in WANT}
for addr, name in sorted(iat.items(), key=lambda x: x[1]):
    pat = b"\xff\x25" + struct.pack("<I", addr)  # jmp dword ptr [iat]
    thunks = []
    for lo, hi in text:
        i = img.find(pat, lo, hi)
        while i >= 0:
            thunks.append(base + i)
            i = img.find(pat, i + 1, hi)
    calls = []
    for t in thunks:
        for lo, hi in text:
            for i in range(lo, hi - 5):
                if img[i] == 0xE8 and base + i + 5 + struct.unpack_from("<i", img, i + 1)[0] == t:
                    calls.append(base + i)
    print(f"== {name}: thunk(s) {', '.join(hex(t) for t in thunks) or 'none'}; {len(calls)} call(s) to them")
    for c in calls:
        print(f"   0x{c:08x}  {owner(c)}")
