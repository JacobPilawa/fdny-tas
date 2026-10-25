"""VM-only reference for hud_glitch parity (backlog item 11b/11c).

Replays an FDNYCore.glitchrec stream (raw collision-hook records + one
snapshot block per frame, written by Debug FDNYCore) through the ORIGINAL
tools/collision_overlay classes: ReplaySource subclasses GameSource and only
replaces where the data comes from (_read_live -> the snapshot block,
_records -> that frame's records); GameSource.poll, _classify, FrameAnalyzer,
Model and TextLog run unmodified. Timestamps are stubbed.

  python -I glitch_replay.py <FDNYCore.glitchrec> [--predict MAPSDIR]  > reference.log
"""
import io
import os
import struct
import sys
import types

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "collision_overlay"))
import fdny_collision_overlay as ov  # noqa: E402

SNAP = 0xF0


class _FixedNow:
    def isoformat(self, *a, **k):
        return "TIMESTAMP"

    def strftime(self, fmt):
        return "HH:MM:SS"


ov.datetime = types.SimpleNamespace(datetime=types.SimpleNamespace(now=lambda: _FixedNow()))


SESSION = 0xF1
MAPCHECK = 0xF2      # MapFinder.update ran this frame; b[1] = readable, b[2] bytes of planes follow
SESSION_END = 0xF4   # hooks removed (the old tool's exit notes)
CLEAR = 0xF5         # F12 after that frame's poll (Model.clear + note)


class Frame:
    def __init__(self, snap):
        self.snap, self.records = snap, []
        self.map_check, self.planes = False, None
        self.clear = False


def sessions(path):
    """[(frames, ended)] per hook install, in stream order."""
    with open(path, "rb") as f:
        data = f.read()
    out = [[[], False]]
    off = 0
    while off + 128 <= len(data):
        blk = data[off:off + 128]
        off += 128
        kind = struct.unpack_from("<I", blk, 0)[0]
        cur = out[-1]
        if kind == SESSION:
            if cur[0] or cur[1]:
                out.append([[], False])
        elif kind == SESSION_END:
            cur[1] = True
        elif kind == CLEAR:
            if cur[0]:
                cur[0][-1].clear = True
        elif kind == MAPCHECK:
            ok, n = struct.unpack_from("<II", blk, 4)
            if cur[0]:
                cur[0][-1].map_check = True
                cur[0][-1].planes = data[off:off + n] if ok and off + n <= len(data) else None
            off += n
        elif kind == SNAP:
            cur[0].append(Frame(blk))
        elif cur[0]:
            cur[0][-1].records.append(struct.unpack(ov.REC_FMT, blk))
    return [s for s in out if s[0] or s[1]]


class ReplaySource(ov.GameSource):
    def __init__(self, model, log, predict_maps=None):  # no live process
        self.m, self.log = model, log
        self.an = ov.FrameAnalyzer(model, log)
        self.player_ptr = 0
        self.last_player_trace_tick = -10 ** 9
        self.gdelta = 0
        self.cur = None
        self.maps = ov.MapFinder(predict_maps, log) if predict_maps else None

    def _read_live(self):
        m, s = self.m, self.cur
        tick, pl, state, ground, spec = struct.unpack_from("<IIiII", s, 4)
        self.gdelta = struct.unpack_from("<I", s, 25 * 4)[0] - ov.GENESIS_PREF
        self.player_ptr = pl
        if pl:
            pos = struct.unpack_from("<3f", s, 6 * 4)
            box = struct.unpack_from("<6f", s, 9 * 4)
            disp = struct.unpack_from("<3f", s, 15 * 4)
            m.player = {"pos": pos, "state": state, "ground": (ground & 0xFF) != 0,
                        "mins": box[0:3], "maxs": box[3:6], "disp": disp, "spectator": (spec & 0xFF) != 0}
        else:
            m.player = None
        m.tick = tick
        ov.track_crouchbug(m, self.log)
        if self.maps is None:
            m.set_window(None)          # 11b: no prediction (both sides)
        else:
            self._predict()             # 11c
        if m.tick - self.last_player_trace_tick > 10:
            imp = struct.unpack_from("<3f", s, 18 * 4)
            nrm = struct.unpack_from("<3f", s, 21 * 4)
            m.slot = (tuple(round(v, 1) for v in imp), nrm)
            m.slot_writer = None

    def _predict(self):
        """GameSource._predict with its two live reads replaced by the stream:
        the plane array (only on frames where the core ran MapFinder.update --
        the 1 s wall-clock throttle is the core's) and the slot plane's ny
        (the snapshot's). MapFinder / Bsp / describe_window run unmodified."""
        m, p, f = self.m, self.m.player, self.frame
        # MapFinder.update checks only when the throttle allows it: open the
        # throttle on the core's check frames, keep it shut on the others.
        self.maps.last_check = -1e9 if f.map_check else ov.time.monotonic()
        bsp = self.maps.update(lambda n: f.planes)
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
        slot_ny = struct.unpack_from("<3f", self.cur, 21 * 4)[1]
        m.set_window(bsp.predict(p["pos"], p["mins"], p["maxs"], p["disp"], slot_ny))

    def _records(self):
        return self.frame.records


def box_text(m):
    """Overlay.build() (the original method, on a stand-in with just the
    attributes it reads) as plain text lines: tags/colours dropped."""
    stub = types.SimpleNamespace(m=m, frozen=False)
    return ["".join(s for s, _ in segs) for segs in ov.Overlay.build(stub)]


def replay_session(frames_, ended=False, predict_maps=None, boxes=None):
    out = io.StringIO()
    m = ov.Model()
    log = ov.TextLog(out)
    src = ReplaySource(m, log, predict_maps)
    for fr in frames_:
        src.frame, src.cur = fr, fr.snap
        src.poll()                      # Overlay.tick: poll, hotkeys, render
        if fr.clear:
            m.clear()
            log.note("history cleared (F12)")
        if boxes is not None:
            boxes.append("== %d" % m.tick)
            boxes.extend(box_text(m))
    if ended:   # main()'s finally: source.close() + the prediction summary
        log.note("hooks removed, original bytes restored")
        s = m.pred_stats
        log.note("warp window: %d predictions, %d said WARP NEXT FRAME; %d warps, %d of them "
                 "predicted" % (s["polls"], s["warp_polls"], s["warps"], s["warps_predicted"]))
    return out.getvalue(), m


def replay(path, predict_maps=None, boxes=None):
    """One TextLog text per hook-install session (boxes: list to receive the
    per-frame build() text, "## session" between sessions)."""
    out = []
    for fr, ended in sessions(path):
        if boxes is not None:
            boxes.append("## session")
        out.append(replay_session(fr, ended, predict_maps, boxes))
    return out


if __name__ == "__main__":
    maps = sys.argv[sys.argv.index("--predict") + 1] if "--predict" in sys.argv else None
    for i, (text, model) in enumerate(replay(sys.argv[1], maps)):
        sys.stdout.write(text)
        sys.stderr.write(f"# session {i}: seq={model.seq} history={len(model.history)} "
                         f"warps={model.pred_stats['warps']}\n")
