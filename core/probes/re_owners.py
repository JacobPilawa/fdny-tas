"""RE helper (demos spike): name the research.db function containing each address. python -I re_owners.py <hexaddr> ..."""
import bisect, sqlite3, sys
db = sqlite3.connect(r"C:\fdny-decomp\research\research.db")
rows = sorted((int(a, 16), n, s or "") for a, n, s in db.execute(
    "select address, name, summary from symbols where kind='function' and address like '0x%'"))
keys = [r[0] for r in rows]
for x in sys.argv[1:]:
    v = int(x, 16)
    k = bisect.bisect_right(keys, v) - 1
    a, n, s = rows[k]
    print(f"{x}: {n}+0x{v - a:x}  -- {s[:150]}")
