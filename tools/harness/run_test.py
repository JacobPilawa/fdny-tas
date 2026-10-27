"""Run live tests. VM-only.

  python -I run_test.py tests/game/core_loads.yaml            # Debug core
  python -I run_test.py --core Release tests/game/*.yaml
  python -I run_test.py --core none tests/game/x.yaml         # baseline, core removed
  python -I run_test.py --suite                           # every file in tests/game/
  python -I run_test.py --ondemand                        # tests/game/ondemand/ (reference runs)
"""
import argparse
import glob
import json
import os
import sys
import time

import yaml

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fdh import runner, session  # noqa: E402

GAME_DIR = os.path.join(runner.REPO_DIR, "FDNY SP 1.1")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tests", nargs="*")
    ap.add_argument("--core", default="Debug", choices=["Debug", "Release", "none"])
    ap.add_argument("--suite", action="store_true")
    ap.add_argument("--ondemand", action="store_true")
    ap.add_argument("--tag", default="")
    ap.add_argument("--game-dir", default=GAME_DIR)
    a = ap.parse_args()
    paths = (sorted(glob.glob(os.path.join(runner.REPO_DIR, "tests", "game", "*.yaml"))) if a.suite else
             sorted(glob.glob(os.path.join(runner.REPO_DIR, "tests", "game", "ondemand", "*.yaml"))) if a.ondemand else
             a.tests)
    core = None if a.core == "none" else a.core
    # Tests that can share a running game run together in one session (fdh/session.py); the
    # others launch their own. Within each group the order is the file order.
    plan = []
    for p in paths:
        with open(p, encoding="utf-8") as f:
            plan.append((p, session.key(yaml.safe_load(f), core, a.game_dir)))
    keys = list(dict.fromkeys(k for _, k in plan))
    plan.sort(key=lambda pk: keys.index(pk[1]))
    sessions = {k: session.Session(k) for k in keys if k is not None}
    results = []
    t0 = time.time()
    try:
        for p, k in plan:
            s = sessions.get(k)
            for other in sessions.values():  # one game at a time: leaving a session ends it
                if other is not s and other.g:
                    other.close()
            r = runner.Run(p, a.game_dir, core, a.tag, s).execute()
            if not r["pass"] and str(r.get("crash", "")).startswith("harness: could not focus"):
                # An environment flake (the game never got focus), not a test result: retry once.
                print(f"   retrying {r['test']} (focus failure)")
                r = runner.Run(p, a.game_dir, core, a.tag + "-retry", s).execute()
            results.append(r)
    finally:
        for s in sessions.values():
            s.close()
    launches = sum(1 for r in results for x in r["assertions"] if x["step"] == "core_ready")
    print(f"{len(results)} tests, {launches} game launches, {time.time() - t0:.0f} s")
    print(json.dumps({r["test"]: r["pass"] for r in results}, indent=2))
    return 0 if results and all(r["pass"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
