"""Known-good test for the FDNY splitters: plays scripted runs in the real game and checks
what LiveSplit's own ASL engine does with the .asl file.

For each case in cases/ (case_*.yaml):
  1. starts events.py (the game's own values: ground truth) and AslHost.exe (the .asl,
     run by LiveSplit's engine without the window);
  2. plays the case with the FDNYCore test harness (core/harness): launches the game,
     menus, walks into exits, wins;
  3. checks the timer against the game (check.py) and writes the result next to the
     harness run (core/runs/<stamp>-<case>/: events.log, livesplit.log, asl_checks.json).

python -I run_asl_tests.py [case names or paths ...] [--asl-dir DIR] [--asl FILE]

Needs: the VM setup (the game, Python with pymem, the Debug FDNYCore build) and
AslHost.exe (build_host.ps1). Each case's `asl:` names the splitter file in --asl-dir
(default: this folder's parent, `livesplit/`); --asl runs every case with one file
instead (e.g. an older splitter, to see what the cases catch).
"""
import argparse
import glob
import json
import os
import subprocess
import sys
import time

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "core", "harness"))
sys.path.insert(0, HERE)  # python -I leaves the script's folder off the path
from fdh import runner  # noqa: E402
import check  # noqa: E402

GAME_DIR = os.path.join(REPO, "FDNY SP 1.1")
HOST = os.path.join(REPO, "environment", "local", "tools", "livesplit", "AslHost.exe")


def run_case(path, asl_dir, asl_override=None):
    with open(path, encoding="utf-8") as f:
        case = yaml.safe_load(f)
    asl = os.path.abspath(asl_override) if asl_override else os.path.join(asl_dir, case["asl"])
    seconds = float(case.get("duration_s", 300))
    run = runner.Run(path, GAME_DIR, "Debug")
    ev_log = os.path.join(run.out, "events.log")
    ls_log = os.path.join(run.out, "livesplit.log")
    procs = [
        subprocess.Popen([sys.executable, "-I", os.path.join(HERE, "events.py"), ev_log, str(seconds)]),
        subprocess.Popen([HOST, asl, ls_log, str(seconds)]),
    ]
    try:
        result = run.execute()
    finally:
        time.sleep(0.5)
        for p in procs:
            if p.poll() is None:
                p.kill()
    print(f"-- {case['name']}: {asl} against the game")
    checks = check.check(ev_log, ls_log, bool(case.get("reset", True)), case.get("splits"))
    for c in checks:
        print(f"  [{'PASS' if c['pass'] else 'FAIL'}] {c['check']}: {c['detail']}")
    with open(os.path.join(run.out, "asl_checks.json"), "w", encoding="utf-8") as f:
        json.dump({"case": case["name"], "asl": asl, "harness_pass": result["pass"],
                   "checks": checks}, f, indent=2)
    ok = result["pass"] and all(c["pass"] for c in checks)
    print(f"== {'PASS' if ok else 'FAIL'} {case['name']} ({run.out})")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cases", nargs="*")
    ap.add_argument("--asl-dir", default=os.path.dirname(HERE))
    ap.add_argument("--asl")
    a = ap.parse_args()
    if not os.path.exists(HOST):
        print(f"no {HOST}: run build_host.ps1 first")
        return 2
    paths = []
    for c in a.cases or ["case_*"]:
        if os.path.exists(c):
            paths.append(c)
        else:
            paths += sorted(glob.glob(os.path.join(HERE, "cases", c if c.endswith(".yaml") else c + ".yaml")))
    results = {}
    for p in paths:
        results[os.path.basename(p)[:-5]] = run_case(p, a.asl_dir, a.asl)
    print(json.dumps(results, indent=2))
    return 0 if results and all(results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
