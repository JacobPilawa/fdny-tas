"""VM-only: synthetic FDNYCore.glitchrec streams for hud_glitch parity (item 11b).

A live session rarely produces warp-capable hits or warps, so the live parity
test mostly checks the log header. This writes a seeded, deterministic stream
in the exact Debug-dump layout (0xF1 session marker, 0xF0 snapshot block per
frame, then that frame's 32-dword records) that forces every path of the
analysis: capable hits (repeats included), warps with known and unknown
writers, every iteration outcome, re-sweeps, engine rays, Genesis callers
(relocated and not), foreign commits, crouchbug edges, player-less frames,
tick gaps, and float values on the .x5 rounding edges.

The core replays it offline (Debug pipe "glitch_replay <path>"); the original
overlay classes replay it via glitch_replay.py; glitch_parity.py compares.

  python -I glitch_fuzz.py <out.glitchrec> [--seed N] [--frames N] [--sessions N]
"""
import os
import random
import struct
import sys

REC_FMT = "<11I3f3ff3fI3f7I"
SNAP, SESSION, MAPCHECK, SESSION_END = 0xF0, 0xF1, 0xF2, 0xF4
KIND_TRACE, KIND_COMMIT, KIND_ENGINE_RAY = 1, 2, 3
PLAYER = 0x0A01F000

RET_TRACETOFLOOR, RET_GROUNDHEIGHT, RET_SEGMENTLOS = 0x00492DA6, 0x0048FFC6, 0x0048FF27
RET_LOS_ACTIVEWORLD, RET_SWEEPBOX, RET_FLOORME, RET_HOSEGFX = 0x0049014D, 0x004900DE, 0x004486F3, 0x0046CBD8
ROLE_RET = {"first": 0x0043F732, "resweep": 0x0043F7E0, "stepup": 0x0043F8BF,
            "forward": 0x0043F920, "stepdown": 0x0043F9B1}
OTHER_OUTERS = [0x00447CCF, 0x0045ACA0, 0x00460DD6, 0x0049DAA7, 0x0049E44C, 0x0045ADE3, 0x0045AE11,
                0x0046781C, 0x0043FD9C, 0x004484CD, 0x00482779, 0x00449A29, 0x004492AB,
                0x00455300, 0x0045C900, 0x0048FE00, 0x00401000]          # last ones: nearest-function names
GENESIS_RETS = [0x10012253, 0x10002FD5, 0x10039167, 0x10020000]


