"""Replay an autosplitter's logic over a splitprobe trace (core/probes/splitprobe.py).

The trace logs every change of the values the ASL reads, with timestamps. This rebuilds
the values at each change and runs the logic like LiveSplit does each tick (update, then
start / isLoading / reset / split), for the OLD script (the owner's v1 IL splitter) and
the NEW one (FDNY_ILs.asl v2). It prints when each would start, split and pause.

--menu-start: model a level started from the menu. Then GameSession+0x688 points at the
menu's TLevelInfo, so mapName keeps the old name until the in-game load sets +0x688 to the
pending block at its start (GameSession::LoadLevel; core/evidence/autosplitter). In the
probe runs every level came from the console (`loadmap`), where mapName already reads the
pending block and changes on the touch frame.

--from T: start both scripts at T seconds into the trace (e.g. just before a level loads,
for an IL started there).

python -I asl_replay.py <trace.log> [--menu-start] [--from T]
"""
import re
import sys

LINE = re.compile(r"^\s*([\d.]+) clock=\S+ ASL (\w+): (.*) -> (.*)$")
NEXT = {"oil_rig_a": "oil_rig_b", "oil_rig_b": "oil_rig_c", "oil_rig_c": "oil_rig_d",
        "boat_a": "boat_b", "subway_a": "subway_b"}


def parse(path):
    events = []
    for line in open(path, encoding="utf-8"):
        m = LINE.match(line)
        if m:
            t, key, _, new = m.groups()
            v = eval(new)  # repr of int / str / None, from the probe
            events.append((float(t), key, v))
    return events


def ticks(events, menu_start):
    """(time, old, cur) per change, values as the ASL would read them."""
    cur = {"gameState": None, "mapName": None, "pendingMap": None, "missionResult": None}
    keymap = {"gameState": "gameState", "mapName": "mapName", "pendingMap": "pendingMap", "result": "missionResult"}
    held_map = None
    for t, key, v in events:
        if key not in keymap:
            continue
        old = dict(cur)
        k = keymap[key]
        if menu_start and k == "mapName":
            # mapName follows the pending block only once the load has started
            if cur["gameState"] == 1 and v is not None:
                held_map = v
                continue
        cur[k] = v
        if menu_start and k == "gameState" and v == 2 and held_map is not None:
            cur["mapName"], held_map = held_map, None
        yield t, old, dict(cur)


def low(s):
    return (s or "").lower().strip()


class OldAsl:
    """The owner's v1 IL splitter (map-change splits for stages; per-map end sounds)."""
    name = "OLD"

    def __init__(self):
        self.has_split, self.last_map, self.started = False, "", False

    def tick(self, old, cur):
        out = []
        m = low(cur["mapName"])
        if not self.started and cur["gameState"] == 1:  # its start block (timer not running)
            self.started, self.has_split, self.last_map = True, False, m
            out.append(f"start on {m}")
        if old["gameState"] == 0 and cur["gameState"] == 1:
            self.has_split, self.last_map = False, m
        if cur["gameState"] == 1 and self.has_split and self.last_map and self.last_map != m:
            self.has_split, self.last_map = False, m
        if cur["gameState"] == 1 and not self.has_split:
            for a, b in NEXT.items():
                if a in self.last_map and b in m:
                    self.has_split = True
                    out.append(f"split stage {a}->{b}")
            # the end-sound flags are modelled by the mission result (same frame +1-2)
            if old["missionResult"] == 0 and cur["missionResult"] == 1 and NEXT.get(m) is None:
                self.has_split = True
                out.append(f"split {m} won")
        loading = self.has_split or cur["gameState"] in (2, 3)
        return out, loading


class NewAsl:
    name = "NEW"

    def __init__(self):
        self.level, self.in_level, self.stage_split, self.done = "", False, False, False

    def tick(self, old, cur):
        out = []
        m, pend, opend = low(cur["mapName"]), low(cur["pendingMap"]), low(old["pendingMap"])
        if old["gameState"] == 3 and cur["gameState"] == 1:
            self.level, self.in_level, self.stage_split, self.done = m, True, False, False
            out.append(f"playing {m}")
        if cur["gameState"] == 0:
            self.in_level = False
        nxt = NEXT.get(self.level)
        if nxt and not self.stage_split and pend == nxt and pend != opend:
            self.stage_split = True
            out.append(f"split stage {self.level}->{nxt} (exit touched)")
        elif (nxt and not self.stage_split and self.in_level and old["gameState"] == 1 and cur["gameState"] == 2
              and pend == nxt):
            self.stage_split = True
            out.append(f"split stage {self.level}->{nxt} (load start)")
        if cur["gameState"] == 1 and old["missionResult"] == 0 and cur["missionResult"] == 1:
            self.done = True
            out.append(f"split {self.level} won")
        loading = self.done or cur["gameState"] in (2, 3)
        return out, loading


def main():
    path = sys.argv[1]
    menu = "--menu-start" in sys.argv
    t_from = float(sys.argv[sys.argv.index("--from") + 1]) if "--from" in sys.argv else 0.0
    events = parse(path)
    for asl in (OldAsl(), NewAsl()):
        print(f"--- {asl.name} ({'menu start' if menu else 'console start'}) {path}")
        was_loading = None
        for t, old, cur in ticks(events, menu):
            if t < t_from:
                continue
            out, loading = asl.tick(old, cur)
            for o in out:
                print(f"  {t:9.4f}  {o}   [state {cur['gameState']}, map {cur['mapName']!r}, pending {cur['pendingMap']!r}]")
            if loading != was_loading:
                print(f"  {t:9.4f}  timer {'PAUSED' if loading else 'running'}")
                was_loading = loading


if __name__ == "__main__":
    main()
