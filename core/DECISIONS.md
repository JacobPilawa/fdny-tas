# FDNYCore decisions

Why things are the way they are. One entry per non-obvious choice: the decision,
the reason, and what was rejected. Add an entry whenever you make a choice a
later session might "simplify" away; change one only with the owner's approval
(say so in the entry). Evidence paths are under `core/evidence/`.

## Toolchain and runtime

- **D1 Zig (clang) freestanding C++17, no CRT.** Owner: keep Zig, MSVC only if needed. One small `.asi` with no runtime dependency, like the earlier `asi/` plugins. Consequences: no STL or exceptions, no 64-bit integer division helpers, no stack probes, and **no static constructors or exit-time destructors** (D2).
- **D2 No dynamically initialised globals (rule R7), enforced with `-Wglobal-constructors -Wexit-time-destructors`.** Nothing runs them. When this was enforced (0.16.1) it found two real cases:
  - `textdraw`'s panel array: benign, all-zero by luck;
  - `glitch_bsp`'s `ON_EPSILON = OnEpsilon()`: never initialised, so the BSP on-plane tolerance could be 0 instead of 0.1.
- **D3 MinHook (vendored) for every inline hook; one owner per hooked function.** Two MinHook hooks on one target in one instance aren't possible, and two owners would fight over semantics. Shared hooks live in a core service that offers subscriptions or filters (`hooks` frame events, `camera`, `gfx` world).

## Game access

- **D4 Every address lives in `addresses.json`** (rule R4). It is versioned by exe + Genesis SHA-256, and every entry is signature-checked at startup. A bad signature disables only what needs that entry, and the game runs on. This replaced raw addresses so an unknown game version degrades instead of crashing.
- **D5 Defer to other mods that own a patch site** (alttabfix, FOV fix, FDNYDeterministic). Owner: the old tools stay installed. The core detects an occupied site and leaves it alone rather than double-patching.
- **D6 Ghidra work goes through subagents**, read-only; the primary session never calls GhidrAssist MCP. Project rule (memory: loop owns Ghidra writes).

## Architecture

- **D7 Core services + self-registering feature modules, in one binary** (0.16.0, owner chose "option A").
  - **Rejected for now:** separate plugin DLLs ("option B": versioned C API, load-order waits, a runtime per DLL, a bigger test matrix). A is shaped to map onto B later: module descriptor = plugin entry, service headers = API.
  - **Proof:** `build.ps1 -Without *` builds and runs the core alone.
- **D8 Registry by COFF section (`.fdmod$a/$m/$z`), not a list or constructors.** A list in the core would make the core name features (R1). Constructors never run (D2). lld merges `$`-suffixed sections in name order, so the core walks pointers between its two markers (nulls skipped).
- **D9 Practice-only is declared by the feature (`cmd::SetPracticeOnly`), enforced by `profile`.** Previously `profile` hard-coded feature names, a service knowing features. The classification itself is the agent's default and still **awaits owner confirmation**.
- **D10 `camera` is a core service** owning `geCamera_SetAttributes`. `fov` is a filter on it, and `gfx` reads the view size from it. Before, `gfx` (a service) depended on `fov` (a feature).

## Drawing

- **D11 Text is GDI-rendered into one bitmap per panel, re-rendered only when it changes** (≥ `minFrames` apart). One blit or poly set per frame. Rejected:
  - `geEngine_Printf`: one ~29 µs blit per character, max 40 strings × 80 chars; a full box ran at 12 fps;
  - an external overlay window: no fullscreen or OBS capture, and a second UI thread.

  Evidence: `text_cutover/`, PROGRESS "In-frame text investigation".
- **D12 `geEngine_FlushPipeline` once before the frame's first immediate draw.** Without it, the cached 3D is drawn over the 2D inside EndFrame. This was the cause of the early "invisible bitmaps".
- **D13 Placement from the camera rect, never the window client size** (0.14.1). With dgVoodoo scaling, the window is the desktop size and the render is smaller, so right- and bottom-anchored panels landed off-frame.
- **D14 Translucency = `gfx::AlphaSurface`: 256-px 4444 tiles attached with `geWorld_AddBitmap`, drawn with `geEngine_RenderPoly`** (flags 1, z 1.0, −0.5 px offset). Spike: `alpha_spike/README.md`. Rejected or impossible:
  - untextured vertex alpha: D3DDrv ignores it;
  - engine-attached bitmaps: 2D decal handle only, crashes D3DDrv;
  - textures over 256 px: crash;
  - 32-bit ARGB: stored as 4444 anyway.

  Without a world (menus) a translucent panel falls back to an opaque decal. Tiles are re-added when the world changes.
- **D15 Text alpha is a parameter, not a separate method** (0.15.1). It costs the same, since the tile is blended either way. Strictly opaque drawing is the Decal (`STYLE_OPAQUE`).
- **D16 The watermark stays opaque** (owner), so it shows in menus too.
- **D17 Font: Consolas, non-antialiased, default 9 px (`fd_text_size`).**
  - Non-antialiased so every text pixel is exactly its colour: alpha keys on the background colour.
  - 9 px because the owner wanted smaller than 11.

