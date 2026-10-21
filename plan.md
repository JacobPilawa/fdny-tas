# Plan

AI-assisted decompilation of a video game using Ghidra, GhidrAssist,
and Claude Code connected to GhidrAssist.

Phase 1 and 1.5 are well-defined. Later phases are directional and
will be revised as work progresses — agents should update this file
in place rather than treat it as fixed. If you discover something that
changes the plan, change the plan — don't just note the discovery and
keep following stale guidance.

`RESEARCH.md` is the companion file for the game itself: what it is,
its subsystem map, current top-down decompilation strategy, open
questions. This file (`plan.md`) is the infrastructure/build plan;
`RESEARCH.md` is the domain understanding. Keep them separate.

No git on the VM — this repo reaches it as a plain file copy via the
Packer file provisioner (see `environment/packer/windows-server.pkr.hcl`),
not a clone. If you edit `plan.md`, `RESEARCH.md`, or `CLAUDE.md` while
working on the VM, those edits only exist on the VM until someone
copies them back to the dev machine — there's no push/pull.

## Prime directive (project owner, 2026-08-27)

The point of the decompilation is **clarity of labeling**, not a
recompilable source tree. The target end-state: a Ghidra database where
function names, parameters, locals, globals, structs, enums, and
comments are accurate and complete enough that an agent can answer a
functional question about any game system ("how does the rescuebot pick
a victim?", "what ends a level?", "how does player movement work?") by
reading named decompiled code — no re-deriving structure each time.

Implications for how the phases below are run:
- Renaming / retyping / commenting / struct creation against the live
  Ghidra project is the **work product**, done in labeled batches with
  the reasoning recorded — not the approval-gated exception described in
  Phase 5/8. The owner has authorized unattended mutation for labeling.
- `RESEARCH.md` now has the concrete subsystem map, engine identity
  (Genesis3D — open source), and the labeling strategy. Read it first.
- Phase 3's SQLite store is still the right home for granular per-symbol
  evidence and for surviving context resets during a long labeling run —
  worth standing up early rather than deferring.

## Phase 1 — Repo scaffolding

- [x] `environment/scripts/get_ghidra.ps1` — download Ghidra
- [x] `environment/scripts/get_ghidrassist.ps1` — download GhidrAssist
- [x] `environment/scripts/get_claude.ps1` — download Claude Code (not run yet, used in later phase)
- [x] `environment/scripts/install_ghidra.ps1`
- [x] `environment/scripts/install_claude.ps1`
- [x] `environment/scripts/get_fdny.ps1` — stage FDNY game resources (zip + `.rb` save) into `environment/fdny/`
- [x] `environment/scripts/common.ps1` — shared download/extract helpers
- [x] `environment/config.ps1` — central user-tweakable params, incl. placeholder game `.exe` path
- [x] Test: get + install Ghidra
- [x] Test: get + install GhidrAssist
- [x] Claude get/install scripts: no test required this phase
- [x] `get_fdny.ps1`: speedrun.com blocks scripted downloads (Cloudflare JS
      challenge) — script stages `environment/fdny/` and prints manual
      download instructions instead of fetching directly; auto-extracts once
      the files are placed by hand

## Phase 1.5 — Wire up GhidrAssist + Claude Code — done

GhidrAssist (chat panel inside Ghidra) and GhidrAssistMCP (MCP server inside
Ghidra, exposes Ghidra's RE functions as tools) are separate extensions.
The connection to Claude Code runs through GhidrAssistMCP, not GhidrAssist.

Initially assumed enabling a Ghidra extension (File > Install Extensions +
Configure Plugins) required manual GUI clicks. Verified that's false: diffed
Ghidra's per-user tool config (`_code_browser.tcd`) before/after enabling
both extensions by hand, found it's just two `<INCLUDE CLASS="..."/>` lines
plus unpacking the extension zip into Ghidra's user `Extensions/` folder —
both fully scriptable. Proved it with a complete from-scratch rebuild (wiped
`environment/local/tools/`, all downloads, and Ghidra's AppData state; reran
every script) — Ghidra came up with both plugins active and the MCP server
self-started on port 8080, zero manual steps.

- [x] `environment/scripts/get_jdk.ps1` / `install_jdk.ps1` — Ghidra needs a
      JDK to launch and doesn't bundle one; this was a Phase 1 gap, fixed
      here. Pinned to Adoptium Temurin 21 "latest" (not version-coupled to
      Ghidra the way GhidrAssist is).
- [x] `environment/scripts/get_ghidrassistmcp.ps1` — download GhidrAssistMCP
- [x] `environment/local/scripts/config_ghidra.ps1` — headlessly creates a
      Ghidra project imported from `$GameExePath` via `analyzeHeadless`,
      writes `$GhidraProjectName` back into `config.ps1`. Also what creates
      Ghidra's default per-user tool config as a side effect, which
      `enable_ghidrassist.ps1` depends on existing. Smoke-tested against
      `notepad.exe` (full import + analysis succeeded) both standalone and
      as part of the full rebuild; reverts cleanly once a real
      `$GameExePath` is set.
- [x] `environment/local/scripts/enable_ghidrassist.ps1` — unpacks
      GhidrAssist/GhidrAssistMCP straight from `environment/downloads/` into
      Ghidra's user `Extensions/` folder and patches `_code_browser.tcd` to
      enable both plugin classes. Idempotent (checked against the live file:
      re-running after a GUI-driven enable was a byte-for-byte no-op).
      Supersedes `install_ghidrassist.ps1` / `install_ghidrassistmcp.ps1`
      (deleted — they staged zips into the *install's* `Extensions\Ghidra`
      folder, which turned out to only cause a spurious "extensions found"
      dialog on launch since Ghidra never actually consumes them from there
      without the GUI install step).
- [x] `environment/local/scripts/run_ghidra.ps1` — launches Ghidra with
      `JAVA_HOME` set (no system-wide JDK on this machine); `-OpenProject`
      opens `$GhidraProjectName` directly, skipping the project picker.
- [x] `environment/local/scripts/connect_claude_mcp.ps1` — registers
      GhidrAssistMCP's SSE endpoint (`http://localhost:8080/sse`) as a
      project-scoped MCP server via `claude mcp add`, written to `.mcp.json`
      (tracked in git, so it's shared with anyone who clones the repo).
- [x] Full connection test: with Ghidra open on the notepad project,
      `GhidrAssistMCPServer` logged `Server state: STARTED` on port 8080
      unprompted; `curl http://localhost:8080/sse` returned a valid MCP SSE
      handshake (`event: endpoint`). `.mcp.json` registration confirmed via
      `claude mcp get`.
- [ ] One remaining non-scriptable step, by design: Claude Code shows new
      project-scoped `.mcp.json` servers as "Pending approval" until a user
      interactively approves them (`/mcp` in a Claude Code session in this
      repo) — a deliberate trust gate, not something to script around.

## Phase 2 — Packer build — done

Branch: `phase2-packer-hyperv`.

`packer build` succeeds end-to-end from a clean slate: JDK, Ghidra,
GhidrAssist, GhidrAssistMCP (both plugins enabled, verified in the exported
VM's tool config), and Claude Code all baked in, zero manual GUI steps.
Build artifact: `C:\packer-output\fdny-decomp` (outside the repo — see
`environment/packer/README.md`).

Blocker hit early: this shell had no Hyper-V rights at all (not admin, not
`Hyper-V Administrators`), and VM creation needs interactive elevation —
can't script around that. User relaunched the whole session elevated
(`Enable-WindowsOptionalFeature ... Microsoft-Hyper-V-Management-PowerShell`
was also needed — platform was on, the PowerShell module wasn't).

ISO sourcing changed from the original assumption: Microsoft's Server 2025
eval page now gates the ISO behind a registration/lead-capture form (no
stable direct link — confirmed by resolving the fwlink and landing on
`info.microsoft.com/ww-landing-...`). Server 2022's eval still has a working
static link (verified live, HTTP 200, ~5GB) — used that instead. SHA256
pinned once downloaded.

Failures hit and fixed, in order (each one real, verified against actual
tool output — see [[feedback-verify-dont-assert]]):

1. **Hyper-V can't attach ISOs/VHDs from Google Drive.** This repo lives on
   a cloud-sync drive (G:\); `Add-VMDvdDrive` failed with "Access is
   denied" — VMMS can't set the NTFS ACLs it needs on a cloud-sync
   filesystem. Fix: Packer's ISO cache and `output_directory` moved to
   `C:\packer-cache` / `C:\packer-output` (real local NTFS). This is a
   general constraint for this repo, not just Packer — anything needing
   Hyper-V-managed storage has to live off the Drive-synced path.
2. **No CD-image tool for `cd_files`.** Packer needs `oscdimg`/`mkisofs`/etc.
   to build the autounattend CD, none installed. Fix: switched Generation
   2 → 1 and `cd_files` → `floppy_files` — Packer builds floppy images
   natively, no external tool needed, and Windows Setup scans a floppy
   root for `autounattend.xml` the same as a CD root.
3. **File provisioner nested the uploaded scripts one level deep**
   (`environment/scripts/scripts/*.ps1`) because `source = "../scripts"`
   had no trailing slash — Packer's file provisioner copies the source
   *directory itself* into the destination without one. Fixed by adding
   trailing slashes.
4. **Guest OOM during the smoke-test Ghidra import**, twice, with two
   different apparent causes before finding the real one:
   - First: "insufficient memory for the JRE" during `analyzeHeadless`
     with an 8GB VM. Assumed Ghidra's heap auto-detection was too
     aggressive; bumped VM RAM to 16GB — didn't fix it.
   - Second: a *smaller* allocation failure even with 16GB assigned
     (confirmed via `Get-VM | Select MemoryAssigned` — the VM really did
     have 16GB). Root cause: `autounattend.xml`'s own
     `FirstLogonCommands` had set
     `winrm/config/winrs @{MaxMemoryPerShellMB="1024"}` — WinRS enforces
     that as a job-object memory cap on the *entire process tree* spawned
     by the remote shell, including the JVM Ghidra launches as a child
     process. Fixed by setting it to `"0"` (unlimited). VM memory stayed
     at 16GB (harmless, host has 32GB, and real game analysis will want
     the headroom).
5. **`install_ghidra.ps1` silently dropped from `provision.ps1`** during
   an unrelated edit (removing a wrong MAXMEM fix) — caught by noticing
   `config_ghidra.ps1` failed on "Ghidra install not found" and grepping
   the full log for confirmation it never ran that build.

Also: a build survived a real host power/network outage mid-run without
any special handling — Hyper-V VMs and WinRM don't care about the host's
internet connectivity once the VM itself is running locally.

- [x] `environment/scripts/get_packer.ps1` / `install_packer.ps1` — Packer
      1.16.0, portable zip, no admin needed.
- [x] `packer plugins install github.com/hashicorp/hyperv` — installed to
      `%APPDATA%\packer.d`, outside the repo.
- [x] `environment/config.ps1` — Packer, Windows ISO (pinned SHA256), and
      VM-spec params (`$WindowsIsoUrl`, `$VmName`, `$VmCpus`,
      `$VmMemoryMb=16384`, `$VmDiskSizeMb`, `$VmAdminPassword`,
      `$PackerCacheDir`, `$PackerOutputDir`, both on `C:\`) as the single
      source of truth.
- [x] `environment/packer/http/autounattend.xml` — unattended Server 2022
      Standard (Desktop Experience) install, WinRM enabled with no shell
      memory cap. **Known manual sync point:** the Administrator password
      is hardcoded here and must match `$VmAdminPassword` in `config.ps1`
      — no templating wired up yet.
- [x] `environment/packer/windows-server.pkr.hcl` — `hyperv-iso` source,
      Generation 1 + `floppy_files`, `output_directory` on local disk,
      uploads `config.ps1` + `environment/scripts/` +
      `environment/local/scripts/` (trailing slashes!), runs
      `provision.ps1`.
- [x] `environment/packer/scripts/provision.ps1` — guest-side rerun of the
      full Phase 1/1.5 pipeline plus the notepad.exe smoke test, reverting
      config values afterward.
- [x] `environment/local/scripts/build_vm.ps1` — reads `config.ps1`, sets
      `$env:PACKER_CACHE_DIR`, runs `packer init` + `packer build -force`.
- [x] `environment/packer/README.md`.
- [x] Full build run (18m38s once the ISO was cached): succeeded, exit 0.
- [x] Verified the artifact for real: imported a copy of the exported VM
      (`Import-VM -Copy -GenerateNewId`), booted it, used
      `Invoke-Command -VMName` (PowerShell Direct — no network config
      needed) to confirm Ghidra/JDK/Claude Code present on disk and both
      `<INCLUDE CLASS="ghidrassist...">` lines in `_code_browser.tcd`.
      Cleaned up the verification copy afterward.
- [x] **MCP server confirmed fully working inside the built VM.** Rebuilt
      after the post-build changes below, imported, booted. Confirmed
      Ghidra's Project Manager alone does NOT start the MCP server — no
      "GhidrAssist" log lines until a project + CodeBrowser tool is
      actually opened (plugin instantiates per-Tool, not from the
      front-end window). Since PowerShell Direct/WinRM run in Session 0
      (no interactive window station — GUI apps hang there, as seen
      earlier), launching Ghidra needed a scheduled task with
      `schtasks /it` (interactive token) to attach to the autologon'd
      console session (Session 1) instead — confirmed via
      `Get-Process javaw | Select SI` showing session 1, not 0. Opening
      the `.gpr` alone doesn't restore the last-active CodeBrowser
      tool/program either; user double-clicked `notepad.exe` in the VM
      via `vmconnect` for the final step. Log then showed the exact
      same sequence as the dev machine: `MCP Server started on port
      8080` → `Active tool changed: CodeBrowser` → `Program activated:
      notepad.exe`. Port 8080 confirmed open.
- [x] `environment/packer/README.md` now explains *why* build output has
      to be on `C:\` (Google Drive/cloud-sync ACL limitation).
- [ ] Decide whether to keep the built VM as a reusable Hyper-V
      checkpoint/export, or treat `packer build` as the reproducible
      artifact and rebuild on demand.

### Post-build changes (after the first successful build)

- [x] **Folder restructure**, at user's request — the original
      `scripts/` vs `local/scripts/` vs `local/tools/` split (from the
      Phase 1.5 plan wording) read as confusing once there was more in
      it. Flattened: every script (portable `get_*`/`install_*` and the
      machine-oriented ones like `config_ghidra.ps1`, `build_vm.ps1`)
      now lives directly in `environment/scripts/`, fully tracked.
      `environment/local/` is now entirely gitignored machine state —
      `local/tools/` (installed software) and `local/downloads/` (moved
      from `environment/downloads/`) — nothing tracked lives under it.
      Updated: every moved script's relative dot-source paths, the
      `.pkr.hcl` file provisioner (dropped the now-redundant third
      upload block), `provision.ps1`, `.gitignore`, and the README.
- [x] Desktop shortcuts (Ghidra, Claude Code) added to `provision.ps1`,
      on the Public desktop. Also fixed while at it: the native Claude
      Code installer doesn't add `claude.exe` to PATH — `provision.ps1`
      now appends `%USERPROFILE%\.local\bin` to the machine `PATH`.
- [x] Persistent autologon + no lock/sleep, at user's request — the
      unattend `<AutoLogon>` element's `LogonCount` expires after that
      many reboots, so added registry-based autologon
      (`Winlogon\AutoAdminLogon`/`DefaultUserName`/`DefaultPassword`)
      via `FirstLogonCommands`, which doesn't expire. Also disabled the
      screensaver, the "machine inactivity limit" auto-lock policy, and
      display/sleep timeouts (`powercfg`).
- [x] Admin password changed from `ChangeMe123!` to `pass` (found
      `autounattend.xml`'s `AdministratorPassword` already changed to
      `pass` externally, with `AutoLogon.Password` left mismatched at
      `ChangeMe123!` — flagged it rather than guessing, user confirmed
      `pass` was intentional; synced both fields plus `$VmAdminPassword`
      in `config.ps1` plus the new registry autologon commands).
- [x] Rebuilt after all the post-build changes above — succeeded, and
      confirmed for real: restructured paths correct
      (`environment\local\downloads\`, `environment\local\tools\` in
      the build log), desktop shortcuts created, both plugins enabled,
      and — per the MCP confirmation above — the whole toolchain
      actually works inside the freshly-built VM, not just that the
      build script exits 0.

## Phase 3 — Persistent research state (SQLite) — decided

Storage question from the original Phase 3 draft is resolved: SQLite as
the canonical research datastore. Durable, queryable, survives context
resets/VM rebuilds/model changes, copies to another machine and reopens
with zero services running. Raw artifacts (dumps, screenshots, long text)
stay as files on disk, referenced by path from the DB — never blobbed in.

This section was adapted from a ChatGPT-drafted plan the user reviewed
adversarially. Kept: the SQLite decision, the fact/hypothesis/evidence
separation, append-only events, read-only-first safety. Cut, with
reasons, from that draft:
- **No bespoke data-access layer to start.** Claude Code (and any
  subagents) talk to the DB directly via the `sqlite3` CLI —
  `get_sqlite.ps1`/`install_sqlite.ps1`, same download/install pattern as
  every other tool in `environment/scripts/`. Not a PowerShell-only rule
  — Python (or anything else) is fine the moment raw SQL over the CLI
  is actually the wrong tool for something; just don't add the runtime
  before there's a real reason to.
- **5 tables to start, not 10.** Prove one investigation survives a
  context reset before adding structures/relationships/observations/
  decisions/agent_runs tables.
- The draft's Phase 5 (a bespoke "Ghidra tool API": `get_function`,
  `get_xrefs`, `rename_symbol`, etc.) is redundant — GhidrAssistMCP
  (wired up in Phase 1.5) already exposes ~49 tools covering exactly
  this. See the Phase 5 section below.
- The draft's Phases 9/10/15 (specialist agent roster, a distributed
  master/worker model hierarchy, local-model benchmarking) aren't
  adopted as infrastructure to build — see "Agent orchestration
  guidance" below. Claude Code's own subagent dispatch is the default
  until it demonstrably isn't enough.

- [x] `environment/scripts/get_sqlite.ps1` / `install_sqlite.ps1` —
      `sqlite3.exe` CLI. Downloaded + installed on the VM 2026-08-27
      (`environment/local/tools/sqlite/sqlite3.exe`, 3.53.4).
- [x] `environment/config.ps1` — added `$ResearchDir`, `$ResearchDbPath`,
      `$ResearchHtmlPath`, `$ResearchSchema`, `$SqliteExe`. DB lives at
      the **repo root** `research/` (project output, travels back to the
      host by file copy), not under `environment/local/`.
- [x] Initial schema — 5 tables (`symbols`, `hypotheses`, `evidence`,
      `research_tasks`, `events`) in
      `environment/scripts/research_schema.sql`; applied by
      `environment/scripts/init_research_db.ps1` (idempotent; seeds the
      task queue from RESEARCH.md's strategy on first create).
- [x] Schema principles enforced: `symbols` (facts) vs `hypotheses`
      (interpretation) are separate; a hypothesis is revised by a new row
      + `superseded_by`, never overwritten; `evidence` FKs a hypothesis
      and/or symbol; `confidence` is an explicit CHECKed column on both
      `symbols` and `hypotheses`; `events` is append-only.
- [ ] **Prove the loop** — still the real milestone. Session 1 (this one)
      stood up the DB and recorded the first symbols/hypotheses/evidence
      + a seeded task queue. Not yet proven: a *fresh* context window
      opens `research.db`, reads the queue + progress log, and continues
      without re-deriving. Test this on the next session boundary.
- [ ] **Host-only follow-up (can't do from the VM — no `environment/packer/`
      here):** add `get_sqlite`/`install_sqlite` + `init_research_db.ps1`
      to `provision.ps1`, and upload `research_schema.sql` /
      `research_report.ps1` in the `.pkr.hcl`, so a rebuilt VM has the
      research store too.

## Phase 4 — Research state, human-readable — built 2026-08-27

Pulled forward at the owner's request (wanted a viewer to watch progress
during the autonomous labeling run). No Python on the VM, so it's a
PowerShell generator, not a web app:

- `environment/scripts/research_report.ps1` dumps each DB table to JSON
  via `sqlite3 -json` and splices it into one self-contained
  `research/research.html` (vanilla JS, no external assets, opens from
  `file://`). Tabs: Overview (symbols-by-subsystem progress bars +
  recent activity), Symbols (filter by subsystem/confidence/text),
  Hypotheses (with nested evidence), Tasks, Events.
- Run modes: bare (regenerate once), `-Watch <sec>` (regenerate on a
  timer), `-Serve` (HttpListener on :8899, regenerates per request).
- Regenerated after every batch of DB writes during labeling. The
  schema's still young, so the page is deliberately generic (plain
  tables) — revisit the layout once the schema has taken real load.

## Phase 5 — Ghidra tool interface (mostly already done)

The source draft assumed a bespoke Ghidra tool API needed to be built
from scratch. It doesn't — GhidrAssistMCP already exposes ~49 tools over
MCP (function/xref/string/decompiled-code/disassembly lookups, renames,
type/signature/comment edits, structure creation, etc.), confirmed when
it was wired up in Phase 1.5.

- [ ] Audit GhidrAssistMCP's actual tool list against what research
      tasks actually need — document real gaps, if any, before building
      anything
- [ ] Only if a real gap exists: add it as a Ghidra script (path TBD) —
      don't rebuild what MCP already exposes
- [ ] Safety rule, kept from the source draft: read-only MCP calls by
      default; anything that mutates the Ghidra project (rename,
      retype, comment, structure creation) requires explicit human
      approval before any agent may call it unattended

## Phase 6/7 — Tasks, evidence, hypotheses (folded into Phase 3's schema)

The source draft treated task tracking and the evidence/hypothesis
lifecycle as separate phases with their own elaborate feature lists
(task dependencies/priority/parent-child, a SPECULATION→CONFIRMED state
machine, etc.). Folded into Phase 3 instead — they're properties of the
same five tables, not separate systems. Add columns/states as real
usage demands them (e.g. a `parent_task_id` column when an actual task
needs splitting), not upfront.

## Phase 8 — First research agent

- [x] **Research the game itself before touching Ghidra.** Done
      2026-08-27 — see `RESEARCH.md`. Identity: *F.D.N.Y. Firefighter:
      American Hero* (2002, PC), dev Mekada / pub Activision Value,
      engine **Genesis3D v1.1** (open source), internal codename
      "Everblaze 2". First-pass subsystem map recorded from 54 source-
      path assert strings + imports (rescue / hose / script-triggers /
      sharedents / ROC / basic / sound / BluelAPI). Top-down
      labeling-first strategy + open questions written up there. Prior-
      art deep dive (format docs, any existing RE) still light.
- [ ] One Claude Code session: GhidrAssistMCP (read-only) + `sqlite3`
      CLI access, given current DB state for one function, a research
      question, and instructions to record findings before ending. No
      multi-agent dispatch yet — this is Phase 3's "prove the loop"
      milestone with a real research question instead of a smoke test.
- [ ] Proposed Ghidra mutations (renames, retypes, comments) get
      written to the DB as a proposal + evidence, not applied directly
      — a human approves before anything mutates the live Ghidra
      project, per Phase 5's safety rule.
      **Superseded by the Prime directive for labeling work** — labeling
      renames/retypes/comments are applied directly and logged. The
      proposal-first rule still holds for *semantic* changes that aren't
      just naming (e.g. changing a function's detected signature in a way
      that alters the decompile materially) if there's real doubt.

## Phase 9 — Full labeling pass (the Prime-directive deliverable)

Goal state in `RESEARCH.md` → "Strategy". Not function-by-function
poking: work the three free levers first (assert-string → translation-
unit clustering; Genesis3D public headers as Ghidra types; FMOD3 /
DInput8 public APIs), then hand-label by subsystem in priority order.

- [ ] Apply Genesis3D types. Recover `geVec3d`, `geXForm3d`, `geWorld`,
      `geActor`, `geEntity`, `geMotion`, `gePath`, `geCamera`,
      `geBitmap`, `geBody`, `geEngine` layouts from the open-source
      headers; create them as Ghidra structs; retype the `GENESIS.DLL`
      import thunks with real signatures.
- [ ] Map assert strings → functions. For each of the 54 `dir\File.cpp`
      strings, xref → cluster the referencing functions → name them
      after the class/file. Record the cluster→file map (DB or
      `RESEARCH.md`).
- [ ] Bootstrap + frame loop: `main.cpp` / `evergame.cpp` — name the
      per-frame update/render call tree.
- [ ] Entity base classes + Genesis3D entity registration — get the
      shared vtable/userdata shape so every `*Ent.cpp` slots in.
- [ ] Owner-priority systems, in order: `rescue\` (RescueBot first),
      `hose\`, `script\` + `sharedents\BoundsTriggerEnt`, `main\IO\`
      + resolve where player movement is integrated.
- [ ] Remainder by cluster: `ROC\`, `basic\`, `sound\`, `BluelAPI\`.
- [ ] Acceptance check: pick 2–3 of the owner's real questions and
      confirm they can be answered purely from the labeled decompile.

### Don't stop at the owner's named asks — go to full coverage

Owner directive (2026-08-27): the named priorities (rescuebot ✓,
level triggers ✓, movement — in progress) are the *starting* targets,
not the finish line. **The end state is a fully labeled decomp** —
every reachable function named, params/locals/globals meaningful,
structs recovered, key functions commented. When the priority queue
empties, the loop keeps going and picks its own next targets from the
baseline context. It does not stop and wait to be told.

**Target-selection heuristic when nothing is explicitly queued**, in
order:
1. **Finish partially-done clusters** — any TU with named anchors but
   `FUN_`/`DAT_` neighbours still in its address range.
2. **Unclaimed source files** — walk the 54 `dir\File.cpp` strings;
   any whose function cluster has zero named symbols is a fresh target
   (`ROC\`, `basic\`, remaining `sharedents\`, `main\IO\`, `sound\`,
   `BluelAPI\Modules\RichBinary`, `LevelManager`, `PlayerManager`,
   `console`, `config`).
3. **Call-graph frontier** — `xrefs` from already-named functions into
   still-`FUN_` callees; name the callees (they're the mechanism behind
   something already understood).
4. **Biggest unnamed functions** — sort remaining `FUN_` by size; large
   ones are usually a subsystem's core and unlock many neighbours.
5. **Global sweep** — remaining `DAT_` referenced from ≥3 named
   functions; figure out and name them.
6. **Type application pass** — once names stabilise, retype `this`
   params to the recovered structs and the `GENESIS.DLL` / FMOD thunks
   to real signatures, so the decompile reads cleanly.

Each cycle: dispatch read-only subagents on the chosen cluster, the
primary session adjudicates and commits, checkpoint to `research.db`,
regenerate `research.html`, log an `events` row. Stop only when a full
`get_functions` sweep shows no meaningful `FUN_`/`DAT_` left (or the
owner says stop). Track coverage as `named / total` per subsystem on
the report's Overview tab.

### Operating guidance for the agent running on the VM

- You may download and install additional tools as the work demands
  it. Follow the existing convention: a `get_<thing>.ps1` /
  `install_<thing>.ps1` pair in `environment/scripts/`, parameters
  added to `environment/config.ps1` — don't install things by hand
  outside that pattern, or a rebuilt VM won't have them.
- Update `environment/packer/scripts/provision.ps1` (and the `.pkr.hcl`
  if new files need uploading) so anything you added gets baked into
  future builds too — the VM build should stay reproducible from
  scratch, not drift from what's actually installed.
- **Never rebuild the VM.** You run inside it — `packer build` tears
  down and recreates the VM you're running on. Editing the build
  scripts so the *next* build includes your changes is correct;
  triggering that build yourself is not. That's a human decision, from
  the host, when it's actually time to produce a new image.
- **Nothing to research until `$GameExePath` is real.** If it's still
  the placeholder when you start, stop and say so rather than
  inventing work — don't quietly research the notepad.exe smoke-test
  target as if it were the actual goal.
- **Checkpoint constantly, not at the end.** Write findings to
  `RESEARCH.md` (and the Phase 3 DB, once it exists) as you go. An
  overnight/long-running session will hit context resets or
  compaction; anything not written down by then didn't happen. Prefer
  many small writes over one big summary at the end.
- **Timebox stuck work.** If a task isn't yielding progress after a
  reasonable number of attempts, log it as blocked in `RESEARCH.md`
  (what was tried, why it's stuck) and move to a different task rather
  than looping on it indefinitely.
- **Leave a summary for the human.** Whoever reads this next needs one
  place that says what happened, what was found, and what needs a
  judgment call only a person can make — a dedicated section in
  `RESEARCH.md` (e.g. "Needs human attention"), not something scattered
  across session output that's already gone.
- **Don't touch the host or Hyper-V config, and don't do anything
  requiring a reboot, without flagging it first.** Installing a tool
  inside the VM via the established script pattern is fine; anything
  affecting the VM's own OS-level config beyond that needs a human's
  eyes first.
- For a long/overnight run, prefer the `/loop` skill (self-pacing,
  survives long stretches better) over one continuous unbroken session.

## Agent orchestration guidance

Per user request: heavy reasoning for big/strategic work, lightweight
agents for small mechanical work. This is Claude Code's own `Agent`
tool (`subagent_type`, `model` override) — not custom multi-model
infrastructure:

- **Strategic work** — deciding what to investigate next, resolving
  conflicting evidence, judging a hypothesis confirmed, synthesizing
  across many findings — stays with the primary session, or an explicit
  high-effort/`opus` agent for a specific hard sub-problem. This plays
  the role of the source draft's "master" model, but it's a
  model-selection choice, not a service to build.
- **Mechanical work** — "call GhidrAssistMCP's `get_xrefs` for
  `FUN_80123456` and report the raw results," "look up this string's
  references," anything with a narrow, well-defined output — goes to a
  lightweight subagent (`Agent` tool, default/cheap model, or a `fork`
  when it needs the parent's conversation context). Don't spend a
  high-effort model's budget on a single MCP tool call.
- Do not build the source draft's specialist-agent roster or a
  distributed master/worker model hierarchy as infrastructure. If
  Claude Code's native subagent dispatch turns out to be insufficient,
  that's a failure mode to observe first — build the custom version
  only after hitting a concrete wall, not speculatively.

## Future direction (unscheduled, not designed yet)

From the source draft, kept as pointers only — deliberately not
fleshed into checklists, because doing that now is the premature-
architecture problem the draft's own closing note warned against
("the architecture should evolve from observed failure modes rather
than being fully designed upfront"):

- Long-running/autonomous research (queueing, parallel workers,
  scheduled research)
- Autonomous mutation with rollback, once the approval-gated version
  has run long enough to trust
- Runtime/debugger analysis correlated with static findings
- Richer knowledge visualization (graphs)
- Local model / hardware evaluation, if cost or latency ever actually
  becomes a problem
- Dedicated server deployment, separate from the dev/analysis VM

None of these are scoped, and shouldn't be until Phase 3 and Phase 8
have actually run against the real game and produced a concrete reason
to build the next piece.
