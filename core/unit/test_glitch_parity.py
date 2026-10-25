"""Warp HUD parity without the game (D47): a seeded synthetic .glitchrec stream
(harness/glitch_fuzz.py) through the core's replay driver, built into fdcore_unit.exe,
and through the original Python overlay classes (harness/glitch_replay.py). The
collision log and the per-frame box text must be identical. tests/glitch_parity_fuzz.yaml
ran the same comparison inside the game.

The warp-window paths need the game's maps (<game>/Data/Maps): with FDNY_MAPS set to that
folder (run.ps1 does on the VM) the stream lines the player up on real map planes; without
it (CI) the rest of the analysis is still compared.
"""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
CORE = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(CORE, "harness"))
import glitch_fuzz  # noqa: E402
import glitch_parity  # noqa: E402

EXE = os.environ.get("FDCORE_UNIT_EXE", os.path.join(CORE, "build", "unit", "fdcore_unit.exe"))
MAPS = os.environ.get("FDNY_MAPS") or None


class GlitchParity(unittest.TestCase):
    def replay(self, seed, frames, sessions):
        d = tempfile.mkdtemp(prefix="fdcore_unit_")
        rec, log, box = (os.path.join(d, n) for n in ("fuzz.glitchrec", "FDNYCore.log", "FDNYCore.glitchbox"))
        glitch_fuzz.generate(rec, seed, frames, sessions, MAPS)
        # Both sides index the same folder: the game's maps, or (CI) an empty one. The core
        # always indexes one (<game>\Data\Maps live) and logs how many maps it found.
        maps = MAPS or os.path.join(d, "no-maps")
        os.makedirs(maps, exist_ok=True)
        out = subprocess.run([EXE, "glitch-replay", rec, maps, log, box], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        return rec, log, box, maps

    def check(self, seed, frames, sessions, min_lines, min_box_frames):
        rec, log, box, maps = self.replay(seed, frames, sessions)
        r = glitch_parity.compare(rec, log, maps)
        self.assertEqual(r["mismatches"], [], "collision log differs")
        self.assertGreaterEqual(r["lines"], min_lines)
        self.assertEqual(r["sessions_ref"], sessions)
        b = glitch_parity.compare_boxes(rec, box, maps)
        self.assertEqual(b["mismatches"], [], "box text differs")
        self.assertGreaterEqual(b["frames"], min_box_frames)

    def test_fuzz_suite_stream(self):
        # The stream tests/glitch_parity_fuzz.yaml replays (seed 1, 800 frames, 3 sessions),
        # with its thresholds.
        self.check(1, 800, 3, 1000, 2400)

    def test_more_seeds(self):
        # Cheap here, impossible live: more streams, more sessions.
        for seed in (2, 3, 7, 42):
            with self.subTest(seed=seed):
                self.check(seed, 400, 3, 1, 1200)


if __name__ == "__main__":
    unittest.main()
