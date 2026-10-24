"""Checks a splitter run: what LiveSplit did (AslHost log) against what the game did
(events log). Both logs share one clock (performance counter seconds) and carry the
game's frame clock (ms).

What the game did -> what the timer must do:
  - gameplay starts (gameState 3 -> 1), first time -> the timer starts, at that tick;
  - a stage ends: its in-game load starts (gameState 1 -> 2) with the next stage pending
    -> one split, on the exit touch, which is before the load starts;
    v2.2: the touch is the level-change request byte going 0 -> 1, and the split must be
    on it (within TOUCH_LAG) and before the load starts, also when the next stage's name
    was already pending;
  - a level is won: the mission result goes 0 -> 1 -> one split, 0 to 3 frames later
    (the end sound follows the win flag by 0-2 frames);
  - the menu (gameState -> 0) -> IL: a reset; rescue%: no reset;
  - Game Time at each split = the time spent playing so far: from each 3 -> 1 to that
    level's or stage's end (its exit touch, its load start, its win, or the menu).
    Loads, results screens and menus are not counted.

python -I check.py <events.log> <host.log> [--reset yes|no] [--splits N]
"""
import argparse
import json
import re
import sys

NEXT = {"oil_rig_a": "oil_rig_b", "oil_rig_b": "oil_rig_c", "oil_rig_c": "oil_rig_d",
        "boat_a": "boat_b", "subway_a": "subway_b"}
LINE = re.compile(r"^\s*([\d.]+) clock=(\S+) (.*)$")
STAGE_AFTER_LOAD = 0.05  # s: or, the fallback (a retried stage), on the tick that sees the load start
TOUCH_LAG = 0.05         # s: a stage split must follow the exit touch (request byte 0 -> 1) within this
WIN_FRAMES_MS = 70       # game-clock ms: up to 3 frames at ~43+ fps after the win flag
START_LAG = 0.1          # s: the timer start must follow the first gameplay frame within this
RESET_LAG = 0.5          # s: a reset clears the timer and changes no time; seen 0.16 s
GAME_TIME_TOL = 0.06     # s, plus 0.02 per play interval (pause edges land on 15 ms ticks)


def parse(path):
    out = []
    for line in open(path, encoding="utf-8"):
        m = LINE.match(line)
        if m:
            clock = None if m.group(2) == "None" else int(m.group(2))
            out.append((float(m.group(1)), clock, m.group(3)))
    return out


def lo(s):
    return (s or "").lower()


def game_truth(events):
    """Expected timer events and the play intervals, from the game's own values."""
    gs, mp, pend, playing = None, None, None, None  # playing: the map at the last 3 -> 1
    expected, intervals, play_start, menu = [], [], None, []
    touch = None  # the last level-change request (byte 0 -> 1), until its load starts
    for t, clock, msg in events:
        m = re.match(r"GAME (\w+): (.*) -> (.*)$", msg)
        if not m:
            continue
        key, old, new = m.group(1), eval(m.group(2)), eval(m.group(3))  # reprs from events.py
        if key == "gameState":
            if old == 3 and new == 1:
                play_start, playing = t, lo(mp)
                expected.append({"kind": "play", "t": t, "clock": clock, "map": playing})
            elif old == 1:
                if play_start is not None:
                    intervals.append((play_start, t))
                    play_start = None
                # the current map's name can already have changed (a stage loaded by an
                # earlier exit: it changes on the touch frame), so use the map played
                if new == 2 and NEXT.get(playing) == lo(pend):
                    expected.append({"kind": "stage", "t": t, "clock": clock, "map": playing, "next": lo(pend),
                                     "touch": touch})
                touch = None
            if new == 0:
                menu.append(t)
            gs = new
        elif key == "map":
            mp = new
        elif key == "pendingMap":
            pend = new
        elif key == "levelChange":
            if not old and new:
                touch = t
                # v2.2: the timer pauses from the touch (the game clock is frozen until the
                # load starts; one stall measured 2.5 s): the play interval ends here
                if play_start is not None and gs == 1:
                    intervals.append((play_start, t))
                    play_start = None
        elif key == "result":
            if old == 0 and new == 1 and gs == 1:
                if play_start is not None:
                    intervals.append((play_start, t))
                    play_start = None
                expected.append({"kind": "win", "t": t, "clock": clock, "map": playing})
    if play_start is not None:
        intervals.append((play_start, events[-1][0]))
    return expected, intervals, menu


def timer(host):
    out = []
    for t, clock, msg in host:
        m = re.match(r"TIMER (\w+)(?: (\d+))?(?: game=(\S+))?", msg)
        if m:
            g = m.group(3)
            out.append({"kind": m.group(1), "t": t, "clock": clock,
                        "game": None if g in (None, "None") else float(g)})
    return out


def played(intervals, until):
    return sum(max(0.0, min(b, until) - a) for a, b in intervals if a < until), \
        sum(1 for a, _ in intervals if a < until)


