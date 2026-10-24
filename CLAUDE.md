# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

AI-assisted decompilation of a video game using Ghidra + GhidrAssist +
GhidrAssistMCP + Claude Code, on a Windows Hyper-V VM built by Packer.
`plan.md` is the living project plan — phases, decisions, and what's
actually been verified vs. assumed. Read it before starting work; update
it as you go, in place, rather than treating it as fixed.

`RESEARCH.md` is the companion file for the game itself — identity,
subsystem map, current top-down decompilation strategy, open questions.
Research the game (prior art, engine, format docs) before starting
granular Ghidra work; record findings there. `plan.md` is the
infrastructure plan, `RESEARCH.md` is the domain understanding — keep
them separate.

**The repo reaches the VM as a plain file copy** via the Packer file
provisioner, not a clone. Since 2026-10-21 the VM copy is also a local git
repo (PortableGit at `environment\local\tools\git`, not on PATH; branches
`main` + `fdny-bxt`). Since 2026-10-22 it has a GitHub remote: `origin` =
`git@github.com:RichardFranksComputer/fdny-tas.git` (private). The VM pushes with a
write-enabled deploy key (`~/.ssh/fdny_decomp_deploy`, set by the repo-local
`core.sshCommand`). **Push after every commit** (`git push`; both branches track
origin). The owner reads and edits from another machine, so **`git pull` before
starting work**. Never commit a file over 100 MB (GitHub rejects it), the game's
binaries, or the Ghidra project's `.rep` (it contains the game's bytes).
Enable the tracked hooks once per clone: `git config core.hooksPath .githooks`.
The GitHub CLI is at `environment\local\tools\gh\bin\gh.exe` (not on PATH; `get_gh.ps1` /
`install_gh.ps1`, pinned in `config.ps1`) for PRs, Actions runs and releases. The owner logs
it in with a fine-grained token scoped to this repo; never ask for or handle the token.
`gh` shells out to git, so put `environment\local\tools\git\cmd` on PATH first. The token
can't upload SSH keys (the 403 on `user/keys` at login is expected); git keeps the deploy key.
One branch per feature or fix, merged through a PR; don't commit straight to `main` / `fdny-bxt`.

**FDNYCore (`core/`) has its own rules:** `core/CLAUDE.md` (loaded when
working there), `core/DECISIONS.md`, and the `new-feature` skill
(`.claude/skills/new-feature/`) for adding features.

**You may install additional tools as work demands it** — follow the
existing `get_<thing>.ps1`/`install_<thing>.ps1` convention in
`environment/scripts/` and add params to `environment/config.ps1`, then
update `environment/packer/scripts/provision.ps1` (and the `.pkr.hcl` if
new files need uploading) so future builds include it too. **Never
rebuild the VM yourself** — if you're running inside it, `packer build`
would tear down and recreate the machine you're running on. Editing the
build scripts for next time is correct; triggering that build isn't —
that's a decision made from the host, by a human.

## Conventions

- `environment/config.ps1` is the single source of truth for every
  path, version, URL, and VM/build parameter. Scripts dot-source it —
  never hardcode a value it already defines.
- Every script in `environment/scripts/` is flat (no subfolders) and
  follows `get_<thing>.ps1` (download only) / `install_<thing>.ps1`
  (extract/install only) naming. `common.ps1` holds shared helpers
  (`Get-RepoFile`, `Expand-ToolZip`, `Get-FirstSubDir`, `Set-ConfigValue`,
  `Add-ToolPluginInclude`).
- Define script-specific params (URLs, filenames) as named variables at
  the top of the file, not inline literals — a value used by more than
  one script belongs in `config.ps1` instead of being duplicated.
- `environment/local/` is entirely gitignored, machine-generated state
  (`local/tools/`, `local/downloads/`, Ghidra projects). Nothing tracked
  lives under it — if you're tempted to put a script there, put it in
  `environment/scripts/` instead.
- PowerShell is the default because this is a Windows/Hyper-V/Ghidra
  environment, not a hard rule — reach for Python (or anything else)
  the moment PowerShell is actually the wrong tool for something (data
  analysis, a viewer UI, etc.). Don't force PowerShell-only if it
  hinders the work.
- Before asserting something "can't be done" or "requires manual GUI
  steps," verify empirically (diff before/after state, check actual
  logs) rather than reasoning from general knowledge. This project has
  been wrong about that more than once — see `plan.md`'s Phase 1.5/2
  notes.

## Environment gotchas (verified, not assumed)

- **This repo lives on a cloud-sync drive** (Google Drive, `G:\`).
  Hyper-V's VMMS service cannot attach an ISO/VHD stored there — ACL
  errors from `Add-VMDvdDrive`. Packer's cache/output
  (`$PackerCacheDir`, `$PackerOutputDir` in `config.ps1`) must stay on
  a real local NTFS volume (`C:\`).
- **No `oscdimg`/`mkisofs` installed** — the Packer template uses
  Generation 1 VMs + `floppy_files` for `autounattend.xml`, not
  Generation 2 + `cd_files`, to avoid needing an external ISO-authoring
  tool.
- **`autounattend.xml`'s `FirstLogonCommands` must not cap**
  `winrm/config/winrs MaxMemoryPerShellMB` (must be `"0"`) — WinRS
  enforces that as a job-object memory limit on the entire process tree
  spawned by a remote shell, which silently kills a memory-hungry child
  process (e.g. Ghidra's headless JVM) even when the VM has plenty of
  RAM.
- **GhidrAssistMCP's server only starts once a project + CodeBrowser
  tool is actually opened** (a program loaded inside it) — not from
  Ghidra's bare Project Manager window, and not just from opening a
  `.gpr` project file via the command line either.
- **PowerShell Direct / WinRM sessions run in Session 0** (no
  interactive window station) — a GUI app launched that way hangs
  during AWT/Swing init. To drive a real GUI inside the VM
  programmatically, use a scheduled task with `schtasks /it`
  (interactive token), which attaches to the console's actual
  interactive session instead.
- VM admin credentials: `Administrator` / `pass` (also the value of
  `$VmAdminPassword` in `config.ps1` — keep both in sync with
  `autounattend.xml` if either changes). The VM has persistent
  registry-based autologon and no screen lock/sleep — see `plan.md`
  Phase 2's "post-build changes."

## Rebuilding the VM

```powershell
.\environment\scripts\build_vm.ps1
```

Needs an **elevated** PowerShell — Hyper-V VM creation requires admin
rights (or `Hyper-V Administrators` membership), and that can't be
scripted around. See `environment/packer/README.md` for the full
picture, including how to import the built VM into Hyper-V Manager
afterward (`packer build` always unregisters its own build VM).

## Agent orchestration

Heavy reasoning (the primary session, or an explicit high-effort/`opus`
agent) is for strategic work: deciding what to investigate next,
resolving conflicting evidence, judging a hypothesis confirmed.
Lightweight subagents (default/cheap model via the `Agent` tool, or a
`fork`) are for mechanical work: a single GhidrAssistMCP tool call, a
narrow lookup with a well-defined output. Use Claude Code's native
subagent dispatch for this — don't build custom multi-model
orchestration infrastructure until that's demonstrably insufficient.
See `plan.md`'s "Agent orchestration guidance" for the full reasoning.
