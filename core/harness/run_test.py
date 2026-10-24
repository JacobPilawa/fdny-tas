"""Run live tests. VM-only.

  python -I run_test.py tests/core_loads.yaml            # Debug core
  python -I run_test.py --core Release tests/*.yaml
  python -I run_test.py --core none tests/x.yaml         # baseline, core removed
  python -I run_test.py --suite                           # every file in core/tests/
"""
import argparse
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fdh import runner  # noqa: E402

GAME_DIR = os.path.normpath(os.path.join(runner.CORE_DIR, "..", "FDNY SP 1.1"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tests", nargs="*")
    ap.add_argument("--core", default="Debug", choices=["Debug", "Release", "none"])
    ap.add_argument("--suite", action="store_true")
    ap.add_argument("--tag", default="")
    ap.add_argument("--game-dir", default=GAME_DIR)
    a = ap.parse_args()
    paths = sorted(glob.glob(os.path.join(runner.CORE_DIR, "tests", "*.yaml"))) if a.suite else a.tests
    core = None if a.core == "none" else a.core
    results = []
    for p in paths:
        r = runner.Run(p, a.game_dir, core, a.tag).execute()
        if not r["pass"] and str(r.get("crash", "")).startswith("harness: could not focus"):
            # An environment flake (the game never got focus), not a test result: retry once.
            print(f"   retrying {r['test']} (focus failure)")
            r = runner.Run(p, a.game_dir, core, a.tag + "-retry").execute()
        results.append(r)
    print(json.dumps({r["test"]: r["pass"] for r in results}, indent=2))
    return 0 if results and all(r["pass"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
