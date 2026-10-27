"""Writes the Debug override fixtures for the item-2 tests from the real table.

  tests/game/data/addr_override_unknown.txt  real table with exe/Genesis hashes zeroed
  tests/game/data/addr_override_badsig.txt   real table with game.map_name's signature corrupted
"""
import os
import subprocess
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
DATA = os.path.join(REPO, "tests", "game", "data")


def main():
    os.makedirs(DATA, exist_ok=True)
    base = os.path.join(DATA, "addr_override_base.tmp")
    subprocess.check_call([sys.executable, "-I", os.path.join(REPO, "tools", "addrgen.py"), "text", base])
    lines = open(base, encoding="ascii").read().splitlines()
    os.remove(base)

    unknown = []
    for ln in lines:
        if ln.startswith("version "):
            name = ln.split()[1]
            ln = f"version {name} {'0' * 64} {'0' * 64}"
        unknown.append(ln)
    with open(os.path.join(DATA, "addr_override_unknown.txt"), "w", encoding="ascii", newline="\n") as f:
        f.write("# item-2 fixture: no version matches (hashes zeroed)\n" + "\n".join(unknown) + "\n")

    bad = []
    for ln in lines:
        p = ln.split()
        if p[:2] == ["entry", "game.map_name"]:
            sig = p[7]
            p[7] = ("FF" if sig[:2] != "FF" else "00") + sig[2:]  # flip the first signature byte
            ln = " ".join(p)
        bad.append(ln)
    with open(os.path.join(DATA, "addr_override_badsig.txt"), "w", encoding="ascii", newline="\n") as f:
        f.write("# item-2 fixture: game.map_name signature corrupted\n" + "\n".join(bad) + "\n")
    print("wrote fixtures in", DATA)


if __name__ == "__main__":
    main()
