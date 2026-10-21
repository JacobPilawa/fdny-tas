"""VM-only: sanity-check an FDNYCore.glitchrec stream with the original
overlay's own record format (tools/collision_overlay REC_FMT)."""
import collections, os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools", "collision_overlay"))
import fdny_collision_overlay as ov  # REC_FMT, R_*, PLAYER_ROLES, OUTER_RETS

data = open(sys.argv[1], "rb").read()
recs = list(struct.iter_unpack(ov.REC_FMT, data))
kinds = collections.Counter(r[ov.R_KIND] for r in recs)
snaps = [r for r in recs if r[0] == 0xF0]
ticks = [r[1] for r in snaps]
print(f"{len(recs)} blocks: kinds {dict(kinds)}; snapshot ticks {ticks[0]}..{ticks[-1]} monotonic={all(b >= a for a, b in zip(ticks, ticks[1:]))}")
roles = collections.Counter()
for r in recs:
    if r[0] == ov.KIND_TRACE:
        roles[ov.PLAYER_ROLES.get(r[ov.R_S30], "-")] += 1   # SWEEPBOX callers use S30 as the outer return
trace_ticks = [r[1] for r in recs if r[0] == ov.KIND_TRACE]
print(f"player sweep roles (via S30): {dict(roles)}")
print(f"trace ticks within snapshot range: {min(trace_ticks)}..{max(trace_ticks)}")
