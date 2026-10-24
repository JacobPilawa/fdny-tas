"""RE helper (demos spike): timeGetTime / GetTickCount / QPC sites, ParticleSystem::Emit callers, GameClock field readers."""
import bisect, sqlite3, struct
import pefile
pe = pefile.PE(r"C:\fdny-decomp\FDNY SP 1.1\FDNYFirefighter.exe")
img = pe.get_memory_mapped_image(); base = pe.OPTIONAL_HEADER.ImageBase
text = [(s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize) for s in pe.sections if s.Characteristics & 0x20000000]
db = sqlite3.connect(r"C:\fdny-decomp\research\research.db")
rows = sorted((int(a, 16), n) for a, n in db.execute("select address,name from symbols where kind='function' and address like '0x%'"))
keys = [r[0] for r in rows]
def owner(v):
    k = bisect.bisect_right(keys, v) - 1; a, n = rows[k]; return f"{n}+0x{v-a:x}"
iat = {}
for d in pe.DIRECTORY_ENTRY_IMPORT:
    for i in d.imports:
        if i.name: iat[i.name.decode()] = i.address
def refs(va):
    pat = struct.pack("<I", va)
    for lo, hi in text:
        i = img.find(pat, lo, hi)
        while i >= 0:
            yield base + i; i = img.find(pat, i + 1, hi)
for name in ("timeGetTime", "GetTickCount", "QueryPerformanceCounter"):
    print(f"== {name}")
    for r in refs(iat[name]):
        print(f"  0x{r:08x} {owner(r)}")
print("== calls to ParticleSystem::Emit 0x00480090")
for lo, hi in text:
    for i in range(lo, hi - 5):
        if img[i] == 0xE8 and base + i + 5 + struct.unpack_from("<i", img, i + 1)[0] == 0x00480090:
            print(f"  0x{base+i:08x} {owner(base+i)}")
print("== code reading g_gameClock real subtimer / fps (0x00502AD8.., 0x00502B08)")
for va in (0x00502AD8, 0x00502ADC, 0x00502AE0, 0x00502AE4, 0x00502B08, 0x00502B0C):
    for r in refs(va):
        print(f"  [{va:08x}] 0x{r:08x} {owner(r)}")
