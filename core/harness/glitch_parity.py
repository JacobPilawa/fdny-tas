"""VM-only: hud_glitch parity check (backlog item 11b/11c).

Compares, session by session, the collision log FDNYCore wrote live
("glitch| " lines in FDNYCore.log) with the log the ORIGINAL
tools/collision_overlay classes produce when the same raw stream
(FDNYCore.glitchrec) is replayed through them. Timestamps are normalised.

  python -I glitch_parity.py <FDNYCore.glitchrec> <FDNYCore.log> [--predict MAPSDIR]
Prints a summary; exit 0 if identical.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glitch_replay  # noqa: E402

TS_HEADER = re.compile(r"^(# FDNY collision log  ).*$")
TS_NOTE = re.compile(r"^# \d\d:\d\d:\d\d  ")


def norm(line):
    line = TS_HEADER.sub(r"\1TIMESTAMP", line)
    line = TS_NOTE.sub("# HH:MM:SS  ", line)
    return line.replace("# HH:MM:SS  ", "# HH:MM:SS  ")


def core_sessions(log_path):
    out = []
    with open(log_path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            i = raw.find("] glitch| ")
            if i < 0:
                continue
            line = raw[i + len("] glitch| "):].rstrip("\r\n")
            if line.startswith("# FDNY collision log"):
                out.append([])
            if out:
                out[-1].append(norm(line))
    return out


def compare(rec_path, log_path, maps=None):
    ref = [[norm(l) for l in text.splitlines()] for text, _ in glitch_replay.replay(rec_path, maps)]
    core = core_sessions(log_path)
    report = {"sessions_ref": len(ref), "sessions_core": len(core), "lines": 0, "mismatches": [],
              "kinds": {}}
    if len(ref) != len(core):
        report["mismatches"].append(f"session count: reference {len(ref)} vs core {len(core)}")
    for si, (a, b) in enumerate(zip(ref, core)):
        report["lines"] += len(a)
        for line in a:
            if line.startswith("#"):
                k = "note" if TS_NOTE.match(line) or line.startswith("# HH") else "header"
            else:
                k = line.split()[3] if len(line.split()) > 3 else "?"
            report["kinds"][k] = report["kinds"].get(k, 0) + 1
        if a != b:
            for li in range(max(len(a), len(b))):
                x = a[li] if li < len(a) else "<missing>"
                y = b[li] if li < len(b) else "<missing>"
                if x != y:
                    report["mismatches"].append(f"session {si} line {li}:\n  ref : {x}\n  core: {y}")
                    if len(report["mismatches"]) > 8:
                        break
    return report


def compare_boxes(rec_path, box_path, maps=None):
    """11d: the core's per-frame box text (FDNYCore.glitchbox, Debug with
    FDNYCORE_TEST_GLITCHBOX=1) vs the original Overlay.build() on the same
    stream. Returns {frames, lines, mismatches}."""
    ref = []
    glitch_replay.replay(rec_path, maps, ref)
    with open(box_path, encoding="utf-8", errors="replace") as f:
        core = [l.rstrip("\r\n") for l in f]
    r = {"frames": sum(1 for l in ref if l.startswith("== ")), "lines": len(ref), "mismatches": []}
    if len(ref) != len(core):
        r["mismatches"].append(f"line count: reference {len(ref)} vs core {len(core)}")
    for i, (a, b) in enumerate(zip(ref, core)):
        if a != b:
            r["mismatches"].append(f"line {i}:\n  ref : {a}\n  core: {b}")
            if len(r["mismatches"]) > 8:
                break
    return r


if __name__ == "__main__":
    maps = sys.argv[sys.argv.index("--predict") + 1] if "--predict" in sys.argv else None
    r = compare(sys.argv[1], sys.argv[2], maps)
    print(f"sessions ref={r['sessions_ref']} core={r['sessions_core']} lines={r['lines']} kinds={r['kinds']}")
    for m in r["mismatches"]:
        print("MISMATCH", m)
    print("IDENTICAL" if not r["mismatches"] else "DIFFERENT")
    sys.exit(0 if not r["mismatches"] else 1)
