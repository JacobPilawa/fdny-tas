"""RE helper (demos spike): direct callers of + pointers to functions, named from research.db. python -I re_callers.py <hexaddr> ..."""
import bisect, sqlite3, struct, sys
import pefile
pe = pefile.PE(r"C:\fdny-decomp\FDNY SP 1.1\FDNYFirefighter.exe")
img = pe.get_memory_mapped_image(); base = pe.OPTIONAL_HEADER.ImageBase
text = [(s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize) for s in pe.sections if s.Characteristics & 0x20000000]
db = sqlite3.connect(r"C:\fdny-decomp\research\research.db")
rows = sorted((int(a, 16), n, s or "") for a, n, s in db.execute("select address,name,summary from symbols where kind='function' and address like '0x%'"))
keys = [r[0] for r in rows]
def owner(v):
    k = bisect.bisect_right(keys, v) - 1; a, n, s = rows[k]; return f"{n}+0x{v-a:x}  -- {s[:110]}"
for t in sys.argv[1:]:
    tv = int(t, 16)
    print(f"== callers of 0x{tv:08x} ({owner(tv).split('  --')[0]})")
    for lo, hi in text:
        for i in range(lo, hi - 5):
            if img[i] == 0xE8 and base + i + 5 + struct.unpack_from("<i", img, i + 1)[0] == tv:
                print(f"  0x{base+i:08x} {owner(base+i)}")
    pat = struct.pack("<I", tv)  # vtable / pointer references
    for s in pe.sections:
        lo, hi = s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize
        i = img.find(pat, lo, hi)
        while i >= 0:
            print(f"  ptr at 0x{base+i:08x} ({s.Name.rstrip(bytes(1)).decode()})")
            i = img.find(pat, i + 1, hi)
