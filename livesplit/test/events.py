"""Ground truth for the ASL tests: what the game did, and when.

Runs beside the game and AslHost.exe and logs, with the same clock as AslHost (the
Windows performance counter, seconds) and the game's own clock (ms, one tick per frame):
  - gameState (0 menu, 2 loading, 3 level start, 1 playing) and the current map;
  - the mission result (PlayerRescueEnt+0x3438): 0 -> 1 is the frame the game declares
    the win (core/evidence/autosplitter);
  - pendingMap and the victim counters, for reading the log.
These are the game's own values, not the splitter's: a stage is done when its load
starts (gameState 1 -> 2 with the next stage pending), a level when the result turns 1.

Test input (the same as the autosplitter spike's probe): once a level has been playing
for --open-after seconds, it writes delivered = total, so the game's own
UpdateZonesAndWin opens the exit (or wins an exit-less map). The case then walks the
player into the exit, whose touch runs the map's own exit verb.

python -I events.py <out.log> <seconds> [--open-after S]
"""
import argparse
import struct
import time

import pymem

BASE = 0x400000
CLOCK = 0x502AB8 + 0x44  # game clock elapsed ms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("seconds", type=float)
    ap.add_argument("--open-after", type=float, default=3.0)
    a = ap.parse_args()
    out = open(a.out, "w", encoding="utf-8")
    t_end = time.perf_counter() + a.seconds
    pm = None

    def log(msg, clock=None):
        out.write(f"{time.perf_counter():12.4f} clock={clock} {msg}\n")
        out.flush()

    while pm is None and time.perf_counter() < t_end:
        try:
            pm = pymem.Pymem("FDNYFirefighter.exe")
        except Exception:
            time.sleep(0.2)
    if pm is None:
        log("FAIL no game process")
        return 1
    time.sleep(5)  # let the game create its window first (an early attach once coincided with a startup hang)

    def deep(off, *chain):
        p = pm.read_uint(BASE + off)
        for c in chain[:-1]:
            p = pm.read_uint(p + c)
        return p + chain[-1]

    def i32(off, *chain):
        try:
            return struct.unpack("<i", pm.read_bytes(deep(off, *chain), 4))[0]
        except Exception:
            return None

    def text(off, *chain):
        try:
            return pm.read_bytes(deep(off, *chain), 16).split(b"\0")[0].decode("latin-1")
        except Exception:
            return None

    values = {
        "gameState": lambda: i32(0x102AAC, 0x6F0),
        "map": lambda: text(0x102AAC, 0x688, 0x0, 0xC),
        "pendingMap": lambda: text(0x102AAC, 0x660, 0xC),
        "result": lambda: i32(0x103A78, 0x30, 0x3438),
        "total": lambda: i32(0x103A78, 0x30, 0x3528),
        "delivered": lambda: i32(0x103A78, 0x30, 0x352C),
    }
    log("attached")
    prev, playing_since, opened = {}, None, False
    while time.perf_counter() < t_end:
        try:
            clock = pm.read_uint(CLOCK)
        except Exception:
            log("game exited")
            break
        cur = {k: f() for k, f in values.items()}
        for k, v in cur.items():
            if k in prev and prev[k] != v:
                log(f"GAME {k}: {prev[k]!r} -> {v!r}", clock)
        if cur["gameState"] == 1 and prev.get("gameState") == 3:
            playing_since, opened = time.perf_counter(), False  # each play of a level, retries too
        if cur["gameState"] != 1:
            playing_since = None
        if (playing_since and not opened and (cur["total"] or 0) > 0
                and time.perf_counter() - playing_since > a.open_after):
            try:
                pm.write_int(deep(0x103A78, 0x30, 0x352C), cur["total"])
                log(f"INPUT delivered := total ({cur['total']}) on {cur['map']}", clock)
            except Exception as e:
                log(f"INPUT failed: {e}", clock)
            opened = True
        prev = cur
        time.sleep(0.001)  # ~2 ms per sample: well under a frame, and leaves the CPU to the game
    log("done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
