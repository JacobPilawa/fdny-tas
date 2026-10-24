"""Spike helper (demos / ghosts): diff two zz_demoprobe_script_*.txt timelines (v8) with the
game clock made relative to the run's LoadLevel; prints the first differing line with
context, and per-slot line counts around it.

    python -I core/probes/demo_scriptdiff.py <script_rec.txt> <script_playN.txt>
"""
import sys


def load(path):
    rows = []
    t0 = None
    for line in open(path, encoding="latin-1"):
        p = line.rstrip("\r\n").split(" ", 3)
        if len(p) < 4:
            continue
        slot, clock, rest = int(p[0]), int(p[1]), p[2] + " " + p[3]
        t0 = clock if t0 is None else t0
        rows.append((slot, clock - t0, rest))
    return rows


a, b = load(sys.argv[1]), load(sys.argv[2])
print(f"{len(a)} vs {len(b)} lines")
for i, (x, y) in enumerate(zip(a, b)):
    if x != y:
        print(f"first difference at line {i}:")
        for j in range(max(0, i - 6), min(len(a), len(b), i + 12)):
            mark = "  " if a[j] == b[j] else "!!"
            print(f" {mark} rec  {a[j][0]:4d} {a[j][1]:6d} {a[j][2][:150]}")
            if a[j] != b[j]:
                print(f" {mark} play {b[j][0]:4d} {b[j][1]:6d} {b[j][2][:150]}")
        break
else:
    print("identical (relative clock)" if len(a) == len(b) else "one is a prefix of the other")
