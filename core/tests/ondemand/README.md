# On-demand tests

These are real tests, but they're kept out of `--suite` because they don't test FDNYCore's
current code on every run. Run them all with `python -I harness/run_test.py --ondemand`,
or one by path. Each feature's `// Tests:` line still names its own.

| Test | What it proves | Run it when |
|---|---|---|
| `speedrun_reference_deterministic` | The original `FDNYDeterministic.asi` pins every victim to spot 0, the script `speedrun_static_spawns` reuses. | The game or that plugin changes |
| `window_parity_reference` | The original `alttabfix.asi` on the alt-tab script `window_alttab` reuses. | The game, Windows, or that plugin changes |
| `baseline_route` | The game behaves the same with the Release core as without it. It runs the route three times (core, then twice without) and applies `compare_runs.py`'s 7 checks. About 8 min. | Before every release |
The first two run the old third-party plugins and check that FDNYCore steps aside for them.
Their numbers are the fixed expectations other suite tests use. Moved here by the suite review (2026-10-24; the
owner approved, "go 1-5"); none were deleted or changed.
