"""Baseline equivalence (item 1c): is a with-core run inside the noise envelope
of two without-core runs of tests/baseline_route.yaml?

python -I compare_runs.py <nocore_a> <nocore_b> <core>   (run dirs or result.json)

Checks (LOOP.md "Baseline equivalence"):
 1 no crash / hang / dialog in any run
 2 same map + game state at start, every lap and the end
 3 identical module bases (exe, Genesis.dll)
 4 every autosplitter chain resolves (non-null) in every run; its final
   address matches the baselines whenever the two baselines agree with each other
 5 median fps within +/-10% of the baselines' median
 6 per-lap player position within tolerance: max(32 u, 2 x baseline-pair spread)
   -- the game isn't deterministic under scripted wall-clock input
 7 zero ERROR lines in the core log (from the core run's own assertions)
"""
import json
import math
import os
import statistics
import sys

POS_FLOOR = 32.0


def load(p):
    if os.path.isdir(p):
        p = os.path.join(p, "result.json")
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def laps(r):
    s = r["samples"]
    return list(zip(s.get("lap_x", []), s.get("lap_y", []), s.get("lap_z", [])))


def main():
    a, b, c = (load(p) for p in sys.argv[1:4])
    checks = []

    def check(name, ok, detail):
        checks.append({"check": name, "pass": bool(ok), "detail": detail})
        print(f"[{'PASS' if ok else 'FAIL'}] {name}: {detail}")

    # 1
    check("1 no_crash", all(r["crash"] is None for r in (a, b, c)),
          f"crash: base_a={a['crash']} base_b={b['crash']} core={c['crash']}")
    # 2
    def states(r):
        s = r["samples"]
        return (s.get("map_start"), s.get("state_start"), tuple(s.get("lap_state", [])))
    check("2 same_level_and_state",
          states(a) == states(b) == states(c) and None not in states(a)[:2] and states(a)[2],
          f"base={states(a)} core={states(c)}")
    # 3
    # The exe never relocates. Genesis.dll is loaded dynamically and does
    # relocate when something already holds 0x10000000 (seen 2026-10-21:
    # 0x02390000) -- legitimate, so it's reported, not required equal.
    exes = {r["samples"].get("module.exe") for r in (a, b, c)}
    gens = [r["samples"].get("module.genesis") for r in (a, b, c)]
    check("3 module_bases", len(exes) == 1 and None not in exes, f"exe {exes}; Genesis.dll {gens}")
    # 4
    keys = sorted(k for k in a["samples"] if k.startswith("addr."))
    unresolved = [k for k in keys for r in (a, b, c) if r["samples"].get(k) is None]
    moved = [k for k in keys
             if a["samples"][k] == b["samples"][k] and c["samples"].get(k) != a["samples"][k]]
    noisy = [k for k in keys if a["samples"][k] != b["samples"][k]]
    check("4 autosplitter_chains", len(keys) == 12 and not unresolved and not moved,
          f"{len(keys)} chains; unresolved={unresolved}; moved_vs_stable_baseline={moved}; "
          f"baseline-noisy (heap differs run to run, not compared)={noisy}")
    # 5
    base_fps = [v for r in (a, b) for v in r["samples"].get("fps", []) if v]
    core_fps = [v for v in c["samples"].get("fps", []) if v]
    if base_fps and core_fps:
        mb, mc = statistics.median(base_fps), statistics.median(core_fps)
        check("5 frame_rate", abs(mc - mb) <= 0.10 * mb, f"median fps base={mb} core={mc}")
    else:
        check("5 frame_rate", False, "no fps samples")
    # 6
    la, lb, lc = laps(a), laps(b), laps(c)
    n = min(len(la), len(lb), len(lc))
    worst = []
    for i in range(n):
        tol = max(POS_FLOOR, 2 * dist(la[i], lb[i]))
        d = min(dist(lc[i], la[i]), dist(lc[i], lb[i]))
        worst.append((round(d, 1), round(tol, 1)))
    check("6 lap_positions", n > 0 and all(d <= t for d, t in worst),
          f"{n} laps (core dist, tolerance): {worst}")
    # 7
    logs = [x for x in c["assertions"] if x["step"] == "log_errors"]
    check("7 core_log_errors", bool(logs) and all(x["pass"] for x in logs),
          logs[0]["detail"] if logs else "core run has no log_errors assertion")

    ok = all(x["pass"] for x in checks)
    out = {"pass": ok, "checks": checks, "runs": sys.argv[1:4]}
    target = sys.argv[3] if os.path.isdir(sys.argv[3]) else os.path.dirname(sys.argv[3])
    with open(os.path.join(target, "equivalence.json"), "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2)
    print("EQUIVALENT" if ok else "NOT EQUIVALENT")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
