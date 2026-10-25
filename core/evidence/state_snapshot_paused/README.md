# state_snapshot with the simulation paused (suite review item 2)

**Before:** `state_snapshot` compared the core's per-frame snapshot with the harness's
direct memory reads while the world ran. In bank, a fire spreading or a roaming victim
moving between the two reads failed it. The latest occurrence was on 2026-10-24 on `main`:
victim v5 was 1.5 u off on z, at frames 2938 and 3004
(`evidence/release_0.19.0/suite_main_3c5aa54.txt`).

**Now:** each comparison runs with the simulation paused, via the Debug pipe `pause on`
(the pause service, D34). The snapshot is still taken every frame, but nothing moves
between it and the harness's reads. The test compares the same fields at the same four
points: Training idle, Training after walking, bank twice.

**Runs, 2026-10-25, Debug:** 3 of 3 PASS. In each, 24 fields were compared at each Training
point and 28 at each bank point, with no mismatches. Files: `run1_result.json` to `run3_result.json`.
