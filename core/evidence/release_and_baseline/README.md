# A Release smoke test, and baseline_route asserts its comparison (suite review item 3)

## Release in the suite: `tests/core_release.yaml`

Every other test runs the Debug build, but players install Release. Tests can now name the
build they need (`core: Release`; `--core none` still removes the core for baseline runs).
`core_release` checks:

- the Release core loads (`(Release) exe=`);
- version retail-1.1 is selected, and every signature is OK;
- the modules start once;
- the core is ready;
- the intro is skipped through the player setting;
- the game reaches Training (map name in memory, non-black screen);
- no crash, and 0 ERROR lines.

2026-10-25: **PASS 15/15.**

Two failed attempts before that, neither a core problem:

- **Intro movies.** The first attempt didn't reach the level: the map name was unresolved
  and the screen black. The harness's intro-skip switch (`FDNYCORE_TEST_SKIP_INTRO`) is
  Debug-only, so the movies played and the setup's clicks missed. The test now uses
  `fd_skip_intro "1"` in `FDNYCore.cfg`, which is how a Release player skips them.
- **Disconnected desktop.** The second attempt failed "Failed to initialize main window":
  session 2 was disconnected (`query user`: Disc). `tscon 2 /dest:console` fixed it, as
  `core/CLAUDE.md` describes.

## `baseline_route`: "behaves the same", asserted

Before, the suite ran only the with-core route and checked that it finished; the comparison
with no-core runs (`harness/compare_runs.py`) was done by hand.

Now the test runs the Release core, then twice more with the core removed, back to back.
`expect: baseline_equivalent` applies `compare_runs.compare`, whose 7 checks must all pass.

2026-10-25, on the VM console: **PASS 7/7.**

| Check | Result |
|---|---|
| 1 no crash | none in all 3 runs |
| 2 same level and state | training, state 1 at start and every lap |
| 3 module bases | exe 0x400000 in all 3. Genesis 0x10000000, 0x10000000, 0x2390000 (it relocated once; reported, not required equal) |
| 4 autosplitter chains | 12 resolve, none moved against a stable baseline |
| 5 frame rate | median 55.6 fps with the core vs 58.8 without (-5.5%, limit 10%) |
| 6 lap positions | 10 laps, 1.0 to 3.6 u from the baselines (tolerance 32 u) |
| 7 log errors | 0 |

The test now takes about 8 minutes, against about 2.5 before: three routes instead of one.

Files: `baseline_route_output.txt`, `baseline_route_result.json`, `core_release_result.json`.
