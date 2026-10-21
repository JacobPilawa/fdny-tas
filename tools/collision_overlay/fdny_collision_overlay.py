#!/usr/bin/env python3
# =============================================================================
#  fdny_collision_overlay.py  --  live overlay for FDNY's out-of-bounds warp
# =============================================================================
#
#  WHAT IT SHOWS
#  -------------
#  Genesis.dll keeps ONE process-wide "last collision hit" slot (impact xyz +
#  hit plane).  Every trace from anyone -- player, victim, fire, hose, axe,
#  engine lighting -- overwrites it, and nothing ever clears it.  When the
#  player's own trace starts inside solid geometry, the engine reports a hit
#  WITHOUT writing a new impact, and the player is handed whatever the slot
#  still holds.  If that plane is not floor-like (normal.Y <= 0.9) the game's
#  step code commits it as the player's position: the warp.
#  (KB article: fdny-oob-warp-stale-impact.)
#
#  The overlay, top to bottom:
#    WARP                          stance, ground and position on one line (stance
#                                  shows "Bugged" during the crouchbug: standing
#                                  stance with the crouched 25-unit box); one
#                                  status line for the NEXT frame, predicted from
#                                  the map's BSP (read from <game>\Data\Maps):
#                                  in bounds / in solid - how far too high, or
#                                  what blocks it / LINED UP / WARP NEXT FRAME;
#                                  and, only while inside solid, the Target: the
#                                  slot and who wrote it (in bounds your own
#                                  sweeps rewrite it every frame).  The prediction
#                                  runs the four sweeps (move, step-up, forward,
#                                  step-down) with a literal port of Genesis' box
#                                  clip, checked against the real DLL code on all
#                                  17 maps.  World geometry only (not doors /
#                                  lifts).  The gap and sweep details go to the log.
#    LAST WARP                     the most recent warp: to / from / source
#    HISTORY                       the last 10 WARP-CAPABLE hits and warps:
#                                  a write counts only if its plane is not a
#                                  floor (normal.y <= 0.9) AND it was still in
#                                  the slot when your next sweep began
#  The same rows, with function names, return addresses, plane values and the
#  per-iteration sweep breakdown of every warp, are printed to the console
#  (nothing is saved to disk).  (Rules: WARP_CONDITIONS.md.)
#
#  HOTKEYS (read with GetAsyncKeyState, so the game still receives them)
#    F9   show / hide the overlay
#    F10  freeze / unfreeze the display (logging continues)
#    F12  clear history + last warp
#
#  REQUIREMENTS
#  ------------
#    python -m pip install pymem          (not needed for --demo)
#    dgVoodoo set to fake fullscreen so the overlay can draw over the game:
#    copy dgVoodoo.overlay.conf (next to this script) over the game's
#    dgVoodoo.conf.  Or run the game windowed and use --windowed.
#
#  USAGE
#    python fdny_collision_overlay.py              attach to the running game
#    python fdny_collision_overlay.py --demo       synthetic events, no game
#    python fdny_collision_overlay.py --windowed   normal movable window
#    python fdny_collision_overlay.py --corner tl  tl / tr / bl / br
#    python fdny_collision_overlay.py --maps DIR   .bsp folder if not <game>\Data\Maps
# =============================================================================

import argparse
import collections
import ctypes
import datetime
import math
import os
import random
import struct
import sys
import time
import tkinter as tk
import tkinter.font as tkfont

# ---- display parameters -----------------------------------------------------
HISTORY_ROWS     = 10
REFRESH_MS       = 50          # overlay redraw / ring poll period
WARP_FLASH_SEC   = 1.0         # LAST WARP box flashes this long after a warp
AGE_DIM_TICKS    = 180         # history rows older than this are dimmed (~3 s)
OVERLAY_ALPHA    = 0.88
FONT_FAMILY      = "Consolas"
FONT_SIZE        = 11
MARGIN_PX        = 12

FLOOR_NY         = 0.9         # BModelEnt::Think step branch gate: normal.Y <= 0.9 warps
WALL_NY          = 0.1         # |normal.Y| below this is called a wall

VK_F9, VK_F10, VK_F12 = 0x78, 0x79, 0x7B

COLORS = {
    "bg":      "#101418",
    "frame":   "#7c8894",
    "title":   "#d8e0e8",
    "header":  "#8a96a2",
    "text":    "#e8e8e8",
    "dim":     "#5c646c",
    "alert":   "#ff3b3b",
    "ok":      "#7fd17f",
    "warn":    "#ffcc4d",
    "warp_bg": "#5a1010",
    "flash_bg":"#b02020",
}
SOURCE_COLORS = {
    "Victim": "#ffa040",
    "Fire":   "#ff5a5a",
    "Hose":   "#5aaaff",
    "HoseRope": "#3f7fbf",
    "Axe":    "#ffd84a",
    "Player": "#d0d0d0",
    "Engine": "#8a8a8a",
    "Other":  "#b48cff",
}

# ---- layout: one column grid shared by every box ----------------------------
#   Tick(6) _ _ Source(8) X(10) Y(10) Z(10) _ _ Surface
W_INNER = 61                       # characters between "| " and " |" (fits "Slope x99")
COORD_COL = 19                     # where the X column starts ("Standing  -  In air")
SRC_W = COORD_COL - 8              # Source column width (after "Tick" + 2 spaces)


def fmt_coords(p):
    return "%10.1f%10.1f%10.1f" % p


def fmt_row(tick, source, pos, surface):
    return "%6d  %-*s%s  %s" % (tick, SRC_W, source, fmt_coords(pos), surface)


TABLE_HEADER = "%6s  %-*s%10s%10s%10s  %s" % ("Tick", SRC_W, "Source", "X", "Y", "Z", "Surface")
XYZ_HEADER = " " * COORD_COL + "%10s%10s%10s" % ("X", "Y", "Z")


CROUCH_BOX_MAX_Y = 55.0       # box maxs.y is 85 standing / 25 crouched; below this = crouched box


def crouchbug(p):
    """True when the stance is not 'crouched' but the collision box is the
    crouched one (research.db H235): camera and speed follow the stance,
    collision and the warp follow the box.  Left by getting off a climb
    (unclimb / jump off) or respawning at the checkpoint while crouched."""
    return bool(p and p.get("maxs") and p["state"] != 4 and p["maxs"][1] < CROUCH_BOX_MAX_Y)


def track_crouchbug(m, log):
    """Log when the crouchbug starts and ends (the overlay shows it live)."""
    bug = crouchbug(m.player)
    if bug != m.crouchbug:
        m.crouchbug = bug
        p = m.player
        log.note(("crouchbug ON at tick %d: stance %d with the crouched %.0f-unit box top, "
                  "pos %.1f %.1f %.1f -- camera/speed are standing, collision and the warp "
                  "use the short box" % ((m.tick, p["state"], p["maxs"][1]) + tuple(p["pos"])))
                 if bug else
                 ("crouchbug OFF at tick %d" % m.tick))


def surface_name(ny):
    if ny is None:
        return "?"
    if ny > FLOOR_NY:
        return "Floor"
    if ny < -WALL_NY:
        return "Ceiling"
    if ny < WALL_NY:
        return "Wall"
    return "Slope"


def warp_allowed(ny):
    return ny is not None and ny <= FLOOR_NY


# =============================================================================
#  Event model
# =============================================================================
class Ev:
    """One collision occurrence.

    kind:  'hit'     trace wrote a fresh impact into the slot
           'reused'  trace reported a hit but wrote nothing (stale slot read)
           'miss'    trace hit nothing
           'commit'  BModelEnt::Think wrote the step-down impact into the
                     player's position
           'warp'    a commit whose step-down impact was reused (the warp)
    role:  the player's per-frame sweep sequence -- 'first' / 'resweep'
           (DepenetrateFromWorld), 'stepup' / 'forward' / 'stepdown' (Think),
           'commit'; None for everyone else.
    pre:   the slot's impact just BEFORE this trace ran.
    """
    __slots__ = ("seq", "tick", "kind", "source", "func", "chain", "pos",
                 "normal", "dist", "src_ev", "frm", "wall", "role", "pre",
                 "count", "last_tick", "detail")

    def __init__(self, seq, tick, kind, source, func="", chain=(), pos=None,
                 normal=None, dist=None, src_ev=None, frm=None, role=None, pre=None):
        self.seq, self.tick, self.kind, self.source = seq, tick, kind, source
        self.func, self.chain, self.pos = func, chain, pos
        self.normal, self.dist, self.src_ev, self.frm = normal, dist, src_ev, frm
        self.role, self.pre = role, pre
        self.count, self.last_tick, self.detail = 1, tick, ""
        self.wall = time.monotonic()

    @property
    def ny(self):
        return self.normal[1] if self.normal else None


class Model:
    WRITERS_MAX = 50000

    def __init__(self):
        # every fresh write keyed by coords (for attribution of reused hits);
        # the per-frame flood of player/engine traces lives only here
        self.writers = collections.OrderedDict()
        # HISTORY = warp-capable hits + warps only
        self.history = collections.deque(maxlen=500)
        self.last_warp = None
        self.player = None         # dict: pos, state, ground
        self.slot = None           # (pos, normal or None): what a reused sweep is handed
        self.slot_writer = None
        self.verdict = None        # dict: tick, level, text  (last finished frame)
        self.crouchbug = False     # stance/box desync, see crouchbug()
        self.window = None         # dict from Bsp.predict(): the NEXT frame, from map geometry
        self.predictions = collections.deque(maxlen=256)    # (tick, window) for checking warps
        self.pred_stats = {"polls": 0, "warp_polls": 0, "warps": 0, "warps_predicted": 0}
        self.tick = 0
        self.seq = 0

    def set_window(self, w):
        if w is not None:
            w["tick"] = self.tick
            if w.get("sweeps") is not None or w["level"] == "clear":
                self.pred_stats["polls"] += 1
                self.pred_stats["warp_polls"] += w["level"] == "warp"
                self.predictions.append((self.tick, w))
        self.window = w

    def prediction_before(self, tick):
        """Latest prediction made from the state just before frame `tick` ran."""
        for t, w in reversed(self.predictions):
            if t <= tick:
                return w
        return None

    def next_seq(self):
        self.seq += 1
        return self.seq

    def note_write(self, ev):
        self.writers[ev.pos] = ev
        self.writers.move_to_end(ev.pos)
        if len(self.writers) > self.WRITERS_MAX:
            self.writers.popitem(last=False)

    def find_writer(self, pos):
        """Most recent fresh write that put exactly these coords in the slot."""
        return self.writers.get(pos)

    def add_capable(self, w, tick):
        """A non-floor write that was still in the slot when the player's sweep
        began.  Returns the history row and whether it is new (repeats of the
        same source+coords just bump the row's count)."""
        for row in list(self.history)[-4:]:
            if row.kind == "hit" and row.source == w.source and row.pos == w.pos:
                if row.last_tick != tick:
                    row.count += 1
                    row.last_tick = tick
                return row, False
        row = Ev(self.next_seq(), w.tick, "hit", w.source, w.func, w.chain, w.pos,
                 w.normal, w.dist)
        row.last_tick = tick
        self.history.append(row)
        return row, True

    def add_warp(self, ev):
        self.history.append(ev)
        self.last_warp = ev

    def clear(self):
        self.history.clear()
        self.last_warp = None


