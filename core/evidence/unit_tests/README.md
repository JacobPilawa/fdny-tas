# Unit tests without the game (D47)

`core/unit/run.ps1`, 2026-10-25, on the VM with the game's maps (`FDNY_MAPS`):

| Part | Tests | Result |
|---|---|---|
| C++ `fdcore_unit.exe` (`addr`) | 7 tests, 34 checks | PASS |
| `test_glitch_parity.py` | 2 tests, 5 streams: the suite's seed 1 (800 frames, 3 sessions) plus seeds 2, 3, 7, 42 | PASS |
| `test_arch_check.py` | 12 tests: the clean source, and one seeded violation per rule (R1, R2, R3 x2, R4 x3, R5, R8, R9 x2) | PASS |
| `test_docgen.py` | 10 tests: release-notes extraction (sections, subheadings, prefixes, CRLF, missing), CHANGES.md has the current version, CLI exit codes, the sheet | PASS |

Run time: about 53 s with the maps, almost all of it the Python reference replay; the C++
part takes under a second. Without the maps (the CI case) the parity streams take 3 s.
`glitch_parity_fuzz` in the game took 60 s for one stream.

## The tests fail when the logic breaks

Each was checked against a deliberate break, then restored (`git status` clean for those files):

- `addr.cpp`, Genesis relocation tolerance switched off: `addr_all_verify` and
  `addr_bad_signature` FAIL (first wrong entry `glitch.exit_box`, a Genesis code entry);
  `addr_genesis_at_preferred_base` still passes, correctly, because nothing is relocated
  at 0x10000000.
- `glitch_analysis.cpp`, one log label changed (`"capable"` to `"capablE"`): the parity
  tests FAIL (5 failures).

## The game path after the refactor

The replay loop moved from `glitch.cpp` into `glitch_replay.cpp`, which the game's Debug pipe
and the unit test both call. Run live after the move:

- `glitch_parity_fuzz` PASS: 2400 box frames, 2301 reference lines, no mismatches;
- `glitch_parity_log` PASS: 4336 box frames, no mismatches.

Release is unaffected: the replay file compiles to nothing without `FDCORE_DEBUG` / `FDCORE_UNIT`.

## Found while doing it

- With no maps, the core always indexes the folder it's given and logs "0 maps indexed".
  The Python reference, given `maps=None`, doesn't predict at all, so the two only match
  when both get the same (empty) folder. That's what the test does, and what happens live,
  where both use `<game>\Data\Maps`.
- `harness/glitch_replay.py` left the record file open (ResourceWarning); it's now closed.
