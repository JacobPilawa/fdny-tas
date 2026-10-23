# Key sheet generated from the source (D41, R9), 2026-10-23

Owner ask: one sheet in `dist/` with every default key, feature and variable (with
values), built each time so it can't go stale.

**Result:** `core/tools/docgen.py` reads the registrations' string literals; `build.ps1`
writes `build/<Config>/README.md` on every build and `-Dist` puts the Release one in
`dist/README.md` with the `.asi`. The generated sheet: `dist/README.md`.

| File | What |
|---|---|
| `FDNYCore_registry.txt` | what the Debug build registered (`fd_dbg_registry`), the run that passed |
| `docs_registry_result.json` | `tests/docs_registry.yaml`: build vs docgen, every kind identical (23 variables, 30 commands incl. Debug ones, 4 aliases, 8 binds, 10 feature keys, 10 modules) |
| `docs_registry_first_run_alttabfix.json` | the first run: FAIL, because with `alttabfix.asi` installed the window module turns itself off and registers nothing. The test now moves alttabfix.asi out for the run, and the comparison skips items of modules the build reports disabled |
| `docgen_error_cases.py` / `.txt` | docgen on a scratch copy of the source with three broken registrations (a help that isn't a literal, a wrong argument count, a `RegisterCvar` of an unknown variable): all three stop it |
| `test_runs.txt` | the tests run for this change |
| `freecam_fail_once.json` / `freecam_rerun_pass.json` | `freecam` failed once (`py` = player y moved 0.9 units during the 4 s pause, all other frozen fields 0), passed on rerun. Not touched by this change (six `DescribeKey` lines in its Init); left as an open item |

arch_check R9 was checked the same way: a feature file without its `// User:` line, and
one calling `GetAsyncKeyState` without `binds::DescribeKey`, are both reported.