# =============================================================================
#  Per-frame analysis -- the player's sweep sequence -> verdict / ARMED / warp
# =============================================================================
#  From WARP_CONDITIONS.md.  Each frame DepenetrateFromWorld sweeps along the
#  player's displacement; on a hit, BModelEnt::Think either clips (latch set,
#  or plane normal.y > 0.9 = floor) or tries a step: up 16 (a hit cancels),
#  forward (a hit cancels), down 17 (a miss cancels, a hit COMMITS that
#  impact as the position).  Up to 5 sweep+Think iterations per frame.
#  A warp = a commit whose step-down was handed the stale slot.

OUTCOME_TEXT = {
    "clear":   "clear",
    "floor":   "floor contact",
    "latched": "already stepped",
    "stepup":  "step-up blocked",
    "forward": "forward blocked",
    "nofloor": "no floor below",
    "stepped": "stepped up",
    "warp":    "WARPED",
}


def iteration_outcome(it, committed_before):
    s = it["sweep"]
    if s.kind == "miss":
        return "clear"
    steps = {e.role: e for e in it["steps"]}
    if "stepup" not in steps:
        return "latched" if committed_before else "floor"
    if steps["stepup"].kind != "miss":
        return "stepup"
    if steps.get("forward") is None or steps["forward"].kind != "miss":
        return "forward"
    sd = steps.get("stepdown")
    if sd is None or sd.kind == "miss":
        return "nofloor"
    return "warp" if sd.kind == "reused" else "stepped"


class FrameAnalyzer:
    def __init__(self, model, log):
        self.m, self.log = model, log
        self.tick = None
        self.iters = []

    def feed(self, ev):
        m = self.m
        if self.tick is not None and ev.tick != self.tick:
            self.finish()
        self.tick = ev.tick
        if ev.kind == "hit":
            m.note_write(ev)
        if ev.kind == "reused" and ev.src_ev is None and ev.pre is not None:
            ev.src_ev = m.find_writer(ev.pre)
        role = ev.role
        if role == "first":
            # the slot exactly as the player's sweep found it this frame
            w = m.find_writer(ev.pre) if ev.pre is not None else None
            m.slot, m.slot_writer = (ev.pre, None), w
            if w is not None and w.source != "Player" and warp_allowed(w.ny):
                row, new = m.add_capable(w, ev.tick)
                if new:
                    self.log.capable(row, ev.tick)
        if role in ("first", "resweep"):
            self.iters.append({"sweep": ev, "steps": [], "commit": None})
        elif role in ("stepup", "forward", "stepdown") and self.iters:
            self.iters[-1]["steps"].append(ev)
        elif role == "commit" and self.iters:
            it = self.iters[-1]
            it["commit"] = ev
            sd = next((e for e in it["steps"] if e.role == "stepdown"), None)
            if sd is not None and sd.kind == "reused":
                w = sd.src_ev
                warp = Ev(m.next_seq(), ev.tick, "warp", w.source if w else "Unknown",
                          "Think step-down commit (reused impact)", ev.chain,
                          ev.pos, w.normal if w else ev.normal, None, src_ev=w, frm=ev.frm)
                warp.detail = self._describe(self.iters)
                pw = m.prediction_before(ev.tick)
                m.pred_stats["warps"] += 1
                if pw is not None and pw["level"] == "warp":
                    m.pred_stats["warps_predicted"] += 1
                warp.detail += "; predicted: " + describe_window(pw, ev.tick)
                m.add_warp(warp)
                self.log.warp(warp)

    def end_of_poll(self, live_tick):
        if self.tick is not None and live_tick > self.tick:
            self.finish()

    @staticmethod
    def _describe(iters):
        parts = []
        committed = False
        for i, it in enumerate(iters):
            o = iteration_outcome(it, committed)
            committed |= it["commit"] is not None
            seq = [it["sweep"]] + it["steps"]
            parts.append("#%d %s [%s]" % (i + 1, OUTCOME_TEXT[o],
                         " ".join("%s:%s" % (e.role, e.kind) for e in seq)))
        return "; ".join(parts)

    def finish(self):
        m, iters = self.m, self.iters
        tick = self.tick
        self.iters, self.tick = [], None
        if not iters:
            return
        outs, committed = [], False
        for it in iters:
            outs.append(iteration_outcome(it, committed))
            committed |= it["commit"] is not None
        reused_i = next((i for i, it in enumerate(iters) if it["sweep"].kind == "reused"), None)
        if "warp" in outs:
            level, text = "warp", "WARPED"
        elif reused_i is not None:
            o = outs[reused_i]
            if o in ("floor", "latched"):
                level, text = "solid", "inside solid - slot is a floor (no warp)"
            else:
                level, text = "armed", "ARMED - " + OUTCOME_TEXT[o]
        else:
            o = outs[0]
            level = "clear"
            text = {"clear": "no contact", "floor": "on floor", "stepup": "against a wall",
                    "forward": "against a wall", "nofloor": "at a ledge",
                    "stepped": "stepped up", "latched": "stepped up"}.get(o, OUTCOME_TEXT[o])
        m.verdict = {"tick": tick, "level": level, "text": text}


# =============================================================================
#  Session text log -- warp-capable hits and warps, with raw details.
#  Written to the console only (nothing is saved to disk).
# =============================================================================
class _NullStream:
    def write(self, s):
        pass


class TextLog:
    def __init__(self, stream=None):
        # pythonw has no console: sys.stdout is None, so drop the text
        self.f = stream or sys.stdout or _NullStream()
        self.f.write(
            "# FDNY collision log  %s\n"
            "# Only what can warp the player:\n"
            "#   capable = a write with a non-floor plane (normal.y <= %.2f) that was still\n"
            "#             in the slot when the player's sweep began (first time seen)\n"
            "#   warp    = the player was moved onto a reused impact; 'frame' lists every\n"
            "#             sweep+Think iteration that frame (role:result); 'predicted' is\n"
            "#             what the WARP status said just before that frame ran\n"
            "# chain = return addresses, innermost first (exe callers of the trace)\n#\n"
            "#%7s %7s %7s  %-7s  %-6s  %-34s %10s %10s %10s  %7s %7s %7s  %s\n"
            % (datetime.datetime.now().isoformat(" ", "seconds"), FLOOR_NY,
               "seq", "tick", "at", "kind", "source", "function", "x", "y", "z",
               "nX", "nY", "nZ", "chain / notes"))

    def _line(self, ev, kind, at, notes):
        p = ev.pos or (float("nan"),) * 3
        n = ev.normal or (float("nan"),) * 3
        chain = " ".join("%08x" % a for a in ev.chain)
        self.f.write("%8d %7d %7d  %-7s  %-6s  %-34s %10.1f %10.1f %10.1f  %7.3f %7.3f %7.3f  %s%s\n"
                     % (ev.seq, ev.tick, at, kind, ev.source, ev.func[:34],
                        p[0], p[1], p[2], n[0], n[1], n[2], chain, notes))

    def capable(self, row, consumed_tick):
        """'at' = the tick the player's sweep found it in the slot."""
        self._line(row, "capable", consumed_tick,
                   "  %s, survived %d tick(s)" % (surface_name(row.ny), consumed_tick - row.tick))

    def warp(self, ev):
        w = ev.src_ev
        src = ("<- %s %s written @tick %d (%s)" % (w.source, w.func, w.tick, surface_name(w.ny))
               if w else "<- writer unknown (engine lighting ray or before attach)")
        frm = ("  from %.1f %.1f %.1f" % ev.frm) if ev.frm else ""
        self._line(ev, "WARP", ev.tick, "  %s%s  frame: %s" % (src, frm, ev.detail))

    def note(self, text):
        self.f.write("# %s  %s\n" % (datetime.datetime.now().strftime("%H:%M:%S"), text))

    def close(self):
        if hasattr(self.f, "flush"):
            self.f.flush()


# =============================================================================
#  Warp window -- predicts the NEXT frame from the map's BSP
# =============================================================================
#  From STALE_HIT_GEOMETRY.md.  Per solid leaf L (contents & 0x43, NumSides > 0)
#  the engine expands every leaf side by the player box and classifies a box
#  sweep S->E against that hull H(L):
#      S outside, segment enters H(L)   -> FRESH write
#      S inside,  E inside              -> nothing
#      S inside,  E outside             -> hit WITHOUT a write (reuses the slot)
#  A model is a fresh hit if any leaf wrote, else a stale hit if any leaf was
#  left, else a miss.  Replaying the player's four sweeps (move, step-up 16,
#  forward, step-down 17) with that rule says whether the next frame warps.
#  World model only: doors/lifts (entity models) are not included.
GBSP_MODELS, GBSP_NODES, GBSP_LEAFS, GBSP_LEAFSIDES, GBSP_PLANES = 1, 2, 4, 8, 10
PLAYER_MASK  = 0x43
STEP_UP      = 16.0            # BModelEnt this+0x138
STEP_DOWN    = 17.0            # step-down delta (0,-(16+1),0)
G_PLANES_PTR = 0x10362ef8      # Genesis: pointer to the loaded world's GFX_Plane array
MAP_SIG_BYTES = 0x4000         # plane-array prefix used to recognise the loaded map
MAP_CHECK_SEC = 1.0
ON_EPSILON   = struct.unpack("<f", bytes.fromhex("cdcccc3d"))[0]   # Genesis 0x10051330