## Features

- **D18 Teleporter writes position, view and zeroed motion at frame end; it does not use the game's Verb_Reset.** Verb_Reset drops held objects and the victim, resets health and zones, moves to an escape-mutated checkpoint, and SnapToFloor commits (0,1,0) without a floor. Owner: it only works on the map it was saved on. It has a single in-memory slot; stance is not saved.
- **D19 Collision-overlay output goes to `FDNYCore.log` (`glitch|` lines), no extra files** (owner).
- **D20 Speedrun options (static spawns, RescueBot off) re-implement FDNYDeterministic and stand down when it is installed** (D5). Replacing the standalone `.asi` is an open owner question.
- **D24 Trigger boxes come from live memory every level; there is no BSP scan or coordinate file** (owner). Triggers are the entities whose vtable is `trigger.vtbl`. The set is rescanned on a map change and about once a second. The exits' open flag is read every frame. Spike: `trigger_boxes/README.md`.
- **D25 Trigger classes are the game's own tests, not kind and not our own parsing rules:**
  - Kind is 8 for every trigger: the constructor writes it, so it can't separate exits.
  - Rescue zone = `trigger.escape`, the flag the game sets when a trigger binds `touch` → `tell player escape`.
  - Exit = a `touch:` line containing `tell player finalexit` or `tell player (finalexitloadmap`. These are the two has-binding tests in `PlayerRescueEnt::UpdateZonesAndWin`, which match by substring.
  - Open = `trigger.enabled`, set by `BoundsTrigger_SetEnabled(1)` when every victim is delivered.
  - An exit is also an escape trigger, and exit wins.
- **D26 World boxes are a `gfx` block (`SubmitWorldBoxes`), drawn in gfx's own late step before the text panels.**
  - Only gfx may call RenderPoly (R5).
  - A queued poly would land on top of the panels at EndFrame's flush. So gfx flushes the queued boxes itself, from an `OnFrameEndLate` subscriber registered before textdraw's.
  - Faces are clipped to the near plane (2 u) and the view, and drawn far to near.
  - One 64-px solid white 4444 tile per alpha level, tinted by vertex colour (the spike showed D3DDrv modulates textured polys).
- **D27 Trigger boxes are depth-tested by default; `fd_triggers_xray 1` draws them over walls.**
  - Depth-tested: queued polys (flags 0) with `geCamera_Project`'s z, so the world hides them.
  - X-ray: immediate polys, which skip the z-test.
  - Rejected: x-ray as the default. Big zones behind walls cover the view and hide the room you're in.
- **D28 A box around the camera is drawn at 25% instead of 50%.** The spawn often stands inside large zones, which tinted the whole view (`spike/training_bank.png`).
- **D29 Default key T (`fd_triggers_toggle`) through `binds::BindDefault`.**
  - The default applies before `FDNYCore.cfg`, so the player's own binds win. An unbound default is saved as `unbind t`.
  - The F-keys were rejected:
    - F2–F4, F6–F8 and F11 are the game's developer keys (F11 crashes, F8 wins the level);
    - F9, F10 and F12 are the collision overlay's;
    - F5 / F6 are suggested for the teleporter.
  - T isn't one of the game's controls (`GameControls::Construct`).
- **D30 A world-attached tile that settles without a driver handle is re-added** (0.17.0, gfx: world boxes and `AlphaSurface`).
  - The world can't be identified by its pointer alone: bank, loaded after Training, got Training's world address. The tiles looked attached but had no handle, so nothing was drawn and text panels would have stayed on the opaque fallback.
  - The handle check after the settle frames is the signal. Log line: `gfx: world box tile without a handle; re-adding`.
  - Rejected: re-attaching on `OnLevelLoad`. It races the first render of the new world, and the handle check also covers anything else that drops a texture.
- **D31 textdraw gains `CENTER` and `STYLE_TEXT_ONLY`** (fd_juggle, owner: "no background, 30 px below centre").
  - **`CENTER`:** the panel is centred horizontally (`dx` shifts it), and `dy` is measured from the view's vertical centre to the panel's top.
  - **`STYLE_TEXT_ONLY`:** the same `AlphaSurface` path, with background slot 0 keyed to alpha 0. The font is non-antialiased (D17), so only glyph pixels stay; there's no fringe.
  - **No world (menus):** it isn't drawn at all, rather than falling back to the opaque decal: a world-space hint has no meaning there, and a box would contradict "no background".
  - Existing anchors and styles are unchanged.
  - Rejected: a per-panel background-alpha parameter. It would be one more knob for no use beyond "none", and `fd_text_alpha` would have to be special-cased for it anyway.

## Process

- **D21 Tests are never weakened or deleted to pass.** An environmental failure (slow VM frame rate, a race in a read) is rerun and reported with evidence, for example the previous version failing identically.
- **D22 Spikes don't touch working code:** standalone probes (`probes/`) and probe tests (`tests/probes/`, outside the suite).
- **D23 Patterns live in the repo, enforced where possible** (this file, `CLAUDE.md`, `tools/arch_check.py`, the `new-feature` skill). Session memory is machine-local and doesn't transfer.
