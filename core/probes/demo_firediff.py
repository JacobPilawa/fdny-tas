"""Spike helper (demos / ghosts): compare the fire lines (heat / spread, v9-v10) of two
zz_demoprobe_script_*.txt timelines slot by slot, without the absolute clock column, and
print the first slot where they differ with both sides.

    python -I core/probes/demo_firediff.py <script_a.txt> <script_b.txt> [max_slot]
"""
import collections
import sys


def load(path, max_slot):
    by = collections.defaultdict(list)
    for line in open(path, encoding="latin-1"):
        p = line.rstrip("\r\n").split(" ", 3)
        if len(p) == 4 and p[2] == "-" and (p[3].startswith("heat") or p[3].startswith("spread")):
            s = int(p[0])
            if s <= max_slot:
                # drop the absolute times inside the line (timer-now stays, it is relative)
                t = p[3].split(' ')
                t = ['due' if (i > 0 and t[i - 1] == 'timer-now' and w.lstrip('-').isdigit() and int(w) < -10000) else w
                     for i, w in enumerate(t)]  # a never-set timer (0) prints as minus the absolute clock
                by[s].append(' '.join(t))
    return by


mx = int(sys.argv[3]) if len(sys.argv) > 3 else 20
a, b = load(sys.argv[1], mx), load(sys.argv[2], mx)
for s in range(mx + 1):
    x, y = a.get(s, []), b.get(s, [])
    if x == y:
        print(f"slot {s}: {len(x)} fire lines identical")
        continue
    print(f"slot {s}: DIFFER ({len(x)} vs {len(y)} lines)")
    for i in range(max(len(x), len(y))):
        u = x[i] if i < len(x) else "-"
        v = y[i] if i < len(y) else "-"
        if u != v:
            print(f"  [{i}] A {u[:160]}\n      B {v[:160]}")
    break
