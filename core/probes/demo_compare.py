"""Spike helper (demos / ghosts): compare zz_demoprobe trace files field by field.

    python -I core/probes/demo_compare.py <rec.txt> <play.txt> [...]

Per field: frames that differ, first differing frame, max |difference| (floats decoded
from their hex bits), and the difference at the first and last frame.
"""
import struct
import sys

FLOATS = {"dt", "pos_x", "pos_y", "pos_z", "vel_x", "vel_y", "vel_z", "yaw", "pitch", "timer", "time_left"}
HEX = {"ent_hash", "vic_hash", "other_hash"}


def load(path):
    with open(path, encoding="ascii") as f:
        head = f.readline().split("(")[0].split()
        rows = []
        for line in f:
            v = line.split()
            row = {}
            for k, x in zip(head, v):
                row[k] = struct.unpack("<f", struct.pack("<I", int(x, 16)))[0] if k in FLOATS else int(x, 16) if k in HEX else int(x)
            rows.append(row)
    return head, rows


def main(rec, *plays):
    head, a = load(rec)
    for p in plays:
        _, b = load(p)
        n = min(len(a), len(b))
        print(f"== {p}: {n} frames (rec {len(a)})")
        for k in head[1:]:
            d = [(i, b[i][k] - a[i][k]) for i in range(n) if a[i][k] != b[i][k]]
            if not d:
                print(f"  {k:12s} identical")
                continue
            mx = max(abs(x) for _, x in d)
            print(f"  {k:12s} differs on {len(d):5d} frames, first {d[0][0]:5d}, max |d| {mx:.6g}, "
                  f"d[first] {d[0][1]:.6g}, d[last frame] {b[n - 1][k] - a[n - 1][k]:.6g}")


if __name__ == "__main__":
    main(*sys.argv[1:])