class Gen:
    def __init__(self, seed):
        self.r = random.Random(seed)
        self.pool = [self.vec(2000) for _ in range(24)]       # shared coords: writer -> reuse attribution

    def f(self, span):
        r = self.r
        v = r.uniform(-span, span)
        if r.random() < 0.3:            # land on the one-decimal rounding edge
            v = round(v, 1) + r.choice((0.05, -0.05, 0.049999, 0.050001))
        return struct.unpack("<f", struct.pack("<f", v))[0]

    def vec(self, span):
        return tuple(self.f(span) for _ in range(3))

    def normal(self, kind=None):
        r = self.r
        kind = kind or r.choice(("floor", "wall", "slope", "ceiling", "edge"))
        ny = {"floor": r.uniform(0.91, 1.0), "wall": r.uniform(-0.09, 0.09), "slope": r.uniform(0.1, 0.9),
              "ceiling": r.uniform(-1.0, -0.11), "edge": r.choice((0.9, 0.1, -0.1, 0.0))}[kind]
        nx = r.uniform(-1, 1) * (1 - abs(ny))
        return (nx, ny, r.uniform(-1, 1) * (1 - abs(ny)))

    def rec(self, kind, tick, ret0, outer=None, via=None, result=1, fresh=1, imp=None, nrm=None, pre=None,
            this=0, gpl=None, dist=None):
        r = self.r
        d = [kind, tick, ret0] + [r.getrandbits(32) for _ in range(6)] + [result, fresh]
        if outer is not None:
            slot = {RET_TRACETOFLOOR: 7, RET_GROUNDHEIGHT: 4, RET_SWEEPBOX: 3, RET_FLOORME: 8}.get(ret0)
            if ret0 == RET_SEGMENTLOS:
                if via:
                    d[3], d[5] = RET_LOS_ACTIVEWORLD, outer
                else:
                    d[3] = outer
            elif slot is not None:
                d[slot] = outer
        imp = imp or self.vec(2000)
        nrm = nrm or self.normal()
        pre = pre or self.r.choice(self.pool)
        gpl = gpl or nrm
        dist = self.f(500) if dist is None else dist
        return struct.pack(REC_FMT, *d, *imp, *nrm, dist, *pre, this, *gpl,
                           *[r.getrandbits(32) for _ in range(7)])

    def player_trace(self, tick, role, kind, pre=None, imp=None, nrm=None):
        outer = ROLE_RET[role]
        ret0, extra = self.r.choice(((RET_SWEEPBOX, {}), (RET_TRACETOFLOOR, {}), (RET_SEGMENTLOS, {"via": True}),
                                     (RET_GROUNDHEIGHT, {})))
        result, fresh = {"miss": (0, 0), "reused": (1, 0), "hit": (1, 1)}[kind]
        return self.rec(KIND_TRACE, tick, ret0, outer, result=result, fresh=fresh, pre=pre, imp=imp,
                        nrm=nrm, **extra)

    def writer(self, tick, pos, nrm, source="other"):
        r = self.r
        if source == "engine":
            return self.rec(KIND_ENGINE_RAY, tick, r.getrandbits(32), imp=pos, nrm=nrm)
        ret0 = r.choice((RET_TRACETOFLOOR, RET_GROUNDHEIGHT, RET_SEGMENTLOS, RET_SWEEPBOX, RET_FLOORME,
                         RET_HOSEGFX, 0x0045AB80, 0x10039167, 0x10012253))
        return self.rec(KIND_TRACE, tick, ret0, r.choice(OTHER_OUTERS), via=r.random() < 0.5, imp=pos, nrm=nrm)

    def noise(self, tick, gbase):
        r, out = self.r, []
        for _ in range(r.randint(0, 6)):
            c = r.random()
            if c < 0.2:
                out.append(self.rec(KIND_ENGINE_RAY, tick, 0, imp=r.choice(self.pool)))
            elif c < 0.35:     # Genesis caller (relocated or not)
                out.append(self.rec(KIND_TRACE, tick, r.choice(GENESIS_RETS) - 0x10000000 + gbase,
                                    result=r.randint(0, 1), fresh=r.randint(0, 1)))
            elif c < 0.45:     # a commit by some other entity: ignored
                out.append(self.rec(KIND_COMMIT, tick, 0, this=PLAYER + 0x100))
            else:
                out.append(self.writer(tick, r.choice(self.pool) if r.random() < 0.5 else self.vec(2000),
                                       self.normal()))
        return out

    def player_frame(self, tick, player):
        """One frame of the player's sweep sequence, by scenario."""
        r, out = self.r, []
        scen = r.choice(("clear", "floor", "wall", "ledge", "step", "armed", "warp", "warp", "capable",
                         "solid", "multi"))
        q = r.choice(self.pool)
        if scen in ("capable", "warp", "armed", "solid") and r.random() < 0.85:
            out.append(self.writer(tick, q, self.normal("floor" if scen == "solid" else
                                                        r.choice(("wall", "slope", "ceiling", "edge"))),
                                   r.choice(("other", "other", "engine"))))
        if scen == "clear":
            out.append(self.player_trace(tick, "first", "miss", pre=q))
        elif scen in ("floor", "solid"):
            out.append(self.player_trace(tick, "first", "reused" if scen == "solid" else "hit", pre=q))
        elif scen in ("wall", "ledge", "step", "armed", "capable"):
            first = "reused" if scen == "armed" else "hit"
            out.append(self.player_trace(tick, "first", first, pre=q))
            out.append(self.player_trace(tick, "stepup", "hit" if scen == "wall" else "miss"))
            if scen != "wall":
                out.append(self.player_trace(tick, "forward", "hit" if r.random() < 0.3 else "miss"))
                sd = "miss" if scen == "ledge" else "hit"
                out.append(self.player_trace(tick, "stepdown", sd))
                if sd == "hit":
                    out.append(self.rec(KIND_COMMIT, tick, 0, this=player, imp=self.vec(2000),
                                        pre=self.vec(2000)))
        elif scen == "warp":
            out.append(self.player_trace(tick, "first", r.choice(("reused", "hit")), pre=q))
            out.append(self.player_trace(tick, "stepup", "miss"))
            out.append(self.player_trace(tick, "forward", "miss"))
            sdpre = q if r.random() < 0.8 else self.vec(2000)          # else: writer unknown
            out.append(self.player_trace(tick, "stepdown", "reused", pre=sdpre))
            out.append(self.rec(KIND_COMMIT, tick, 0, this=player, imp=sdpre, pre=self.vec(2000)))
        else:   # multi: several sweep+Think iterations, possibly a latched re-sweep
            out.append(self.player_trace(tick, "first", "hit", pre=q))
            for _ in range(r.randint(1, 4)):
                out.append(self.player_trace(tick, "stepup", r.choice(("miss", "hit"))))
                out.append(self.player_trace(tick, "forward", r.choice(("miss", "hit"))))
                out.append(self.player_trace(tick, "stepdown", r.choice(("miss", "hit", "reused")),
                                             pre=r.choice(self.pool)))
                if r.random() < 0.5:
                    out.append(self.rec(KIND_COMMIT, tick, 0, this=player, imp=self.vec(2000)))
                out.append(self.player_trace(tick, "resweep", r.choice(("miss", "hit", "reused")),
                                             pre=r.choice(self.pool)))
        return out

    def snapshot(self, tick, player, state, maxs_y, gbase, pos=None, box=None, disp=None, slot_ny=None):
        r = self.r
        d = [SNAP, tick, player, state & 0xFFFFFFFF, r.choice((0, 1, 0x100)), 1 if r.random() < 0.03 else 0]
        mins, maxs = box or ((self.f(20), self.f(20), self.f(20)), (self.f(20), maxs_y, self.f(20)))
        nrm = self.normal() if slot_ny is None else (0.0, slot_ny, 0.0)
        blk = struct.pack("<6I", *d) + struct.pack("<3f3f3f3f3f4f", *(pos or self.vec(3000)), *mins, *maxs,
                                                   *(disp or self.vec(30)), *self.vec(2000), *nrm,
                                                   self.f(500))
        blk += struct.pack("<I", gbase)
        return blk + b"\0" * (128 - len(blk))


