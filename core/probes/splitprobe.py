"""Autosplitter probe (spike, not part of the suite; owner request 2026-10-23).

Runs beside tests/probes/probe_split_*.yaml. Reads memory the way LiveSplit reads an ASL
state block (DeepPointer: module base + offset, then dereference each offset but the
last), as fast as it can, and logs every change with a timestamp:
  - the current ASL values: gameState, mapName, the 8 level-end sound flags, victim counts;
  - every dword of GameSession+0x600..+0x700 (the level-change fields: pending flag
    +0x68C, TLevelInfo +0x688, +0x6EC, gameState +0x6F0);
  - the game clock's elapsed ms (one tick per GameSession::RunFrame) for frame order.

Question: on a staged map, what changes on the frame the player touches the exit
(`finalexitloadmap <next>` -> GameSession::RequestLevelChange), and how do gameState and
mapName move through the in-game load?

Candidate signals added after the Ghidra pass:
  - pendingMap: GameSession+0x660, the String RequestLevelChange stores the next map in
    (set on the exit-touch frame by Verb_FinalExitLoadMap, kept until the next request);
  - result: PlayerRescueEnt+0x3438, the mission result (Verb_FinalExit sets 1 = win).

Probe-only test input (like the fire-juggling probe's safe-spot write): with `force`, once
a level has run 8 s, it writes delivered (+0x352C) = total (+0x3528), so the game's own
UpdateZonesAndWin opens the exit and the test's touch runs the real exit verb.

python -I splitprobe.py <out.log> <seconds> [force]
"""
import struct
import sys
import time

import pymem

BASE = 0x400000
out = open(sys.argv[1], "w", encoding="utf-8")
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 600
t0 = time.perf_counter()


def log(msg):
    line = f"{time.perf_counter() - t0:9.4f} {msg}"
    out.write(line + "\n")
    out.flush()


pm = None
for _ in range(600):
    try:
        pm = pymem.Pymem("FDNYFirefighter.exe")
        break
    except Exception:
        time.sleep(1)
if pm is None:
    log("FAIL no game process")
    sys.exit(1)


def deep(off, *chain):
    """LiveSplit DeepPointer address: [base+off] then +c (deref) for all but the last."""
    a = pm.read_uint(BASE + off)
    for c in chain[:-1]:
        a = pm.read_uint(a + c)
    return a + chain[-1]


def read_i32(off, *chain):
    try:
        return struct.unpack("<i", pm.read_bytes(deep(off, *chain), 4))[0]
    except Exception:
        return None


def read_str(off, *chain, n=10):
    try:
        b = pm.read_bytes(deep(off, *chain), n)
        return b.split(b"\0")[0].decode("latin-1")
    except Exception:
        return None


ASL = {
    "gameState": lambda: read_i32(0x102AAC, 0x6F0),
    "mapName": lambda: read_str(0x102AAC, 0x688, 0x0, 0xC),
    "total": lambda: read_i32(0x103A78, 0x30, 0x3528),
    "rescued": lambda: read_i32(0x103A78, 0x30, 0x352C),
    "pendingMap": lambda: read_str(0x102AAC, 0x660, 0xC, n=16),
    "result": lambda: read_i32(0x103A78, 0x30, 0x3438),
    "snd_bank": lambda: read_i32(0x103A78, 0x8, 0x0, 0x158, 0x0),
    "snd_bar": lambda: read_i32(0x103A78, 0x8, 0x0, 0xFC, 0x0),
    "snd_gas": lambda: read_i32(0x103A78, 0x8, 0x0, 0xF0, 0x0),
    "snd_mbase": lambda: read_i32(0x103A78, 0x8, 0x0, 0xC8, 0x0),
    "snd_training": lambda: read_i32(0x103A78, 0x8, 0x0, 0x104, 0x0),
    "snd_boat": lambda: read_i32(0x103A78, 0x8, 0x0, 0xBC, 0x0),
    "snd_oilrigD": lambda: read_i32(0x103A78, 0x8, 0x0, 0xC4, 0x0),
    "snd_subway": lambda: read_i32(0x103A78, 0x8, 0x0, 0x144, 0x0),
}
CLOCK = 0x502AB8 + 0x44

force = len(sys.argv) > 3 and sys.argv[3] == "force"
forced_map = None
level_since = None
prev = {}
prev_sess = None
prev_clock = None
log("attached")
samples = 0
while time.perf_counter() - t0 < seconds:
    samples += 1
    try:
        clock = pm.read_uint(CLOCK)
    except Exception:
        clock = None
    vals = {k: f() for k, f in ASL.items()}
    for k, v in vals.items():
        if prev.get(k, "<unset>") != v:
            log(f"clock={clock} ASL {k}: {prev.get(k)!r} -> {v!r}")
    prev = vals
    # probe-only input: open the exit the game's way (victim counters -> UpdateZonesAndWin)
    if vals["gameState"] == 1 and vals["mapName"]:
        if level_since is None or level_since[0] != vals["mapName"]:
            level_since = (vals["mapName"], time.perf_counter())
        elif (force and forced_map != vals["mapName"] and time.perf_counter() - level_since[1] > 8
              and (vals["total"] or 0) > 0):
            try:
                pm.write_int(deep(0x103A78, 0x30, 0x352C), vals["total"])
                forced_map = vals["mapName"]
                log(f"clock={clock} PROBE INPUT delivered := total ({vals['total']}) on {vals['mapName']}")
            except Exception as e:
                log(f"PROBE INPUT failed: {e}")
    else:
        level_since = None
    try:
        sess = pm.read_uint(BASE + 0x102AAC)
        blob = pm.read_bytes(sess + 0x600, 0x100)
    except Exception:
        sess, blob = None, None
    if blob is not None and prev_sess is not None and len(prev_sess) == len(blob):
        for i in range(0, 0x100, 4):
            a, b = prev_sess[i:i + 4], blob[i:i + 4]
            if a != b:
                log(f"clock={clock} session+0x{0x600 + i:03X}: {struct.unpack('<I', a)[0]:#010x} -> "
                    f"{struct.unpack('<I', b)[0]:#010x}")
    prev_sess = blob
    if clock != prev_clock and prev_clock is not None and clock is not None and clock < prev_clock:
        log(f"clock went back {prev_clock} -> {clock}")
    prev_clock = clock
log(f"done, {samples} samples")
