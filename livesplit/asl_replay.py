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
# each level's end-sound slot (trace key), read only on its own map, as both scripts do
END_SOUND = [("training", "snd_training"), ("bank", "snd_bank"), ("bar_r", "snd_bar"),
             ("gas_r", "snd_gas"), ("mbase", "snd_mbase"), ("boat_b", "snd_boat"),
             ("subway_b", "snd_subway"), ("oil_rig_d", "snd_oilrigD")]


def parse(path):
    events = []
    for line in open(path, encoding="utf-8"):
        m = LINE.match(line)
        if m:
            t, key, _, new = m.groups()
            v = eval(new)  # repr of int / str / None, from the probe
            events.append((float(t), key, v))
    return events


TICK = 0.015  # LiveSplit's default ASL refresh: the script runs every 15 ms (66.7 Hz)


def ticks(events, menu_start):
    """(time, old, cur) per change and per LiveSplit tick between changes (nothing changed:
    old == cur), values as the ASL would read them."""
    cur = {"gameState": None, "mapName": None, "pendingMap": None, "missionResult": None}
    keymap = {"gameState": "gameState", "mapName": "mapName", "pendingMap": "pendingMap", "result": "missionResult"}
    for _, key in END_SOUND:
        cur[key], keymap[key] = None, key
    held_map = None
    last_t = None
    for t, key, v in events:
        if key not in keymap:
            continue
        while last_t is not None and last_t + TICK < t:
            last_t += TICK
            yield last_t, dict(cur), dict(cur)
        last_t = t
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


def end_sound(m, old, cur):
    """The map's end sound started (0 -> nonzero) this tick."""
    return any(name in m and old[key] == 0 and cur[key] not in (0, None) for name, key in END_SOUND)


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
            if end_sound(m, old, cur):
                self.has_split = True
                out.append(f"split {m} won (end sound)")
        loading = self.has_split or cur["gameState"] in (2, 3)
        return out, loading


class NewAsl:
    name = "NEW"

    def __init__(self):
        self.level, self.in_level, self.stage_split, self.done = "", False, False, False
        self.t, self.win_t = 0.0, None

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
        if old["missionResult"] == 0 and cur["missionResult"] == 1:
            self.win_t = self.t  # the game's win flag, for the end sound's lag
        if cur["gameState"] == 1 and self.in_level and not self.done and end_sound(self.level, old, cur):
            self.done = True
            lag = f", {1000 * (self.t - self.win_t):.0f} ms after the win flag" if self.win_t is not None else ""
            out.append(f"split {self.level} won (end sound{lag})")
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
            asl.t = t
            out, loading = asl.tick(old, cur)
            for o in out:
                print(f"  {t:9.4f}  {o}   [state {cur['gameState']}, map {cur['mapName']!r}, pending {cur['pendingMap']!r}]")
            if loading != was_loading:
                print(f"  {t:9.4f}  timer {'PAUSED' if loading else 'running'}")
                was_loading = loading


if __name__ == "__main__":
    main()