class Maps:
    """Real map data for the warp-window paths: each .bsp's plane-array prefix
    (what MapFinder matches) and boxes to place the player in (the world
    model's bounds, and solid leaves' bounds = inside-solid positions)."""

    def __init__(self, maps_dir, r, limit=4):
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools",
                                        "collision_overlay"))
        import fdny_collision_overlay as ov
        self.r, self.maps = r, []
        names = sorted(n for n in os.listdir(maps_dir) if n.lower().endswith(".bsp"))
        for name in r.sample(names, min(limit, len(names))):
            with open(os.path.join(maps_dir, name), "rb") as f:
                ch = ov.gbsp_chunks(f)
                planes = ov.read_chunk(f, ch, ov.GBSP_PLANES, ov.MAP_SIG_BYTES)[0]
                d, sz, n = ov.read_chunk(f, ch, ov.GBSP_MODELS)
                m0 = struct.unpack_from("<2i3f3f", d, 0)
                d, sz, n = ov.read_chunk(f, ch, ov.GBSP_LEAFS)
                solid = []
                for li in range(n):
                    leaf = struct.unpack_from("<i3f3f8i", d, li * sz)
                    if leaf[0] & ov.PLAYER_MASK and leaf[14]:
                        solid.append((leaf[1:4], leaf[4:7]))
            planes += bytes(r.getrandbits(8) for _ in range(ov.MAP_SIG_BYTES - len(planes)))
            self.maps.append({"name": name, "planes": planes, "bounds": (m0[2:5], m0[5:8]), "solid": solid,
                              "path": os.path.join(maps_dir, name), "spots": None})
        self.ov = ov

    def spots(self, m, budget=4000):
        """Inputs (pos, box, disp, slot_ny) whose window is one of the rare
        texts: inside-solid points nudged up under an underside until lined
        up. The reference Bsp is used only to CHOOSE inputs here."""
        if m["spots"] is not None:
            return m["spots"]
        r, bsp, found = self.r, self.ov.Bsp(m["path"]), {}
        for _ in range(budget):
            if not m["solid"] or len(found) >= 40:
                break
            box = r.choice(BOXES)
            lo, hi = r.choice(m["solid"])
            p = [r.uniform(min(a, b), max(a, b)) for a, b in zip(lo, hi)]
            disp = r.choice(((0.0, 0.0, 0.0), (r.uniform(-8, 8), r.uniform(-3, 0), r.uniform(-8, 8)),
                             (r.uniform(-1e-3, 1e-3), 0.0, 0.0)))
            ny = r.choice((1.0, 0.5, 0.0))
            disp = tuple(self.ov.f32(v) for v in disp)       # as the stream will carry them
            for _ in range(3):
                p = [self.ov.f32(v) for v in p]
                w = bsp.predict(p, box[0], box[1], disp, ny)
                key = w["text"].split(" u ")[0] if "too high" not in w["text"] else "too high"
                if key != "too high" and key not in found:
                    found[key] = (tuple(p), box, disp, ny)
                if w.get("gap") is None or w["gap"] <= w["win"]:
                    break
                # down toward the underside (gap shrinks by -ny per unit; ~1 for a ceiling)
                p[1] -= w["gap"] - max(w["win"], 0.0) * r.random() + r.choice((0.0, 0.01, -0.01))
        m["spots"] = list(found.values())
        return m["spots"]

    def position(self, m, prev):
        r = self.r
        c = r.random()
        if prev is not None and c < 0.3:
            lo, hi = [v - 24 for v in prev], [v + 24 for v in prev]
        elif c < 0.7 and m["solid"]:
            lo, hi = r.choice(m["solid"])
        else:
            lo, hi = m["bounds"]
        return tuple(r.uniform(min(a, b), max(a, b)) for a, b in zip(lo, hi))


