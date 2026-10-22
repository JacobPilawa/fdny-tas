# Suite 0.17.0 (2026-10-22, Debug, tag s0170)

28 of 31 PASS, including `triggers` and `text_alpha` (the `AlphaSurface` re-attach change, D30).
The run folders are `core/runs/*-s0170`.

| Test | Result | Cause |
|---|---|---|
| glitch_box_cost | FAIL | `glitch.tick` +209 / +212 over the window, want >= 250. The known slow-VM frame-rate threshold: the VM ran at ~35 fps all session. It has failed identically since it21. |
| text_panels | FAIL | `stats.end` +205 / +194, want >= 300. The same frame-rate threshold. |
| state_snapshot | FAIL | Roaming bank victim (state 7) moved ~1.5 u between the core's snapshot and the harness's direct read. Reruns on 0.17.0: FAIL 2/2 (v5, v1). **On 0.16.1** (stash, triggers.cpp held out, rebuilt): PASS 1 of 2, FAIL 1 of 2 on the same thing (`v5` moved 1.5 u, plus fires 86 vs 85). So this is the test's existing race (CLAUDE.md "known environmental"), not this change; the test is unchanged. |

All other tests PASS: addr_bad_signature, addr_unknown_exe, addr_verified, autoexec, baseline_route,
console_basic, console_menu_safe, core_loads, fov_parity_reference, fov_sets_camera, glitch_parity_fuzz,
glitch_parity_log, glitch_records, hooks_frame, hooks_toggle, hud_player, profile_run, skip_intro,
speedrun_reference_deterministic, speedrun_rescuebot, speedrun_static_spawns, text_alpha, tp_teleport,
triggers, vars_binds_persist, window_alttab, window_parity_reference.
