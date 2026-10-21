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


def sessions(path):
    """[[(snapshot_bytes, [record tuples...]), ...] per hook install] in stream order."""
    data = open(path, "rb").read()
    out = [[]]
    for off in range(0, len(data) - 127, 128):
        blk = data[off:off + 128]
        kind = struct.unpack_from("<I", blk, 0)[0]
        if kind == SESSION:
            if out[-1]:
                out.append([])
        elif kind == SNAP:
            out[-1].append((blk, []))
        elif out[-1]:
            out[-1][-1][1].append(struct.unpack(ov.REC_FMT, blk))
    return [s for s in out if s]


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

    def _records(self):
        return self.cur_records


def replay_session(frames_, predict_maps=None):
    out = io.StringIO()
    m = ov.Model()
    log = ov.TextLog(out)
    src = ReplaySource(m, log, predict_maps)
    for snap, recs in frames_:
        src.cur, src.cur_records = snap, recs
        src.poll()
    return out.getvalue(), m


def replay(path, predict_maps=None):
    """One TextLog text per hook-install session."""
    return [replay_session(s, predict_maps) for s in sessions(path)]


if __name__ == "__main__":
    maps = sys.argv[sys.argv.index("--predict") + 1] if "--predict" in sys.argv else None
    for i, (text, model) in enumerate(replay(sys.argv[1], maps)):
        sys.stdout.write(text)
        sys.stderr.write(f"# session {i}: seq={model.seq} history={len(model.history)} "
                         f"warps={model.pred_stats['warps']}\n")