def f32(v):
    """Round to float32, as the engine's FSTP dword does."""
    return struct.unpack("<f", struct.pack("<f", v))[0]


def gbsp_chunks(f):
    """{chunk type: (file offset, record size, count)} of an open .bsp file."""
    f.seek(0, 2)
    end, p, ch = f.tell(), 0, {}
    while p + 12 <= end:
        f.seek(p)
        t, sz, n = struct.unpack("<iii", f.read(12))
        p += 12
        ch.setdefault(t, (p, sz, n))
        if t == 0xffff or sz < 0 or n < 0:
            break
        p += sz * n
    return ch


def read_chunk(f, ch, t, limit=None):
    off, sz, n = ch[t]
    f.seek(off)
    return f.read(sz * n if limit is None else min(sz * n, limit)), sz, n


class Bsp:
    def __init__(self, path):
        self.path = path
        self.name = os.path.splitext(os.path.basename(path))[0]
        with open(path, "rb") as f:
            ch = gbsp_chunks(f)
            d, sz, n = read_chunk(f, ch, GBSP_PLANES)
            self.planes = [struct.unpack_from("<4f", d, i * sz) for i in range(n)]
            d, sz, n = read_chunk(f, ch, GBSP_NODES)
            self.nodes = [struct.unpack_from("<2i8xi", d, i * sz) for i in range(n)]
            d, sz, n = read_chunk(f, ch, GBSP_LEAFSIDES)
            sides = [struct.unpack_from("<ii", d, i * sz) for i in range(n)]
            d, sz, n = read_chunk(f, ch, GBSP_MODELS)
            model0 = struct.unpack_from("<2i3f3f3f8i", d, 0)
            self.root = model0[0]
            d, sz, n = read_chunk(f, ch, GBSP_LEAFS)
        # solid leaves: their side planes, flipped to face out of the solid
        self.solid = {}
        for li in range(n):
            leaf = struct.unpack_from("<i3f3f8i", d, li * sz)
            cont, first, count = leaf[0], leaf[13], leaf[14]
            if not (cont & PLAYER_MASK) or count == 0:
                continue
            hull = []
            for pn, pside in sides[first:first + count]:
                nx, ny, nz, dist = self.planes[pn]
                hull.append((-nx, -ny, -nz, -dist) if pside else (nx, ny, nz, dist))
            self.solid[li] = hull
        self.hulls = {}

    def expanded(self, leaf, mins, maxs):
        """Leaf sides pushed out by the box (Genesis FUN_10035320)."""
        key = (leaf, mins, maxs)
        h = self.hulls.get(key)
        if h is None:
            if len(self.hulls) > 50000:
                self.hulls.clear()
            h = []
            for nx, ny, nz, d in self.solid[leaf]:
                for i, c in enumerate((nx, ny, nz)):
                    d = f32(d - c * (mins[i] if c > 0 else maxs[i]))    # FSTP dword per axis
                h.append((nx, ny, nz, d))
            self.hulls[key] = h
        return h

    def walk(self, lo, hi):
        """Solid leaves whose BSP cell meets the AABB lo..hi (Genesis FUN_100371f0)."""
        stack = [self.root]
        while stack:
            n = stack.pop()
            while n >= 0:
                front, back, pn = self.nodes[n]
                nx, ny, nz, d = self.planes[pn]
                dmax = (nx * (hi[0] if nx > 0 else lo[0]) + ny * (hi[1] if ny > 0 else lo[1])
                        + nz * (hi[2] if nz > 0 else lo[2]))
                dmin = (nx * (lo[0] if nx > 0 else hi[0]) + ny * (lo[1] if ny > 0 else hi[1])
                        + nz * (lo[2] if nz > 0 else hi[2]))
                f, b = dmax >= d, dmin < d
                if f and b:                     # engine order: front subtree first
                    stack.append(back)
                    n = front
                elif f:
                    n = front
                elif b:
                    n = back
                else:
                    break
            if n < 0 and (-1 - n) in self.solid:
                yield -1 - n

    def sweep(self, S, E, mins, maxs):
        """('miss' | 'hit' | 'stale', normal of a fresh hit or None) for the world model."""
        lo = tuple(min(S[i], E[i]) + mins[i] - 1.0 for i in range(3))
        hi = tuple(max(S[i], E[i]) + maxs[i] + 1.0 for i in range(3))
        st = {"e90": False, "best": 9999.0, "normal": None, "f44": False, "S": S}
        for leaf in self.walk(lo, hi):
            st["f44"] = False
            self._clip(self.expanded(leaf, mins, maxs), S, E, 0, True, st)
        if st["normal"] is not None:
            return "hit", st["normal"]
        return ("stale" if st["e90"] else "miss"), None

    def _clip(self, hull, F, B, side, pside, st):
        """Literal port of Genesis' per-leaf box clip FUN_10036ea0 (epsilon 0.1,
        t clamped to [0,1], fresh write only if closer than the model's best)."""
        if not pside:
            return False
        if side >= len(hull):
            return True                          # inside every side: no e90 write
        nx, ny, nz, d = hull[side]
        fd = nx * F[0] + ny * F[1] + nz * F[2] - d
        bd = nx * B[0] + ny * B[1] + nz * B[2] - d
        if fd >= 0 and bd >= 0:
            return False
        if fd < 0 and bd < 0:
            return self._clip(hull, F, B, side + 1, True, st)
        side2 = fd < 0
        t = ((fd + ON_EPSILON) if side2 else (fd - ON_EPSILON)) / (fd - bd)
        t = 0.0 if t < 0 else (1.0 if t > 1 else t)
        I = (f32(F[0] + t * (B[0] - F[0])), f32(F[1] + t * (B[1] - F[1])),
             f32(F[2] + t * (B[2] - F[2])))
        if self._clip(hull, F, I, side + 1, side2, st):
            st["e90"] = True
            return True
        if not self._clip(hull, I, B, side + 1, not side2, st):
            return False
        S = st["S"]
        dist = math.sqrt((I[0] - S[0]) ** 2 + (I[1] - S[1]) ** 2 + (I[2] - S[2]) ** 2)
        st["e90"] = True
        if dist < st["best"] and not st["f44"]:
            st["best"], st["normal"], st["f44"] = f32(dist), (nx, ny, nz), True
        return True

    def inside(self, p, mins, maxs):
        """Solid leaves whose expanded hull strictly contains the box origin p."""
        lo = tuple(p[i] + mins[i] - 1.0 for i in range(3))
        hi = tuple(p[i] + maxs[i] + 1.0 for i in range(3))
        return [leaf for leaf in self.walk(lo, hi)
                if all(nx * p[0] + ny * p[1] + nz * p[2] - d < 0
                       for nx, ny, nz, d in self.expanded(leaf, mins, maxs))]

    def predict(self, pos, mins, maxs, disp, slot_ny):
        """What the player's next frame would do, if disp stays as it is."""
        pos, mins, maxs = tuple(pos), tuple(mins), tuple(maxs)
        dx, dy, dz = disp
        r = {"map": self.name, "inside": 0, "gap": None, "win": None, "sweeps": None,
             "level": "clear", "text": "In bounds - no warp possible"}
        inside = self.inside(pos, mins, maxs)
        r["inside"] = len(inside)
        if not inside:
            return r
        # nearest downward-facing side above the box top (the warp needs 0 < gap <= window)
        for leaf in inside:
            for nx, ny, nz, d in self.expanded(leaf, mins, maxs):
                if ny < 0:
                    gap = d - (nx * pos[0] + ny * pos[1] + nz * pos[2])
                    if r["gap"] is None or gap < r["gap"]:
                        r["gap"], r["win"] = gap, -ny + nx * dx + nz * dz
        up = (pos[0], pos[1] + STEP_UP, pos[2])
        fwd_end = (pos[0] + dx, pos[1] + STEP_UP, pos[2] + dz)
        move, mn = self.sweep(pos, (pos[0] + dx, pos[1] + dy, pos[2] + dz), mins, maxs)
        s_up, _ = self.sweep(pos, up, mins, maxs)
        s_fwd, _ = self.sweep(up, fwd_end, mins, maxs)
        s_down, _ = self.sweep(fwd_end, (fwd_end[0], fwd_end[1] - STEP_DOWN, fwd_end[2]), mins, maxs)
        r["sweeps"] = (move, s_up, s_fwd, s_down)
        gate_ny = mn[1] if move == "hit" else slot_ny
        gate_ok = gate_ny is None or gate_ny <= FLOOR_NY
        gap, win = r["gap"], r["win"]
        # the sweeps decide; the gap only explains what to change when they say no
        if move != "miss" and (s_up, s_fwd, s_down) == ("miss", "miss", "stale"):
            if move == "hit":
                r["level"], r["text"] = "armed", "LINED UP - target is your own spot (tiny move)"
            elif not gate_ok:
                r["level"], r["text"] = "armed", "LINED UP - waiting for a wall or slope target"
            else:
                r["level"], r["text"] = "warp", "WARP NEXT FRAME if you keep moving"
            return r
        r["level"] = "solid"
        if gap is None:
            r["text"] = "In solid - nothing here to line up with"
        elif gap > win:
            r["text"] = "In solid - %.1f u too high to warp" % min(gap - win, 9999.0)
        elif abs(dx) + abs(dy) + abs(dz) < 1e-6:
            r["text"] = "In solid - lined up, start moving to warp"
        elif move == "miss":
            r["text"] = "In solid - lined up, move toward open space"
        elif s_up != "miss":
            r["text"] = "In solid - lined up, but blocked above"
        elif s_fwd != "miss":
            r["text"] = "In solid - lined up, but blocked ahead"
        elif s_down == "hit":
            r["text"] = "In solid - lined up, but a floor stops it"
        else:
            r["text"] = "In solid - almost lined up"
        return r


