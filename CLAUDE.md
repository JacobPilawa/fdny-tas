# CLAUDE.md

Guidance for Claude Code in this repository: speedrun and practice tooling for FDNY
Firefighter: American Hero (FDNYCore, an in-process `.asi` plugin), built on a Ghidra
decompilation of the game. `README.md` is the overview. The build, test and release rules
and the architecture are imported below; this file adds what is specific to agents and the VM.

@CONTRIBUTING.md
@docs/architecture.md

## Before working

- Read `docs/decisions.md` (why things are the way they are) and the recent `git log` (what
  happened last). For decomp or VM work, read `docs/decomp-vm.md` and `docs/game-research.md`.
- To add or substantially change a feature, use the `new-feature` skill
  (`.claude/skills/new-feature/`).
- **Asking vs deciding.** A choice that is irreversible and could reasonably go either way
  goes to the owner, once, with a recommendation. A reversible one takes the sensible
  default and is recorded.

## Working rules

- **Keep the living docs current, in place.** `docs/decomp-vm.md` is the infrastructure plan
  and `docs/game-research.md` the understanding of the game: when you learn something that
  changes them, change them rather than noting it and following stale guidance. Keep the
  two separate.
- **Research the game before granular Ghidra work** (prior art, the engine, format docs) and
  record what you find in `docs/game-research.md`.
- **Verify empirically before asserting something "can't be done"** or "needs manual GUI
  steps": diff before/after state, read the actual logs. This project has been wrong about
  that more than once (`docs/decomp-vm.md`, Phase 1.5/2).
- **Installing a tool** on the VM follows the `get_<thing>.ps1` / `install_<thing>.ps1`
  convention with its parameters in `environment/config.ps1` (local-only; `docs/decomp-vm.md`,
  "Decomp VM working notes", has the conventions and the gotchas).
- PowerShell is the default on this Windows VM, not a rule: use Python (or anything else)
  when PowerShell is the wrong tool. Edit UTF-8 docs with the Edit/Write tools, not a
  PowerShell `Get-Content`/`Set-Content` round trip (mojibake). Some `.cpp` and `build.ps1`
  are CRLF.

## Git and GitHub

- `origin` = `git@github.com:RichardFranksComputer/fdny-tas.git` (private). The VM pushes
  with a write-enabled deploy key (`~/.ssh/fdny_decomp_deploy`, set by the repo-local
  `core.sshCommand`).
- **`git pull` before starting work; push after every commit.** The owner reads and edits
  from another machine. Merge only when the owner says so.
- Commit messages go through `-F` from a UTF-8 file without BOM and end with the
  Co-Authored-By / Claude-Session lines.
- The GitHub CLI is at `environment\local\tools\gh\bin\gh.exe` on the VM (not on PATH). The
  owner logs it in with a fine-grained token scoped to this repo; never ask for or handle the
  token. `gh` shells out to git, so put git on PATH first.

## On the VM

- Git, Python, Zig and gh are under `environment\local\tools` and not on PATH:
  `environment\local\tools\git\cmd`, `environment\local\tools\python\python.exe` (run it with
  `-I`). The build and test scripts find them there, or on PATH elsewhere (CI).
- Local-only, untracked and ignored: `environment/` (VM build scripts, `config.ps1`, tools),
  `research/` (research.db), `asi/`, `asi-archive/`, `livesplit/`, `reference/original_tools/`,
  `backup/`, `build/`, `.mcp.json`. **A checkout or pull that deletes a once-tracked file also
  deletes it from disk**; the backup of everything removed on 2026-10-26/27 is
  `C:\fdny-decomp-untracked-20261026` (see `docs/decomp-vm.md`, "Decomp VM working notes").
- **Never rebuild the VM** (`packer build` would destroy the machine you run on); that is
  a human decision, made from the host.
- Never edit the game's exe or Genesis.dll on disk; back up any game-folder file before
  replacing it.
- Ghidra work goes through subagents; the primary session never calls GhidrAssist MCP
  itself. Heavy reasoning (the primary session) decides what to investigate and judges
  evidence; lightweight subagents do single lookups (`docs/decomp-vm.md`, "Agent
  orchestration guidance").

## Report to the owner (end of every task)

- the outcome and version/commit, first sentence;
- what changed, in owner terms (commands, behaviour, look);
- verification: tests with numbers; failures stated plainly with cause and evidence;
- evidence path;
- open items and anything needing an owner ruling.
