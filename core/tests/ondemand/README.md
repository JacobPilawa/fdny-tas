# On-demand tests

These are real tests, but they're kept out of `--suite` because they're too slow to run every
time. Run them all with `python -I harness/run_test.py --ondemand`, or one by path.

| Test | What it proves | Run it when |
|---|---|---|
| `baseline_route` | The game behaves the same with the Release core as without it. It runs the route three times (core, then twice without) and applies `compare_runs.py`'s 7 checks. About 8 min. | Before every release |

The reference tests that ran the old third-party plugins (FOV fix, alttabfix, FDNYDeterministic)
were removed with the core's deferral to them (D55); their measured values are in
`core/evidence/foundation`.