SWEEP_NAMES = ("Move", "Up", "Fwd", "Down")


def describe_window(w, tick=None):
    """One-line text of a prediction, for the log."""
    if w is None:
        return "none"
    s = w["text"]
    if tick is not None and "tick" in w:
        s += " (made %d tick(s) before)" % (tick - w["tick"])
    if w.get("gap") is not None:
        s += ", head %.2f u above underside (window %.2f u)" % (w["gap"], w["win"])
    if w.get("sweeps"):
        s += ", " + " ".join("%s:%s" % (n, r) for n, r in zip(SWEEP_NAMES, w["sweeps"]))
    return s


class MapFinder:
    """Recognises the loaded map by comparing Genesis' in-memory plane array
    with each .bsp's plane chunk, then loads that map's BSP."""

    def __init__(self, maps_dir, log):
        self.log, self.sigs, self.current = log, {}, None
        self.last_check = -1e9
        if maps_dir and os.path.isdir(maps_dir):
            for name in sorted(os.listdir(maps_dir)):
                if not name.lower().endswith(".bsp"):
                    continue
                path = os.path.join(maps_dir, name)
                try:
                    with open(path, "rb") as f:
                        ch = gbsp_chunks(f)
                        self.sigs[path] = read_chunk(f, ch, GBSP_PLANES, MAP_SIG_BYTES)[0]
                except (OSError, KeyError, struct.error):
                    pass
        log.note("warp window: %d maps indexed from %s" % (len(self.sigs), maps_dir))

    def update(self, read_planes):
        """read_planes(n) -> the first n bytes of the live plane array, or None."""
        now = time.monotonic()
        if now - self.last_check < MAP_CHECK_SEC or not self.sigs:
            return self.current
        self.last_check = now
        live = read_planes(MAP_SIG_BYTES)
        if not live:
            return self.current
        if self.current and live[:len(self.sigs[self.current.path])] == self.sigs[self.current.path]:
            return self.current
        match = [p for p, s in self.sigs.items() if live[:len(s)] == s]
        if len(match) != 1:
            if self.current:
                self.log.note("warp window: map no longer recognised")
            self.current = None
            return None
        self.current = Bsp(match[0])
        self.log.note("warp window: map %s (%d solid leaves)" % (self.current.name, len(self.current.solid)))
        return self.current


