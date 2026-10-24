---
name: new-feature
description: Add or substantially change an FDNYCore feature (a mod feature, overlay, HUD, console command, game patch, speedrun/practice tool) the established way -- feature module in core/src/features, core services only, address table, textdraw/gfx, tests, evidence, docs, version, commit, owner report. Use for any new FDNYCore capability, not for one-line fixes.
---

# Adding an FDNYCore feature

Follow these steps in order. The rules are in `core/CLAUDE.md` (arch_check enforces
R1-R5, the compiler R6-R7) and the reasons in `core/DECISIONS.md`. Don't re-derive them,
and don't work around them; if a rule blocks a good design, stop and ask the owner
(a rule changes only with a `DECISIONS.md` entry).

## 1. Orient (before writing code)

- Read `core/CLAUDE.md`, `core/DECISIONS.md` and the last few entries of `core/PROGRESS.md`.
- **Check for something existing first** (owner rule). Look in `core/src/features/`, the
  services in `core/src/`, the console commands (the repository `README.md`, the key sheet), and the old tools
  (`asi/`, `tools/`). Extend what's there instead of adding a parallel path.
- Decide the shape:
  - **Feature or service?** A feature uses services. If it needs a hook another piece
    already owns, or something two features would share, it's a service change first
    (core `src/`, with subscriptions or filters, like `camera`).
  - **Practice-only?** It is if it shows information the game doesn't, or moves the
    player. Then `cmd::SetPracticeOnly`.
  - **Drawing?** Pick the layer first (`core/CLAUDE.md`, "Drawing"; D40).
    - `LAYER_WORLD` for 3D things walls should hide.
    - `LAYER_OVERLAY` for x-ray views.
    - Text panels are `LAYER_HUD` automatically: a `textdraw` panel, `STYLE_TRANSLUCENT` by
      default, `STYLE_OPAQUE` if it must show in menus.
    - Use `gfx` blocks only for non-text, and never `hooks::OnFrameEndLate` (R8).
- Owner decisions that need asking (gameplay semantics, what's speedrun-legal): ask
  once, up front, with a recommendation.

## 2. Game research and addresses

- Ghidra lookups go through subagents (read-only); the primary session never calls
  GhidrAssist MCP. Prior knowledge: `research/research.db`, `RESEARCH.md`, and memory.
- Every address, offset or field gets an `addresses.json` entry with `sig` (first 16
  bytes, for code), `ghidra` (the evidence) and `note` (who uses it). Code uses
  `addr::Id::<entry>`; `build.ps1` regenerates the header.
- If a site may be patched by another mod, detect and defer (D5); never double-patch.

## 3. Code

- Copy `core/templates/feature.cpp` to `core/src/features/<name>.cpp`:
  - fill the `// Tests:` line, the `// User: <Title> -- <one sentence>` line (the key sheet's
    entry, R9) and the header paragraph;
  - register variables, commands, practice-only names and default keys with string literals
    (the key sheet reads them, D41); a key the feature reads itself (`GetAsyncKeyState`, not
    a bind) gets a `binds::DescribeKey(key, action, when)`;
  - one `FDCORE_MODULE`, listing the required Ids;
  - pick a start `order` after the existing modules (the `FDCORE_MODULE` lines in
    `core/src/features/`);
  - startup patches before WinMain go in the `early` step, guarded with
    `addr::EarlyCodeAddress`.
- Globals constant-initialisable only (`= {}`, `constexpr`; R7). No STL, CRT or
  `<initializer_list>`.
- Log a `<name>: ready (...)` line, and a reason on every refusal or disable.
- `.\core\build.ps1` must pass: arch_check and `-Werror`.

## 4. Test

- Copy `core/templates/test.yaml` to `core/tests/<name>.yaml` (in the suite). Prove the
  behaviour with assertions (log lines, memory, pipe), not just "no crash". Add
  screenshots for anything visible.
- Run it, then the full suite before the version bump:
  `python -I core/harness/run_test.py --suite` (Python not on PATH:
  `environment\local\tools\python`).
- Never weaken a test. For a failure you believe is environmental, rerun it, show it
  fails identically on the previous version or passes on rerun, and report it as such.

## 5. Finish

- Evidence: `core/evidence/<name>/` (logs, screenshots, numbers; a README for anything
  investigative).
- The player docs are the generated key sheet (the repository `README.md`, rewritten by every build
  and the pre-commit hook): check your feature's lines read well, and fix the help texts
  and `// User:` line in the code, never the sheet. A new service goes in `core/CLAUDE.md`'s
  "Core services" table.
- On the version bump: the release notes under `## <version>` in the root `CHANGES.md`.
  Merging the bump to `main` publishes the GitHub release (built by the workflow, D45).
- `core/DECISIONS.md`: an entry for every non-obvious choice (what, why, rejected).
- `core/PROGRESS.md`: `- <date> it<N>: **<version>** <what>; tests; evidence path.`
- `core/VERSION`: bump minor for a feature, patch for a fix.
- Commit (git not on PATH: `environment\local\tools\git\cmd`): one topic, message via
  `-F` from a UTF-8 file without BOM, with the Co-Authored-By / Claude-Session lines.
  Don't stage `research/research.db-*`.

## 6. Report to the owner

1. Outcome + version + commit, in the first sentence.
2. What changed, in the owner's terms: commands, variables, keys, how it looks and behaves.
3. Verification: which tests, with numbers; failures stated plainly with cause and evidence.
4. Evidence path.
5. Open items and decisions the owner needs to make. Offer the obvious next step in one
   line; don't start it unasked.