def check(events_path, host_path, reset, n_splits):
    events, host = parse(events_path), parse(host_path)
    expected, intervals, menus = game_truth(events)
    tm = timer(host)
    checks = []

    def rec(name, ok, detail):
        checks.append({"check": name, "pass": bool(ok), "detail": detail})

    loaded = [m for _, _, m in host if m.startswith("HOST loaded")]
    rec("script loaded", bool(loaded), loaded[0] if loaded else "no 'HOST loaded' line: " +
        "; ".join(m for _, _, m in host if m.startswith("HOST"))[:300])
    errors = [m for _, _, m in host if m.startswith("HOST update error")]
    rec("no script errors", not errors, errors[0][:200] if errors else "none")

    # attempts: the timer runs from a start to a reset. The first gameplay starts one; with
    # resets (IL), so does the first gameplay after each menu visit.
    plays = [e for e in expected if e["kind"] == "play"]
    attempts, menu_resets = [], []
    for p in plays:
        if not attempts or (reset and any(attempts[-1] < m < p["t"] for m in menus)):
            if attempts:
                menu_resets.append(min(m for m in menus if attempts[-1] < m < p["t"]))
            attempts.append(p["t"])
    if reset and attempts:
        later = [m for m in menus if m > attempts[-1]]
        if later:
            menu_resets.append(later[0])

    starts = [x for x in tm if x["kind"] == "start"]
    rec("start count", len(starts) == len(attempts), f"game: {len(attempts)} run start(s); timer: {len(starts)} start(s)")
    for i, a_t in enumerate(attempts):
        st = starts[i]["t"] if i < len(starts) else None
        ok = st is not None and a_t - 0.02 <= st <= a_t + START_LAG
        rec(f"start {i + 1}", ok, f"gameplay began {a_t:.3f}; timer start "
            + (f"{st:.3f} ({st - a_t:+.3f} s, allowed {START_LAG})" if st is not None else "none"))

    want = [e for e in expected if e["kind"] in ("stage", "win")]
    splits = [x for x in tm if x["kind"] == "split"]
    rec("split count", len(splits) == len(want) and (n_splits is None or len(want) == n_splits),
        f"game: {len(want)} stage end(s)/win(s)" + (f" (case expects {n_splits})" if n_splits is not None else "")
        + f"; timer: {len(splits)} split(s)")
    for i, e in enumerate(want):
        s = splits[i] if i < len(splits) else None
        label = f"{e['map']} -> {e['next']}" if e["kind"] == "stage" else f"{e['map']} won"
        if s is None:
            rec(f"split {i + 1} ({label})", False, "no split")
            continue
        if e["kind"] == "stage":
            dt = e["t"] - s["t"]
            # no upper bound: the touch frame can stall for seconds (2.5 s seen) before the
            # load starts; "on the touch" below is the precise check
            ok = dt >= -STAGE_AFTER_LOAD
            when = f"{dt:.3f} s before" if dt >= 0 else f"{-dt:.3f} s after"
            detail = f"split {when} the load started (allowed: any time before, or {STAGE_AFTER_LOAD} s after)"
            # v2.2: on the touch means before the load, every time (the load-start fallback is
            # only a safety net; v2.1 used it for a stage whose name was already pending)
            rec(f"split {i + 1} ({label}) before the load", dt > 0,
                f"split {when} the load started (must be before)")
            if e.get("touch") is not None:
                dtouch = s["t"] - e["touch"]
                rec(f"split {i + 1} ({label}) on the touch", -0.02 <= dtouch <= TOUCH_LAG,
                    f"split {dtouch:+.3f} s after the exit touch (request byte 0 -> 1; allowed -0.02 to "
                    f"+{TOUCH_LAG}); the byte stayed set {e['t'] - e['touch']:.3f} s, until the load started")
            else:
                rec(f"split {i + 1} ({label}) on the touch", False, "no request byte 0 -> 1 seen before this load")
        else:
            dc = None if s["clock"] is None or e["clock"] is None else s["clock"] - e["clock"]
            ok = dc is not None and 0 <= dc <= WIN_FRAMES_MS
            detail = f"split {dc} ms (game clock) after the win flag (allowed 0-{WIN_FRAMES_MS})"
        rec(f"split {i + 1} ({label})", ok, detail)
        base = max([a for a in attempts if a <= s["t"]] or [0.0])
        exp_game, n = played([iv for iv in intervals if iv[0] >= base - 0.001], s["t"])
        tol = GAME_TIME_TOL + 0.02 * n
        ok = s["game"] is not None and abs(s["game"] - exp_game) <= tol
        rec(f"split {i + 1} game time", ok, f"timer {s['game']} s, played {exp_game:.3f} s over {n} "
            f"interval(s) (tolerance {tol:.2f})")

    resets = [x for x in tm if x["kind"] == "reset"]
    if reset:
        rec("reset count", len(resets) == len(menu_resets),
            f"game: {len(menu_resets)} menu visit(s) during a run; timer: {len(resets)} reset(s)")
        for i, m in enumerate(menu_resets):
            rt = resets[i]["t"] if i < len(resets) else None
            ok = rt is not None and m - 0.02 <= rt <= m + RESET_LAG
            rec(f"reset {i + 1}", ok, f"menu at {m:.3f}; " + (f"reset at {rt:.3f} ({rt - m:+.3f} s, allowed "
                f"{RESET_LAG})" if rt is not None else "no reset"))
    else:
        visits = [m for m in menus if attempts and m > attempts[0]]
        rec("no reset", not resets, f"{len(visits)} menu visit(s) after the start, {len(resets)} reset(s)")
    return checks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("events")
    ap.add_argument("host")
    ap.add_argument("--reset", choices=["yes", "no"], default="yes")
    ap.add_argument("--splits", type=int)
    ap.add_argument("--json")
    a = ap.parse_args()
    checks = check(a.events, a.host, a.reset == "yes", a.splits)
    for c in checks:
        print(f"  [{'PASS' if c['pass'] else 'FAIL'}] {c['check']}: {c['detail']}")
    if a.json:
        json.dump(checks, open(a.json, "w", encoding="utf-8"), indent=2)
    return 0 if all(c["pass"] for c in checks) else 1


if __name__ == "__main__":
    sys.exit(main())
