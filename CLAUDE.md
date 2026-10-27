# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Speedrun and practice tooling for FDNY Firefighter: American Hero, built on a Ghidra
decompilation of the game:

| Path | What |
|---|---|
| `core/` | **FDNYCore**, the in-process `.asi` plugin: core services, feature modules, address table, tests, evidence. Its own rules: `core/CLAUDE.md`, `core/DECISIONS.md`, and the `new-feature` skill (`.claude/skills/new-feature/`) for adding features. |
| `README.md` | The player's key sheet, generated from the source by every build (never edit it by hand). |
| `CHANGES.md` | Hand-written release notes; the release workflow publishes each version's section. |
| `ghidra_export/` | The Ghidra labels (names, comments, types; no game bytes) and how to restore them. Regenerate with `tools/ghidra_scripts/ExportProgramBackup.java`. |
| `.github/workflows/` | `unit` and `sheet-check` on pull requests and pushes to `main`; `release` publishes `v<core/VERSION>` from `main`. |
| `plan.md`, `RESEARCH.md` | The decompilation effort: the VM/infrastructure plan (with the decomp VM's working notes at the end) and what is known about the game. Read them before Ghidra fact-finding or labeling work. |

The repo holds only what the tooling needs: no game binaries, no Ghidra `.rep`, no raw
logs or run output, no files nothing uses.

## Working rules

- **Keep the living docs current, in place.** `plan.md` is the infrastructure plan and
  `RESEARCH.md` the understanding of the game: read them before decomp or VM work, and when
  you learn something that changes them, change them rather than noting it and following
  stale guidance. Keep the two separate.
- **Research the game before granular Ghidra work** (prior art, the engine, format docs) and
  record what you find in `RESEARCH.md`.
- **Verify empirically before asserting something "can't be done"** or "needs manual GUI
  steps": diff before/after state, read the actual logs. This project has been wrong about
  that more than once (`plan.md`, Phase 1.5/2).
- **Installing a tool** on the VM follows the `get_<thing>.ps1` / `install_<thing>.ps1`
  convention with its parameters in `environment/config.ps1` (local-only; `plan.md`, "Decomp
  VM working notes", has the conventions and the gotchas).
- PowerShell is the default on this Windows VM, not a rule: use Python (or anything else)
  when PowerShell is the wrong tool.

## Git and GitHub

- `origin` = `git@github.com:RichardFranksComputer/fdny-tas.git` (private). The VM pushes
  with a write-enabled deploy key (`~/.ssh/fdny_decomp_deploy`, set by the repo-local
  `core.sshCommand`).
- **`git pull` before starting work; push after every commit.** The owner reads and edits
  from another machine.
- One branch per feature or fix, cut from `main` and merged through a PR. Never commit
  straight to `main`; merge only when the owner says so.
- Never commit a file over 100 MB (GitHub rejects it), the game's binaries, or the Ghidra
  project's `.rep` (it contains the game's bytes).
- Enable the tracked hooks once per clone: `git config core.hooksPath .githooks` (the
  pre-commit hook regenerates the key sheet).
- The GitHub CLI is at `environment\local\tools\gh\bin\gh.exe` on the VM (not on PATH). The
  owner logs it in with a fine-grained token scoped to this repo; never ask for or handle the
  token. `gh` shells out to git, so put git on PATH first.

## On the VM

- Git, Python, Zig and gh are under `environment\local\tools` and not on PATH:
  `environment\local\tools\git\cmd`, `environment\local\tools\python\python.exe` (run it with
  `-I`). The core's build and test scripts find them there, or on PATH elsewhere (CI).
- Local-only, untracked and ignored: `environment/` (VM build scripts, `config.ps1`, tools),
  `research/` (research.db), `asi-archive/`, `livesplit/`, `reference/original_tools/`,
  `core/backup/`, `.mcp.json`. **A checkout or pull that deletes a once-tracked file also
  deletes it from disk**; the backup of everything removed on 2026-10-26/27 is
  `C:\fdny-decomp-untracked-20261026` (see `plan.md`, "Decomp VM working notes").
- **Never rebuild the VM** (`packer build` would destroy the machine you run on); that is
  a human decision, made from the host.
- Never edit the game's exe or Genesis.dll on disk; back up any game-folder file before
  replacing it.
- Ghidra work goes through subagents; the primary session never calls GhidrAssist MCP
  itself. Heavy reasoning (the primary session) decides what to investigate and judges
  evidence; lightweight subagents do single lookups (`plan.md`, "Agent orchestration
  guidance").
