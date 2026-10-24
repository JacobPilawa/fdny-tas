"""Spike helper (demos / ghosts): diff two zz_demoprobe_ents_*.txt dumps per frame: entity
counts by kind, and which entities (matched by list position within each kind) differ.

    python -I core/probes/demo_entdiff.py <ents_rec.txt> <ents_playN.txt>
"""
import collections
import struct
import sys


def load(path):
    frames = collections.defaultdict(list)
    with open(path, encoding="ascii") as f:
        for line in f:
            p = line.split()
            frames[int(p[0])].append((int(p[3]), tuple(p[5:8]), p[9], p[11]))
    return frames


def fl(h):
    return round(struct.unpack("<f", struct.pack("<I", int(h, 16)))[0], 2)


a, b = load(sys.argv[1]), load(sys.argv[2])
for fr in sorted(set(a) & set(b)):
    ka = collections.Counter(k for k, *_ in a[fr])
    kb = collections.Counter(k for k, *_ in b[fr])
    print(f"== frame {fr}: {len(a[fr])} vs {len(b[fr])} entities; kinds rec {dict(ka)} play {dict(kb)}")
    for kind in sorted(set(ka) | set(kb)):
        ea = [e for e in a[fr] if e[0] == kind]
        eb = [e for e in b[fr] if e[0] == kind]
        diff = [(i, x, y) for i, (x, y) in enumerate(zip(ea, eb)) if x != y]
        if diff or len(ea) != len(eb):
            print(f"  kind {kind}: {len(ea)} vs {len(eb)}, {len(diff)} differ")
            for i, x, y in diff[:4]:
                print(f"    #{i} rec pos {[fl(v) for v in x[1]]} s120 {x[2]} s70 {x[3]} | play pos {[fl(v) for v in y[1]]} "
                      f"s120 {y[2]} s70 {y[3]}")