BOXES = (((-16.0, 0.0, -16.0), (16.0, 85.0, 16.0)), ((-16.0, 0.0, -16.0), (16.0, 25.0, 16.0)),
         ((-30.0, -30.0, -30.0), (30.0, 85.0, 30.0)))


def generate(path, seed=1, frames=600, sessions=2, maps_dir=None):
    g = Gen(seed)
    r = g.r
    maps = Maps(maps_dir, r) if maps_dir and os.path.isdir(maps_dir) else None
    with open(path, "wb") as f:
        tick = 1000
        for s in range(sessions):
            f.write(struct.pack("<I", SESSION) + b"\0" * 124)
            gbase = 0x10000000 if s % 2 == 0 else 0x02390000
            state, maxs_y = 3, 85.0
            cur = r.choice(maps.maps) if maps else None
            pos, box = None, r.choice(BOXES)
            for fi in range(frames):
                tick += 1 if r.random() < 0.9 else r.randint(2, 30)
                if r.random() < 0.05:
                    state = r.choice((3, 4, 5, 6, 7, 2, -1))
                if r.random() < 0.05:
                    maxs_y = r.choice((85.0, 25.0, 54.9, 55.0, 60.0))
                player = 0 if r.random() < 0.02 else PLAYER
                if cur is None:
                    f.write(g.snapshot(tick, player, state, maxs_y, gbase))
                else:
                    if r.random() < 0.02:
                        box = r.choice(BOXES)
                    spots = maps.spots(cur)
                    if spots and r.random() < 0.3:              # a lined-up / rare-window spot
                        pos, sbox, disp, ny = r.choice(spots)
                        f.write(g.snapshot(tick, player, state, maxs_y, gbase, pos, sbox, disp, ny))
                    else:
                        pos = maps.position(cur, pos)
                        disp = (0.0, 0.0, 0.0) if r.random() < 0.15 else tuple(r.uniform(-12, 12) for _ in range(3))
                        f.write(g.snapshot(tick, player, state, maxs_y, gbase, pos, box, disp))
                    if fi == 0 or r.random() < 1 / 60:          # MapFinder.update ran this frame
                        c = r.random()
                        if c < 0.1:
                            planes = None                         # plane array unreadable
                        elif c < 0.2:
                            planes = bytes(r.getrandbits(8) for _ in range(len(cur["planes"])))  # unknown map
                        else:
                            if c > 0.9:
                                cur = r.choice(maps.maps)          # level change
                            planes = cur["planes"]
                        f.write(struct.pack("<3I", MAPCHECK, planes is not None,
                                            len(planes) if planes else 0) + b"\0" * 116)
                        if planes:
                            f.write(planes)
                recs = g.noise(tick, gbase)
                if player and r.random() < 0.9:
                    recs += g.player_frame(tick, player)
                if r.random() < 0.05:            # records stamped with the next tick (frame boundary race)
                    recs.append(g.writer(tick + 1, r.choice(g.pool), g.normal()))
                for x in recs:
                    f.write(x)
            if s % 2 == 0:                                         # hooks removed (fd_hud_glitch 0)
                f.write(struct.pack("<I", SESSION_END) + b"\0" * 124)


if __name__ == "__main__":
    a = sys.argv
    opt = lambda k, d: int(a[a.index(k) + 1]) if k in a else d  # noqa: E731
    generate(a[1], opt("--seed", 1), opt("--frames", 600), opt("--sessions", 2),
             a[a.index("--maps") + 1] if "--maps" in a else None)
