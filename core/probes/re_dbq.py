"""RE helper (demos spike): research.db symbols by name/summary LIKE pattern. python -I re_dbq.py "Pattern%" ..."""
import sqlite3, sys
c = sqlite3.connect(r"C:\fdny-decomp\research\research.db")
for pat in sys.argv[1:]:
    print(f"=== {pat}")
    for r in c.execute("select address, name, summary from symbols where name like ? or summary like ? order by address",
                       (pat, pat)):
        print(f"{r[0]}  {r[1]}  -- {(r[2] or '')[:260]}")