# =============================================================================
#  Demo source -- synthetic sweep sequences, for checking the look without the game
# =============================================================================
class DemoSource:
    WRITERS = [("Victim", "VictimRescueEnt::CanReachOffset", (0.02, 0.0, 0.999)),
               ("Fire",   "FireRescueEnt::FloorMe",          (0.0, 1.0, 0.0)),
               ("Hose",   "HoseGfx::RebuildGeometry",        (-1.0, 0.0, 0.0)),
               ("Axe",    "AxeSwing::ResolveHit",            (0.0, 0.71, 0.70))]

    def __init__(self, model, log):
        self.m, self.log = model, log
        self.an = FrameAnalyzer(model, log)
        self.pos = [1204.5, -88.0, 3310.2]
        self.t0 = time.monotonic()
        self.slot = ((0.0, 0.0, 0.0), (0.0, 1.0, 0.0))
        self.insolid_until = 0

    def _ev(self, kind, source, func, pos, normal, role=None):
        m = self.m
        ev = Ev(m.next_seq(), m.tick, kind, source, func, (), pos, normal, 0.0,
                role=role, pre=self.slot[0])
        if kind == "hit":
            self.slot = (pos, normal)
        elif kind == "reused":
            ev.pos, ev.normal = self.slot
        self.an.feed(ev)
        return ev

    def poll(self):
        m = self.m
        tick = int((time.monotonic() - self.t0) * 60)
        while m.tick < tick:
            m.tick += 1
            t = m.tick
            if t > self.insolid_until and random.random() < 0.006:
                self.insolid_until = t + random.randint(60, 240)
            insolid = t <= self.insolid_until
            self.pos[0] += random.uniform(-2, 2)
            self.pos[2] += random.uniform(-2, 2)
            feet = (round(self.pos[0], 1), round(self.pos[1] - 30, 1), round(self.pos[2], 1))
            # the player's sweep sequence
            if insolid:
                s = self._ev("reused", "Player", "DepenetrateFromWorld", None, None, "first")
            else:
                s = self._ev("hit", "Player", "DepenetrateFromWorld", feet, (0.0, 1.0, 0.0), "first")
            if s.ny is not None and warp_allowed(s.ny):
                up = self._ev("miss" if random.random() < 0.6 else "reused", "Player", "Think step-up",
                              None, None, "stepup")
                if up.kind == "miss":
                    fw = self._ev("miss" if random.random() < 0.6 else "reused", "Player",
                                  "Think step-forward", None, None, "forward")
                    if fw.kind == "miss":
                        sd = self._ev("reused" if random.random() < 0.5 else "miss", "Player",
                                      "Think step-down", None, None, "stepdown")
                        if sd.kind == "reused":
                            frm = tuple(round(v, 1) for v in self.pos)
                            c = Ev(m.next_seq(), t, "commit", "Player", "Think commit", (0x0043f9c7,),
                                   sd.pos, sd.normal, None, frm=frm, role="commit")
                            self.an.feed(c)
                            self.pos = [sd.pos[0], sd.pos[1] + 30, sd.pos[2]]
                            self.insolid_until = 0
            # other writers after the player (hose/victim/fire/axe), then render floor rays
            if random.random() < 0.05:
                src, fn, n = random.choice(self.WRITERS)
                hp = (round(self.pos[0] + random.uniform(-300, 300), 1),
                      round(random.uniform(-120, 0), 1),
                      round(self.pos[2] + random.uniform(-300, 300), 1))
                self._ev("hit", src, fn, hp, n)
            if not insolid and random.random() < 0.5:
                self._ev("hit", "Engine", "engine lighting ray", (feet[0], -500.0, feet[2]), (0.0, 1.0, 0.0))
        self.an.end_of_poll(m.tick + 1)
        bugged = (m.tick // 600) % 3 == 2              # 10 s of crouchbug every 30 s
        m.player = {"pos": tuple(self.pos), "state": 3, "ground": random.random() < 0.8,
                    "mins": (-30.0, -30.0, -30.0), "maxs": (30.0, 25.0 if bugged else 85.0, 30.0)}
        track_crouchbug(m, self.log)
        m.set_window(self._window(m.tick <= self.insolid_until))
        return []

    def _window(self, insolid):
        w = {"map": "demo", "inside": 0, "gap": None, "win": None, "sweeps": None,
             "level": "clear", "text": "In bounds - no warp possible"}
        if not insolid:
            return w
        gap = 4.0 - (self.m.tick % 120) / 30.0          # box top sinking toward an underside
        w.update(inside=2, gap=gap, win=1.0)
        if 0 < gap <= 1.0:
            w["sweeps"] = ("stale", "miss", "miss", "stale")
            slot_floor = self.slot[1] is not None and self.slot[1][1] > FLOOR_NY
            w["level"], w["text"] = (("armed", "LINED UP - waiting for a wall or slope target")
                                     if slot_floor else ("warp", "WARP NEXT FRAME if you keep moving"))
        else:
            w["sweeps"] = ("miss", "miss", "miss", "miss")
            w["level"], w["text"] = "solid", "In solid - %.1f u too high to warp" % (gap - 1.0)
        return w

    def close(self):
        pass


# =============================================================================
#  Game source -- pymem code caves in Genesis.dll / FDNYFirefighter.exe
# =============================================================================
#  Every address and byte pattern below is from HOOK_SITES.md (next to this
#  script).  Genesis addresses are at its preferred base 0x10000000 and are
#  rebased if the DLL loads elsewhere; the exe has no relocations.
#
#  Hooks:
#    ENTRY   Genesis 0x10038c10   start of every geWorld_Collision (body, so
#                                 engine shadow traces are seen too): tick,
#                                 return address, stack slots that hold the
#                                 outer caller, slot contents BEFORE the call
#    EXIT_x  Genesis 0x10038d0a / 0x10038db6 / 0x10038dbe   box hit / ray hit /
#                                 miss tails: result + what the caller got
#    BOXW    Genesis 0x100371c2   box clip wrote a fresh impact  -> fresh++
#    RAYW    Genesis 0x10036633   ray clip wrote a fresh impact  -> fresh++
#            A trace that returns 1 with fresh == 0 REUSED the old slot.
#    COMMIT  exe 0x0043f9c7       BModelEnt::Think writes the step-down impact
#                                 into entity+0x4c.  Right after a reused
#                                 step-down trace, that is the warp.
#    TICK    exe 0x0042af0f       GameSession::RunFrame, after TickSubsystems and
#                                 before RenderFrame: once per SIMULATED frame
#                                 (the RunFrame entry also counts ESC-menu frames)
#
#  Nothing is ever freed: on exit the original bytes are put back but the cave
#  stays allocated, so a thread that happens to be inside it returns safely.

EXE_NAME      = "FDNYFirefighter.exe"
GENESIS_NAME  = "Genesis.dll"
EXE_BASE      = 0x00400000
GENESIS_PREF  = 0x10000000

G_ENTRY       = 0x10038c10
G_EXIT_BOX    = 0x10038d0a
G_EXIT_RAY    = 0x10038db6
G_EXIT_MISS   = 0x10038dbe
G_RESUME_ENTRY= 0x10038c17
G_BOXW        = 0x100371c2
G_RAYW        = 0x10036633
G_RAYW_RESUME = 0x1003663d
G_LATCH       = 0x10362f44     # absolute operand inside the RAYW displaced bytes
G_IMPACT      = 0x10362e9c     # x, y, z floats
G_PLANE       = 0x10362f48     # normal x, y, z, dist

X_COMMIT      = 0x0043f9c7
X_COMMIT_RESUME = 0x0043f9ce
X_TICK        = 0x0042af0f       # mov eax,[0x00503a78] (absolute; exe has no relocs)
X_TICK_RESUME = 0x0042af14
X_EVERGAME    = 0x00503a78     # g_everGame; player = *(g_everGame + 0x30)

EG_PLAYER, OFF_POS, OFF_GROUND, OFF_STATE = 0x30, 0x4c, 0x12c, 0x18c
OFF_MINS, OFF_MAXS, OFF_DISP = 0x78, 0x84, 0x90      # collision box + this frame's displacement
OFF_SPECTATOR = 0x380                                # nonzero: Depenetrate is skipped

# ---- cave memory layout ------------------------------------------------------
D_TICK, D_HEAD, D_INCALL, D_FRESH = 0x00, 0x04, 0x08, 0x0C
D_REC         = 0x40                 # scratch record, 128 bytes
CODE_O        = 0x100
RING_O        = 0x1000
REC_SIZE      = 128
RING_N        = 16384                # power of two
ALLOC_SIZE    = RING_O + REC_SIZE * RING_N

# record dword indices
R_KIND, R_TICK, R_RET0 = 0, 1, 2
R_S30, R_S34, R_S44, R_S54, R_S90, R_SC4 = 3, 4, 5, 6, 7, 8
R_RESULT, R_FRESH = 9, 10
R_IMP, R_NRM, R_DIST, R_PRE, R_THIS = 11, 14, 17, 18, 21
R_GPL = 22                           # GLOBAL plane normal: what a later reuse is handed
STACK_SLOTS = ((R_S30, 0x30), (R_S34, 0x34), (R_S44, 0x44),
               (R_S54, 0x54), (R_S90, 0x90), (R_SC4, 0xc4))
# KIND_ENGINE_RAY: a fresh ray-clip write OUTSIDE any geWorld_Collision call --
# the engine's per-actor "light from floor" ray (default on for every rendered
# actor, 30000 units straight down) and lightmap rays.  In a normal frame these
# are the last writer, which is why most frames cannot warp you (floor plane).
KIND_TRACE, KIND_COMMIT, KIND_ENGINE_RAY = 1, 2, 3
REC_FMT = "<11I3f3ff3fI3f7I"         # 32 dwords

# ---- caller classification (from HOOK_SITES.md section 6) ---------------------
RET_TRACETOFLOOR   = 0x00492da6      # outer return address at [esp+0x90]
RET_GROUNDHEIGHT   = 0x0048ffc6      # outer at [esp+0x34]
RET_SEGMENTLOS     = 0x0048ff27      # outer at [esp+0x30]; via ActiveWorld wrapper: [esp+0x44]
RET_LOS_ACTIVEWORLD= 0x0049014d
RET_SWEEPBOX       = 0x004900de      # outer at [esp+0x30]
RET_FLOORME        = 0x004486f3      # outer at [esp+0xc4]
RET_HOSEGFX        = 0x0046cbd8
RET_STEP_DOWN      = 0x0043f9b1      # Think's step-down TraceToFloor
RET_DEPEN_FIRST    = 0x0043f732      # DepenetrateFromWorld's first trace of the frame

# the player's per-frame sweep sequence, by outer return address
PLAYER_ROLES = {
    0x0043f732: "first",       # DepenetrateFromWorld, first sweep of the frame
    0x0043f7e0: "resweep",     # DepenetrateFromWorld, re-sweep after a Think
    0x0043f8bf: "stepup",      # BModelEnt::Think step-up    (a hit cancels the step)
    0x0043f920: "forward",     # BModelEnt::Think step-forward (a hit cancels)
    0x0043f9b1: "stepdown",    # BModelEnt::Think step-down  (a hit commits)
}

# Exact outer return addresses -> (source, label)
OUTER_RETS = {
    0x0043f732: ("Player", "DepenetrateFromWorld"),
    0x0043f7e0: ("Player", "DepenetrateFromWorld (2)"),
    0x0043f8bf: ("Player", "Think step-up"),
    0x0043f920: ("Player", "Think step-forward"),
    0x0043f9b1: ("Player", "Think step-down"),
    0x00447ccf: ("Fire",   "TrySpawnSpreadFire"),
    0x0045aca0: ("Victim", "CanReachOffset"),
    0x00460dd6: ("Axe",    "AxeSwing::ResolveHit"),
    0x0049daa7: ("Other",  "ModelEntity::SnapToFloor"),
    0x0049e44c: ("Other",  "ROCWorldEntity vfunc 0x0049e3e0"),
    0x0045ade3: ("Victim", "CanReachOffset (ground)"),
    0x0045ae11: ("Victim", "CanReachOffset (ground 2)"),
    0x0046781c: ("Hose",   "PlayerHose::Think_Advance"),
    0x0043fd9c: ("Other",  "BlinkLight LOS"),
    0x004484cd: ("Fire",   "Fire heat LOS ray (player -> fire)"),
    0x00482779: ("HoseRope", "hose rope node sweep (HoseRope::Step)"),
    0x00449a29: ("Fire",   "FloorMe <- FireRescueEnt::AdvanceSpread"),
    0x004492ab: ("Fire",   "FloorMe <- GotoPathPoint"),
}

# Function starts for "nearest function" fallback naming (callers of the
# ActiveWorld LOS wrapper and anything unexpected).
FUNC_STARTS = sorted([
    (0x0043dc60, "Other",  "BModelEnt_Liftable::Think"),
    (0x0043f6d0, "Player", "BModelEnt::DepenetrateFromWorld"),
    (0x0043f810, "Player", "BModelEnt::Think"),
    (0x00447a90, "Fire",   "FireRescueEnt::TrySpawnSpreadFire"),
    (0x00448630, "Fire",   "FireRescueEnt::FloorMe"),
    (0x00448a90, "Fire",   "FireRescueEnt::GotoPathPoint"),
    (0x00449340, "Fire",   "FireRescueEnt::Spawn"),
    (0x00452570, "Player", "PlayerRescueEnt::Think"),
    (0x00455230, "Other",  "RescueBot::TryStartRescue"),
    (0x0045ab40, "Victim", "VictimRescueEnt::CanReachOffset"),
    (0x0045ae50, "Victim", "VictimRescueEnt::AdvanceRoamTarget"),
    (0x0045c7f0, "Victim", "VictimRescueEnt::ValidateSpawn"),
    (0x00460b90, "Axe",    "AxeSwing::ResolveHit"),
    (0x0046c5a0, "Hose",   "HoseGfx::RebuildGeometry"),
    (0x004677d0, "Hose",   "PlayerHose::Think_Advance"),
    (0x0048fea0, "Other",  "roc_SegmentClearLOS"),
    (0x0048ff30, "Other",  "World::TraceGroundHeight"),
    (0x00490000, "Other",  "roc_SweepWorldBox"),
    (0x00492ce0, "Other",  "ROCRender::TraceToFloor"),
    (0x0049da00, "Other",  "ModelEntity::SnapToFloor"),
])
GENESIS_RETS = {
    0x10012253: "engine actor shadow",
    0x10002fd5: "engine sound occlusion",
    0x10039167: "engine TestModelMove",
}


def nearest_func(addr):
    best = None
    for start, src, name in FUNC_STARTS:
        if start <= addr:
            best = (start, src, name)
        else:
            break
    if best and addr - best[0] < 0x2000:
        return best[1], "%s+0x%x" % (best[2], addr - best[0])
    return "Other", "0x%08x" % addr


# ---- tiny x86 emitter ----------------------------------------------------------
def p32(v):
    return (v & 0xffffffff).to_bytes(4, "little")


class Asm:
    def __init__(self, base):
        self.base, self.b = base, bytearray()

    @property
    def pc(self):
        return self.base + len(self.b)

    def raw(self, bs):                  self.b += bs
    def push_eax(self):                 self.raw(b"\x50")
    def pop_eax(self):                  self.raw(b"\x58")
    def ret(self):                      self.raw(b"\xc3")
    def mov_abs_eax(self, a):           self.raw(b"\xa3" + p32(a))
    def mov_eax_abs(self, a):           self.raw(b"\xa1" + p32(a))
    def mov_abs_esi(self, a):           self.raw(b"\x89\x35" + p32(a))
    def mov_abs_imm(self, a, v):        self.raw(b"\xc7\x05" + p32(a) + p32(v))
    def inc_abs(self, a):               self.raw(b"\xff\x05" + p32(a))
    def call(self, t):                  self.raw(b"\xe8" + p32(t - (self.pc + 5)))
    def jmp(self, t):                   self.raw(b"\xe9" + p32(t - (self.pc + 5)))

    def mov_eax_esp(self, off):         # mov eax,[esp+off]
        if off < 0x80:
            self.raw(b"\x8b\x44\x24" + bytes([off]))
        else:
            self.raw(b"\x8b\x84\x24" + p32(off))

    def mov_eax_esi(self, off):         # mov eax,[esi+off]   (off < 0x80)
        self.raw(b"\x8b\x46" + bytes([off]))

    def copy_esp(self, off, dst):       # [dst] = [esp+off]
        self.mov_eax_esp(off)
        self.mov_abs_eax(dst)

    def copy_esi(self, off, dst):       # [dst] = [esi+off]
        self.mov_eax_esi(off)
        self.mov_abs_eax(dst)

    def copy_abs(self, src, dst):
        self.mov_eax_abs(src)
        self.mov_abs_eax(dst)


def jmp_patch(site, target, length):
    rel = target - (site + 5)
    return b"\xe9" + p32(rel) + b"\x90" * (length - 5)


class GameSource:
    def __init__(self, model, log, maps_dir=None):
        try:
            import pymem
            import pymem.process
        except ImportError:
            sys.exit("pymem is not installed.  Run:  python -m pip install pymem")
        self.m, self.log = model, log
        self.pm = pymem.Pymem(EXE_NAME)
        if self.pm.base_address != EXE_BASE:
            sys.exit("%s loaded at 0x%08X, expected 0x%08X -- addresses would be wrong."
                     % (EXE_NAME, self.pm.base_address, EXE_BASE))
        gmod = pymem.process.module_from_name(self.pm.process_handle, GENESIS_NAME)
        if gmod is None:
            sys.exit("%s is not loaded in the game process." % GENESIS_NAME)
        self.gdelta = gmod.lpBaseOfDll - GENESIS_PREF
        self.patches = []                 # (address, original bytes)
        self.last_head = 0
        self.dropped = 0
        self.an = FrameAnalyzer(model, log)
        self.player_ptr = 0
        self.last_player_trace_tick = -10**9
        self.maps = MapFinder(maps_dir or self._default_maps_dir(), log)
        self._install()

    def _default_maps_dir(self):
        """<folder of the running FDNYFirefighter.exe>\\Data\\Maps"""
        buf = ctypes.create_unicode_buffer(1024)
        ctypes.windll.psapi.GetModuleFileNameExW(ctypes.c_void_p(self.pm.process_handle), None, buf, 1024)
        return os.path.join(os.path.dirname(buf.value), "Data", "Maps") if buf.value else None

    def _read_planes(self, n):
        try:
            ptr = self._u32(self.g(G_PLANES_PTR))
            return self.pm.read_bytes(ptr, n) if ptr else None
        except Exception:                # no world loaded yet / array smaller than n
            return None

    def _predict(self):
        m, p = self.m, self.m.player
        bsp = self.maps.update(self._read_planes)
        if p is None:
            m.set_window(None)
            return
        if bsp is None:
            m.set_window({"map": None, "level": "none",
                          "text": "map not recognised" if self.maps.sigs else "Data\\Maps not found (use --maps)"})
            return
        if p["spectator"]:
            m.set_window({"map": bsp.name, "level": "none", "text": "spectator - no collision"})
            return
        slot_ny = struct.unpack("<3f", self.pm.read_bytes(self.g(G_PLANE), 12))[1]
        w = bsp.predict(p["pos"], p["mins"], p["maxs"], p["disp"], slot_ny)
        m.set_window(w)

    def g(self, addr):
        return addr + self.gdelta

    # -- install ------------------------------------------------------------------
    def _alloc_info(self, addr):
        """(AllocationBase, AllocationProtect, Type) of the game-side region at addr."""
        class MBI(ctypes.Structure):
            _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                        ("AllocationProtect", ctypes.c_ulong), ("RegionSize", ctypes.c_size_t),
                        ("State", ctypes.c_ulong), ("Protect", ctypes.c_ulong), ("Type", ctypes.c_ulong)]
        mbi = MBI()
        k32 = ctypes.windll.kernel32
        k32.VirtualQueryEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
        if not k32.VirtualQueryEx(ctypes.c_void_p(self.pm.process_handle), ctypes.c_void_p(addr),
                                  ctypes.byref(mbi), ctypes.sizeof(mbi)):
            return None
        return mbi.AllocationBase or 0, mbi.AllocationProtect, mbi.Type

    def _reclaim_stale(self, sites):
        """Undo hooks a previous run left behind (it was killed before cleanup).

        Only our own leftovers are touched: every patched site must JMP into the
        code area of one private read/write/execute block, which is what our
        cave looks like.  The original bytes are known, so they are rewritten.
        The old cave is left allocated (a thread could still be inside it)."""
        stale, bases = [], set()
        for k, (addr, orig) in sites.items():
            live = self.pm.read_bytes(addr, len(orig))
            if live == orig or live[:1] != b"\xe9":
                continue
            target = (addr + 5 + struct.unpack("<i", live[1:5])[0]) & 0xFFFFFFFF
            info = self._alloc_info(target)
            if (info is None or info[1] != 0x40 or info[2] != 0x20000      # PAGE_EXECUTE_READWRITE, MEM_PRIVATE
                    or not CODE_O <= target - info[0] < RING_O):
                sys.exit("0x%08X is hooked by something else (jumps to 0x%08X). "
                         "Close other tools attached to the game, or restart it." % (addr, target))
            stale.append((addr, orig))
            bases.add(info[0])
        if not stale:
            return
        if len(bases) != 1:
            sys.exit("hooks from more than one earlier run are present. Restart the game.")
        self._suspend(True)
        try:
            for addr, orig in stale:
                self.pm.write_bytes(addr, orig, len(orig))
        finally:
            self._suspend(False)
        self.log.note("removed %d hook(s) left by an earlier run (old cave 0x%08X)"
                      % (len(stale), bases.pop()))
        print("removed hooks left by an earlier run")

    def _expect(self, addr, expected):
        live = self.pm.read_bytes(addr, len(expected))
        if live[:1] == b"\xe9" and expected[:1] != b"\xe9":
            sys.exit("0x%08X is already hooked (a previous run did not clean up?). "
                     "Restart the game and try again." % addr)
        if live != expected:
            sys.exit("unexpected bytes at 0x%08X: %s (expected %s) -- different game/DLL build?"
                     % (addr, live.hex(" "), expected.hex(" ")))
        return live

    def _install(self):
        g = self.g
        sites = {
            "entry":  (g(G_ENTRY),     bytes.fromhex("83ec24 8b4c2428")),
            "x_box":  (g(G_EXIT_BOX),  bytes.fromhex("5e 83c424 c3")),
            "x_ray":  (g(G_EXIT_RAY),  bytes.fromhex("5e 83c424 c3")),
            "x_miss": (g(G_EXIT_MISS), bytes.fromhex("5e 83c424 c3")),
            "boxw":   (g(G_BOXW),      bytes.fromhex("5b 83c42c c3")),
            "rayw":   (g(G_RAYW),      b"\xc7\x05" + p32(g(G_LATCH)) + p32(1)),
            "commit": (X_COMMIT,       bytes.fromhex("8d464c 8bc8 8911")),
            "tick":   (X_TICK,         bytes.fromhex("a1783a5000")),
        }
        self._reclaim_stale(sites)
        live = {k: self._expect(a, e) for k, (a, e) in sites.items()}

        cave = self.pm.allocate(ALLOC_SIZE)
        self.cave = cave
        self.pm.write_bytes(cave, b"\x00" * RING_O, RING_O)
        TICK, HEAD, INCALL, FRESH = cave + D_TICK, cave + D_HEAD, cave + D_INCALL, cave + D_FRESH
        REC = cave + D_REC
        RING = cave + RING_O

        def rec(i):
            return REC + 4 * i

        a = Asm(cave + CODE_O)
        entry_pts = {}

        # LOG: copy REC into ring[head & mask], head++
        LOG = a.pc
        a.raw(b"\x56\x57\x51")                                  # push esi/edi/ecx
        a.raw(b"\x8b\x3d" + p32(HEAD))                          # mov edi,[HEAD]
        a.raw(b"\x81\xe7" + p32(RING_N - 1))                    # and edi,mask
        a.raw(b"\xc1\xe7\x07")                                  # shl edi,7  (*128)
        a.raw(b"\x81\xc7" + p32(RING))                          # add edi,RING
        a.raw(b"\xbe" + p32(REC))                               # mov esi,REC
        a.raw(b"\xb9" + p32(REC_SIZE // 4))                     # mov ecx,32
        a.raw(b"\xfc\xf3\xa5")                                  # cld; rep movsd
        a.inc_abs(HEAD)
        a.raw(b"\x59\x5f\x5e")                                  # pop ecx/edi/esi
        a.ret()

        # ENTRY (offsets below are +4 for the pushed eax)
        entry_pts["entry"] = a.pc
        a.push_eax()
        a.copy_esp(4, rec(R_RET0))
        for idx, off in STACK_SLOTS:
            a.copy_esp(4 + off, rec(idx))
        a.copy_abs(TICK, rec(R_TICK))
        for i in range(3):
            a.copy_abs(g(G_IMPACT) + 4 * i, rec(R_PRE + i))
        a.mov_abs_imm(FRESH, 0)
        a.mov_abs_imm(INCALL, 1)
        a.pop_eax()
        a.raw(live["entry"])
        a.jmp(g(G_RESUME_ENTRY))

        # EXIT tails: ESI = Col*, EAX = result
        for k in ("x_box", "x_ray", "x_miss"):
            entry_pts[k] = a.pc
            a.mov_abs_eax(rec(R_RESULT))
            a.push_eax()
            a.mov_abs_imm(rec(R_KIND), KIND_TRACE)
            for i in range(3):
                a.copy_esi(0x0c + 4 * i, rec(R_IMP + i))        # Col->Impact
                a.copy_esi(0x1c + 4 * i, rec(R_NRM + i))        # Col->Plane.Normal
            a.copy_esi(0x28, rec(R_DIST))
            # Ray hits can hand the CALLER a flipped plane while the global
            # keeps the BSP's stored (dominant-axis-positive) one; a later
            # reuse gets the global, so that is the one the warp gate sees.
            for i in range(3):
                a.copy_abs(g(G_PLANE) + 4 * i, rec(R_GPL + i))
            a.copy_abs(FRESH, rec(R_FRESH))
            a.mov_abs_imm(INCALL, 0)
            a.call(LOG)
            a.pop_eax()
            a.raw(live[k])                                      # pop esi; add esp,24h; ret

        # fresh-write counters
        entry_pts["boxw"] = a.pc
        a.inc_abs(FRESH)
        a.raw(live["boxw"])                                     # pop ebx; add esp,2ch; ret
        entry_pts["rayw"] = a.pc
        a.inc_abs(FRESH)
        a.raw(b"\x83\x3d" + p32(INCALL) + b"\x00")              # cmp dword [INCALL],0
        a.raw(b"\x75\x00")                                      # jnz skip (patched below)
        jnz_at = len(a.b)
        a.push_eax()                                            # outside a call: engine ray
        a.mov_abs_imm(rec(R_KIND), KIND_ENGINE_RAY)
        a.mov_abs_imm(rec(R_RET0), 0)
        a.copy_abs(TICK, rec(R_TICK))
        for i in range(3):
            a.copy_abs(g(G_IMPACT) + 4 * i, rec(R_IMP + i))
            a.copy_abs(g(G_PLANE) + 4 * i, rec(R_GPL + i))
        a.call(LOG)
        a.pop_eax()
        skip = len(a.b) - jnz_at
        assert skip < 0x80
        a.b[jnz_at - 1] = skip
        a.raw(live["rayw"])                                     # mov [latch],1
        a.jmp(g(G_RAYW_RESUME))

        # COMMIT: ESI = entity, [esp+10h..18h] = impact, [esp+34h..3ch] = normal
        entry_pts["commit"] = a.pc
        a.push_eax()
        a.mov_abs_imm(rec(R_KIND), KIND_COMMIT)
        a.copy_abs(TICK, rec(R_TICK))
        a.mov_abs_esi(rec(R_THIS))
        for i in range(3):
            a.copy_esp(4 + 0x10 + 4 * i, rec(R_IMP + i))
            a.copy_esp(4 + 0x34 + 4 * i, rec(R_NRM + i))
            a.copy_esi(OFF_POS + 4 * i, rec(R_PRE + i))         # old position
        a.call(LOG)
        a.pop_eax()
        a.raw(live["commit"])
        a.jmp(X_COMMIT_RESUME)

        # TICK
        entry_pts["tick"] = a.pc
        a.inc_abs(TICK)
        a.raw(live["tick"])
        a.jmp(X_TICK_RESUME)

        assert len(a.b) <= RING_O - CODE_O, "cave code overflow"
        self.pm.write_bytes(a.base, bytes(a.b), len(a.b))
        self.HEAD, self.RING = HEAD, RING

        self._suspend(True)
        try:
            for k, (addr, orig) in sites.items():
                patch = jmp_patch(addr, entry_pts[k], len(orig))
                self.pm.write_bytes(addr, patch, len(patch))
                self.patches.append((addr, live[k]))
        finally:
            self._suspend(False)
        self.log.note("hooks installed: cave 0x%08X, Genesis base 0x%08X"
                      % (cave, GENESIS_PREF + self.gdelta))
        print("hooks installed (cave 0x%08X)" % cave)

    def _suspend(self, on):
        nt = ctypes.windll.ntdll
        (nt.NtSuspendProcess if on else nt.NtResumeProcess)(self.pm.process_handle)

    def close(self):
        patches, self.patches = self.patches, []    # may run twice (console close + finally)
        if not patches:
            return
        try:
            self._suspend(True)
            try:
                for addr, orig in reversed(patches):
                    self.pm.write_bytes(addr, orig, len(orig))
            finally:
                self._suspend(False)
            self.log.note("hooks removed, original bytes restored")
            print("hooks removed, original bytes restored")
        except Exception as e:          # game already gone
            print("cleanup skipped:", e)

    # -- reading --------------------------------------------------------------------
    def _u32(self, a):
        return self.pm.read_uint(a)

    def _read_live(self):
        m = self.m
        eg = self._u32(X_EVERGAME)
        pl = self._u32(eg + EG_PLAYER) if eg else 0
        self.player_ptr = pl
        if pl:
            x, y, z = struct.unpack("<3f", self.pm.read_bytes(pl + OFF_POS, 12))
            state = struct.unpack("<i", self.pm.read_bytes(pl + OFF_STATE, 4))[0]
            ground = self.pm.read_bytes(pl + OFF_GROUND, 1)[0] != 0
            box = struct.unpack("<6f", self.pm.read_bytes(pl + OFF_MINS, 24))   # mins, maxs
            disp = struct.unpack("<3f", self.pm.read_bytes(pl + OFF_DISP, 12))
            spectator = self.pm.read_bytes(pl + OFF_SPECTATOR, 1)[0] != 0
            m.player = {"pos": (x, y, z), "state": state, "ground": ground,
                        "mins": box[0:3], "maxs": box[3:6], "disp": disp, "spectator": spectator}
        else:
            m.player = None
        m.tick = self._u32(self.cave + D_TICK)
        track_crouchbug(m, self.log)
        self._predict()
        # WARP box Target: the slot as the player's own first trace saw it.
        # Fall back to a live read when the player has not traced recently.
        if m.tick - self.last_player_trace_tick > 10:
            imp = struct.unpack("<3f", self.pm.read_bytes(self.g(G_IMPACT), 12))
            nrm = struct.unpack("<3f", self.pm.read_bytes(self.g(G_PLANE), 12))
            m.slot = (tuple(round(v, 1) for v in imp), nrm)
            m.slot_writer = None

    def _records(self):
        head = self._u32(self.HEAD)
        n = (head - self.last_head) & 0xffffffff
        if n == 0:
            return []
        if n > RING_N:
            self.dropped += n - RING_N
            self.log.note("ring overflow: %d records lost" % (n - RING_N))
            self.last_head = (head - RING_N) & 0xffffffff
            n = RING_N
        start = self.last_head & (RING_N - 1)
        first = min(n, RING_N - start)
        data = self.pm.read_bytes(self.RING + start * REC_SIZE, first * REC_SIZE)
        if n > first:
            data += self.pm.read_bytes(self.RING, (n - first) * REC_SIZE)
        self.last_head = head
        return struct.iter_unpack(REC_FMT, data)

    def _classify(self, r):
        ret0 = r[R_RET0]
        if ret0 == RET_TRACETOFLOOR:
            outer = r[R_S90]
        elif ret0 == RET_GROUNDHEIGHT:
            outer = r[R_S34]
        elif ret0 == RET_SEGMENTLOS:
            outer = r[R_S44] if r[R_S30] == RET_LOS_ACTIVEWORLD else r[R_S30]
        elif ret0 == RET_SWEEPBOX:
            outer = r[R_S30]
        elif ret0 == RET_FLOORME:
            outer = r[R_SC4]
            if outer not in OUTER_RETS:
                return "Fire", "FireRescueEnt::FloorMe", (ret0, outer)
        elif ret0 == RET_HOSEGFX:
            return "Hose", "HoseGfx::RebuildGeometry", (ret0,)
        else:
            rel = ret0 - self.gdelta
            if GENESIS_PREF <= rel < GENESIS_PREF + 0x400000:
                return "Engine", GENESIS_RETS.get(rel, "engine 0x%08x" % rel), (ret0,)
            src, name = nearest_func(ret0)
            return src, name, (ret0,)
        if outer in OUTER_RETS:
            src, name = OUTER_RETS[outer]
        else:
            src, name = nearest_func(outer)
        return src, name, (ret0, outer)

    def poll(self):
        m, an = self.m, self.an
        self._read_live()
        for r in self._records():
            tick = r[R_TICK]
            kind_rec = r[R_KIND]
            if kind_rec == KIND_TRACE:
                src, func, chain = self._classify(r)
                if r[R_RESULT] == 0:
                    kind = "miss"
                elif r[R_FRESH] == 0:
                    kind = "reused"
                else:
                    kind = "hit"
                role = PLAYER_ROLES.get(chain[-1]) if src == "Player" else None
                ev = Ev(m.next_seq(), tick, kind, src, func, chain,
                        tuple(round(v, 1) for v in r[R_IMP:R_IMP + 3]),
                        tuple(r[R_GPL:R_GPL + 3]),      # the slot's plane, not the caller's copy
                        r[R_DIST], role=role,
                        pre=tuple(round(v, 1) for v in r[R_PRE:R_PRE + 3]))
                if src == "Player":
                    self.last_player_trace_tick = tick
                an.feed(ev)
            elif kind_rec == KIND_ENGINE_RAY:
                an.feed(Ev(m.next_seq(), tick, "hit", "Engine",
                           "engine lighting ray (floor light / lightmap)", (),
                           tuple(round(v, 1) for v in r[R_IMP:R_IMP + 3]),
                           tuple(r[R_GPL:R_GPL + 3]), None))
            elif kind_rec == KIND_COMMIT and r[R_THIS] == self.player_ptr:
                an.feed(Ev(m.next_seq(), tick, "commit", "Player", "Think step-down commit",
                           (X_COMMIT,), tuple(round(v, 1) for v in r[R_IMP:R_IMP + 3]),
                           tuple(r[R_NRM:R_NRM + 3]), None, role="commit",
                           frm=tuple(round(v, 1) for v in r[R_PRE:R_PRE + 3])))
        an.end_of_poll(m.tick)
        return []


# =============================================================================
#  Overlay window
# =============================================================================
# "<stance>  -  <ground>" fills the COORD_COL columns left of X: stance 8 wide,
# ground 6 wide.  Each word has its own colour (normal = green).
STATE_NAMES = {3: "Standing", 4: "Crouched", 5: "Climbing", 6: "Out cold", 7: "Scripted"}
PLAYER_COLORS = {
    "Standing": "#7fd17f",
    "Crouched": "#5ad1d1",
    "Climbing": "#5aaaff",
    "Out cold": "#ff5a5a",
    "Scripted": "#b48cff",
    "Bugged":   "#ff8c1a",     # crouchbug: standing stance with the crouched box
    "Ground":   "#7fd17f",
    "In air":   "#ffcc4d",
}


class Overlay:
    def __init__(self, model, source, log, args):
        self.m, self.src, self.log, self.args = model, source, log, args
        self.frozen = False
        self.hidden = False
        self.keys = {}
        self.last_text = None

        self.root = tk.Tk()
        self.root.title("FDNY collision")
        self.root.configure(bg=COLORS["bg"])
        font = tkfont.Font(family=FONT_FAMILY, size=FONT_SIZE)
        self.txt = tk.Text(self.root, font=font, bg=COLORS["bg"], fg=COLORS["text"],
                           width=W_INNER + 4, height=HISTORY_ROWS + 14, bd=0,
                           highlightthickness=0, padx=4, pady=4, cursor="arrow")
        self.txt.pack()
        bold = tkfont.Font(family=FONT_FAMILY, size=FONT_SIZE, weight="bold")
        for k in ("frame", "title", "header", "text", "dim", "alert", "ok", "warn"):
            self.txt.tag_configure(k, foreground=COLORS[k])
        self.txt.tag_configure("title", font=bold)
        self.txt.tag_configure("alert", font=bold)
        self.txt.tag_configure("warp", background=COLORS["warp_bg"], foreground="#ffffff", font=bold)
        self.txt.tag_configure("flash", background=COLORS["flash_bg"], foreground="#ffffff", font=bold)
        for s, c in SOURCE_COLORS.items():
            self.txt.tag_configure("src_" + s, foreground=c)
        for s, c in PLAYER_COLORS.items():
            self.txt.tag_configure("pl_" + s.replace(" ", "_"), foreground=c)
        self.txt.tag_configure("pl_Bugged", font=bold)

        if not args.windowed:
            self.root.overrideredirect(True)
            self.root.attributes("-topmost", True)
        self.root.update_idletasks()
        self._place()
        if not args.windowed:
            self._make_click_through()
        self.root.attributes("-alpha", OVERLAY_ALPHA)
        self.root.after(REFRESH_MS, self.tick)

    # -- window plumbing ------------------------------------------------------
    def _place(self):
        w, h = self.root.winfo_reqwidth(), self.root.winfo_reqheight()
        sw, sh = self.root.winfo_screenwidth(), self.root.winfo_screenheight()
        c = self.args.corner
        x = MARGIN_PX if c[1] == "l" else sw - w - MARGIN_PX
        y = MARGIN_PX if c[0] == "t" else sh - h - MARGIN_PX
        self.root.geometry("+%d+%d" % (x, y))

    def _make_click_through(self):
        # Never take focus: focusing another window makes FDNY pause its
        # mission clock (GameClock::Pause on WM_ACTIVATEAPP).
        u32 = ctypes.windll.user32
        GWL_EXSTYLE = -20
        WS_EX_LAYERED, WS_EX_TRANSPARENT = 0x00080000, 0x00000020
        WS_EX_TOOLWINDOW, WS_EX_NOACTIVATE = 0x00000080, 0x08000000
        hwnd = u32.GetParent(self.root.winfo_id()) or self.root.winfo_id()
        ex = u32.GetWindowLongW(hwnd, GWL_EXSTYLE)
        u32.SetWindowLongW(hwnd, GWL_EXSTYLE,
                           ex | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)
        self.hwnd = hwnd

    def _keep_on_top(self):
        if self.args.windowed or self.hidden:
            return
        HWND_TOPMOST = -1
        SWP_NOSIZE, SWP_NOMOVE, SWP_NOACTIVATE = 0x1, 0x2, 0x10
        ctypes.windll.user32.SetWindowPos(self.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                                          SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE)

    def _pressed(self, vk):
        down = bool(ctypes.windll.user32.GetAsyncKeyState(vk) & 0x8000)
        was = self.keys.get(vk, False)
        self.keys[vk] = down
        return down and not was

    def _hotkeys(self):
        if self._pressed(VK_F9):
            self.hidden = not self.hidden
            (self.root.withdraw if self.hidden else self.root.deiconify)()
        if self._pressed(VK_F10):
            self.frozen = not self.frozen
        if self._pressed(VK_F12):
            self.m.clear()
            self.log.note("history cleared (F12)")
            self.last_text = None

    # -- main loop ------------------------------------------------------------
    def tick(self):
        try:
            self.src.poll()               # sources log their own events
        except Exception as e:          # game closed, read failure, ...
            self.log.note("source error: %r" % (e,))
            print("source error:", e, file=sys.stderr)
            self.root.destroy()
            return
        self._hotkeys()
        self._keep_on_top()
        if not self.frozen and not self.hidden:
            self.render()
        self.root.after(REFRESH_MS, self.tick)

    # -- drawing --------------------------------------------------------------
    def render(self):
        lines = self.build()
        key = repr(lines)
        if key == self.last_text:
            return
        self.last_text = key
        t = self.txt
        t.configure(state="normal")
        t.delete("1.0", "end")
        for segs in lines:
            for text, tag in segs:
                t.insert("end", text, tag)
            t.insert("end", "\n")
        t.configure(state="disabled", height=len(lines))

    def build(self):
        """Returns a list of lines; each line is a list of (text, tag)."""
        m = self.m
        L = []

        def top(title, tag="frame", extra=""):
            dash = "-" * (W_INNER + 4 - 6 - len(title) - len(extra))
            seg = [("+-- ", tag), (title, "title")]
            if extra:
                seg.append((extra, "alert" if tag == "flash" else "header"))
            seg += [(" " + dash + "+", tag)]
            L.append(seg)

        def body(segs, bg=None):
            n = sum(len(s) for s, _ in segs)
            pad = " " * max(0, W_INNER - n)
            row = [("| ", "frame")]
            row += [(s, (tg + " " + bg) if bg else tg) for s, tg in segs]
            row += [(pad, bg or "text"), (" |", "frame")]
            L.append([(s, tuple(tg.split())) for s, tg in row])

        def bottom():
            L.append([("+" + "-" * (W_INNER + 2) + "+", "frame")])

        def src_tag(s):
            return "src_" + (s if s in SOURCE_COLORS else "Other")

        def ev_row(ev, dim=False):
            surf = surface_name(ev.ny)
            tick_s = "%6d  " % ev.tick
            return [(tick_s, "dim" if dim else "text"),
                    ("%-*s" % (SRC_W, ev.source), "dim" if dim else src_tag(ev.source)),
                    (fmt_coords(ev.pos), "dim" if dim else "text"),
                    ("  " + surf, "dim" if dim else ("ok" if surf == "Floor" else "alert"))]

        # WARP: who you are, what the next frame will do, where it would send you
        w, p = m.window, m.player
        extra = ("  (map %s)" % w["map"]) if w and w.get("map") else ""
        extra += "  tick %d" % m.tick
        top("WARP", extra=extra + ("  FROZEN" if self.frozen else ""))
        body([(XYZ_HEADER, "header")])
        if p:
            stance = STATE_NAMES.get(p["state"], "State %d" % p["state"])[:8]
            ground = "Ground" if p["ground"] else "In air"

            def pl_tag(s):
                return ("pl_" + s.replace(" ", "_")) if s in PLAYER_COLORS else "dim"
            stance_tag = pl_tag(stance)
            if crouchbug(p):
                # stance says not-crouched but the box is the crouched one
                stance, stance_tag = ("Bugged" if p["state"] == 3 else stance), "pl_Bugged"
            body([("%-8s" % stance, stance_tag), ("  -  ", "frame"),
                  ("%-6s" % ground, pl_tag(ground)), (fmt_coords(p["pos"]), "text")])
        else:
            body([("(no player -- menu or loading)", "dim")])
        if w is None or w["level"] == "none":
            body([((w["text"] if w else ""), "dim")])
        else:
            tag = {"clear": "ok", "solid": "warn", "armed": "alert", "warp": "flash"}[w["level"]]
            body([(w["text"], tag)])
        # The target only means something while inside solid: in bounds your
        # own sweeps rewrite the slot every frame, so it would just be you.
        if w and w.get("inside") and m.slot:
            spos, snorm = m.slot
            wr = m.slot_writer if (m.slot_writer and m.slot_writer.pos == spos) else m.find_writer(spos)
            if wr is None:
                # no logged writer has these coords: re-rotated in place by an
                # entity model, or written before the tool attached
                wr = Ev(0, m.tick, "hit", "Unknown", pos=spos, normal=snorm)
            row = ev_row(wr)
            row[0] = ("Target  ", "header")
            body(row)
        else:
            body([("Target  -", "dim")])
        bottom()

        # LAST WARP -----------------------------------------------------------
        lw = m.last_warp
        if lw is None:
            top("LAST WARP")
            body([(TABLE_HEADER, "header")])
            body([("(none yet)", "dim")])
        else:
            flash = (time.monotonic() - lw.wall) < WARP_FLASH_SEC
            top("LAST WARP", tag="flash" if flash else "frame",
                extra="  -  %d ticks ago" % (m.tick - lw.tick))
            body([(TABLE_HEADER, "header")])
            body(ev_row(lw), bg="flash" if flash else "warp")
            if lw.frm is not None:
                body([("%6s" % "from" + " " * (COORD_COL - 6) + fmt_coords(lw.frm), "text")])
        bottom()

        # HISTORY -------------------------------------------------------------
        top("HISTORY", extra="  (warp-capable hits, newest first)")
        body([(TABLE_HEADER, "header")])
        shown = 0
        for ev in reversed(list(m.history)[-HISTORY_ROWS:]):
            shown += 1
            dim = (m.tick - ev.last_tick) > AGE_DIM_TICKS
            if ev.kind == "warp":
                who = ev.src_ev.source if ev.src_ev else "unknown"
                body([("%6d  " % ev.tick, "text"),
                      (">>>>> WARP: player moved to %s hit <<<<<" % who, "warp")], bg="warp")
            else:
                row = ev_row(ev, dim)
                if ev.count > 1:          # same writer still in the slot on later frames
                    row.append((" x%d" % ev.count, "dim" if dim else "header"))
                body(row)
        for _ in range(HISTORY_ROWS - shown):
            body([("", "text")])
        bottom()
        return L

    def run(self):
        self.root.mainloop()


# =============================================================================
def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--demo", action="store_true", help="synthetic events, no game needed")
    ap.add_argument("--windowed", action="store_true", help="normal window (second monitor / windowed game)")
    ap.add_argument("--corner", default="tr", choices=("tl", "tr", "bl", "br"))
    ap.add_argument("--maps", help="folder with the game's .bsp maps for the WARP prediction "
                                   "(default: <game folder>\\Data\\Maps)")
    args = ap.parse_args()

    model = Model()
    log = TextLog()
    if args.demo:
        source = DemoSource(model, log)
        log.note("DEMO MODE -- synthetic events")
    else:
        source = GameSource(model, log, args.maps)

    # Closing the console window (or logoff/shutdown) kills Python without
    # running `finally`, which would leave the hooks in the game.  Ctrl+C is
    # left to Python (KeyboardInterrupt reaches the `finally` below).
    @ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_ulong)
    def on_console_event(kind):
        if kind in (1, 2, 5, 6):          # CTRL_BREAK, CTRL_CLOSE, LOGOFF, SHUTDOWN
            source.close()
        return 0                          # let the default handler continue
    ctypes.windll.kernel32.SetConsoleCtrlHandler(on_console_event, 1)

    try:
        Overlay(model, source, log, args).run()
    finally:
        source.close()
        s = model.pred_stats
        log.note("warp window: %d predictions, %d said WARP NEXT FRAME; %d warps, %d of them "
                 "predicted" % (s["polls"], s["warp_polls"], s["warps"], s["warps_predicted"]))
        log.close()


if __name__ == "__main__":
    main()
