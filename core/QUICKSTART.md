# FDNYCore quickstart (0.18.1)

FDNYCore is one `.asi` plugin for *FDNY Firefighter: American Hero* that adds a
console command layer, practice tools and speedrun options. Everything is driven
from the game's own console. Deeper detail: `PROGRESS.md` (what was built and
why), `LOOP.md` (the backlog), `addresses.json` (every game address used).

## 1. Build

```powershell
cd C:\fdny-decomp\core
.\build.ps1                  # Debug and Release
.\build.ps1 -Config Release  # just one
```

Output: `core\build\<Debug|Release>\FDNYCore.asi`. Zig is bundled; the build
regenerates `src\gen\` from `addresses.json` and `VERSION`.

- **Release** is the one to play with.
- **Debug** adds the test pipe (`\\.\pipe\FDNYCore`) and benchmark commands; the
  test harness uses it.

## 2. Install

Copy `FDNYCore.asi` into the game folder (`C:\fdny-decomp\FDNY SP 1.1\`), next
to `FDNYFirefighter.exe`. The ASI loader already there picks it up at launch.
Back up the existing `FDNYCore.asi` first if you're replacing one.

The older plugins (FOV fix, alttabfix, FDNYDeterministic) can stay installed
alongside it.

Files the core writes in the game folder:

| File | What |
|---|---|
| `FDNYCore.cfg` | your settings, rewritten on every change; loaded at startup |
| `FDNYCore.log` | log, including collision-overlay `glitch|` lines |
| `profile_practice.cfg` / `profile_run.cfg` | optional, yours: run on `fd_profile` switch |

## 3. The console

Press **` (backtick / tilde)** in game to type; **Esc** closes it.

| Command | |
|---|---|
| `cvarlist` | every FDNYCore variable with its value and help |
| `cmdlist` | every FDNYCore command with its help |
| `<variable>` | show its value; `<variable> <value>` sets it |
| `echo <text>` | print text |
| `fd_writeconfig` | write `FDNYCore.cfg` now (it's also written on every change) |

Settings persist automatically: set something once and it's there next launch.

## 4. Key binds

```
bind f5 "fd_tp_save"
bind f6 "fd_tp_load"
unbind f5
bindlist
unbindall
```

Key names: `a`–`z`, `0`–`9`, `f1`–`f12`, `mouse1`–`mouse5`, `space`, `tab`,
`enter`, `shift`, `ctrl`, `alt`, `ins`, `del`, `home`, `end`, `pgup`, `pgdn`,
arrows (`uparrow` …), keypad (`kp_0` … `kp_9`, `kp_plus` …). Binds only fire
with the game focused and the console closed; backtick stays reserved for the
console. Binds are saved with the config.

## 5. Features

### Practice tools (refused while `fd_profile run`)

| | |
|---|---|
| `fd_hud_player 1` | top-left: position, velocity, XY speed, player state, screen state |
| `fd_hud_glitch 1` | top-right: collision-warp boxes (what you're touching, warp prediction) |
| `fd_glitch_hide` (**F9**) | show / hide the boxes |
| `fd_glitch_freeze` (**F10**) | freeze the boxes; logging continues |
| `fd_glitch_clear` (**F12**) | clear box history and the last warp |
| `fd_tp_save` | save position + view (pitch/yaw) |
| `fd_tp_load` | teleport back to it |
| `fd_triggers 1` / `fd_triggers_toggle` (**T**) | trigger boxes, 50%: **blue** rescue zone (drop-off for a carried victim), **red** final exit still closed, **green** exit open (every victim accounted for) |
| `fd_triggers_all 1` | also every other trigger, grey: room zones, hints, doors, sounds |
| `fd_triggers_xray 1` | trigger boxes show through walls (default: walls hide them) |
| `fd_triggers_list` | the map's triggers in the console: class, open/shut, name, box corners |
| `fd_juggle 1` | fire-juggling arrow, 30 px below the screen centre, no background: where a carried survivor would land if you passed out now |

Trigger boxes are read from the loaded map every level (no files). A box you are
standing in is drawn at 25%, so the whole view is tinted its colour. T is a default
bind: `bind t "..."` or `unbind t` overrides it.

Fire-juggling arrow (`fd_juggle`):
- **`-`**: the game saved your position as the drop spot this frame (it's following you).
- **An arrow**: the game didn't, so the spot is frozen somewhere else.
  - The arrow points at where the survivor lands: the spot, 30 units to -X. It is relative to
    your view, so up = straight ahead, in 8 directions.
  - Below it is the horizontal distance (`120u`), with ` ^` / ` v` when that point is more than
    48 units above or below you.
- **`o`**: the spot is frozen and the landing point is within 16 units.

The overlay doesn't know *why* the spot froze: it only compares the spot with where you
were last frame (the game copies that position when it saves). So everything that stops the
saving shows, including things nobody has documented yet. Known so far: fire contact, a
burn meter at 0.1 or more, O2 at 0.5 or less, being in the air (a jump shows the arrow for
its whole length, and landings blip it for a frame or two), and being passed out. Each
change is logged to `FDNYCore.log` with the burn, O2, flinch, state and on-ground values
(`juggle: frozen (...)` / `juggle: following (...)`). A save to anywhere other than your last
position is logged as `juggle: spot written elsewhere (...)` (the respawn does this when it
puts the spot on the checkpoint). Standing still, a spot frozen under your feet reads as
`-`; the survivor would land at your feet either way. Background: research.db KB article
`fire-juggling`.

Teleporter rules: one slot, in memory only (gone on exit); only works on the map
it was saved on ("saved on X, this is Y -- refused"); only inside a level.
Velocity is zeroed on arrival. Crouch/stance is not saved.

F9 / F10 / F12 only act while `fd_hud_glitch` is on, the game is focused and the
console is closed. The boxes' full detail goes to `FDNYCore.log`.

### Profiles and watermark

| | |
|---|---|
| `fd_profile practice` | default; everything allowed |
| `fd_profile run` | turns practice tools off and refuses them; remembers your practice values and restores them on `fd_profile practice` |
| `fd_watermark 1` | bottom-left: core version, profile, every non-default setting, other `.asi` plugins. Always on in `run` |

Put anything you want applied on a switch (binds, settings) in
`profile_run.cfg` / `profile_practice.cfg`.

### Speedrun options (allowed in `run`, shown on the watermark)

| | |
|---|---|
| `fd_static_spawns 1` | victims spawn at fixed spots instead of random |
| `fd_spawns <map> [spot ...]` | pin spots per map, one per victim in spawn order (`-1` = random). No spots = show the current pins |
| `fd_rescuebot 0` | the AI RescueBot never claims victims; chatter muted. Applies fully from the next level load |

### Quality of life

| | |
|---|---|
| `fd_fov <deg>` | horizontal FOV at 4:3, Hor+ for wider screens; `0` = classic FOV fix |
| `fd_fov_factor <x>` | multiplier used when `fd_fov` is 0 (default 1.4) |
| `fd_fovinfo` | show the game's and the camera's FOV |
| `fd_window_cursor 1` | cursor kept in the window only while focused (hold **O** to free it); `0` = game default |
| `fd_skip_intro 1` | skip the intro movies (next launch) |
| `fd_log_commands 0/1` | log every console command (default 1) |
| `fd_text_size <px>` | on-screen text height, 7–16 (default 9) |
| `fd_text_alpha <0-255>` | background opacity of the player HUD and collision boxes (default 160; 255 = solid). The watermark is always opaque |
| `fd_text_fg_alpha <0-255>` | text opacity on the same panels (default 255 = solid text). No extra cost: those panels are blended either way |
| `fd_textdraw` | list the on-screen text panels: shown/hidden, size, position, translucent/opaque (troubleshooting) |
| `fd_version` | read-only core version |

## 6. Suggested first session

```
fd_profile practice
fd_hud_player 1
fd_hud_glitch 1
bind f5 "fd_tp_save"
bind f6 "fd_tp_load"
```

Load Training, walk into a wall, watch the top-right boxes; F5 to mark a spot,
F6 to return. For a run: `fd_profile run` and check the watermark bottom-left.

## 7. Tests (VM only)

```powershell
$env:PATH = "C:\fdny-decomp\environment\local\tools\python;$env:PATH"
cd C:\fdny-decomp\core\harness
python -I run_test.py --suite                      # all of core/tests/*.yaml, Debug core
python -I run_test.py ..\tests\text_panels.yaml    # one test
python -I run_test.py --core Release ..\tests\*.yaml
python -I run_test.py --core none ..\tests\x.yaml  # baseline without the core
```

The harness launches the game, drives it, and backs up / restores the game
folder files it touches. Results and screenshots land in `core\runs\`. Last
result (0.18.1): 31/32 pass (Debug). The one failure, fire_juggle, read the pipe before a
test hook applied on a ~1 s frame; the test now waits for the hook and passed 3/3
(`evidence/fire_juggle_overlay/observed/README.md`).
glitch_box_cost / text_panels (VM frame rate) and state_snapshot (roaming-victim race)
can still fail on a slow VM.

## 8. Known gaps

- Player HUD says "crouched (3)" where the collision box says "Standing" for the
  same state; research says 3 = standing (HUD label likely wrong, awaiting the
  owner).
- The practice-only list (above) is a default; the owner still needs to confirm it.
- Teleporter: single slot, no stance.
- In-frame text verified windowed through dgVoodoo only; real fullscreen and OBS
  game capture untested.
- Fire-juggling arrow:
  - It uses the shared 9 px font (`fd_text_size`) with no outline, so it can be hard to
    read on bright fire.
  - It aims at the spot before the game's floor search (the survivor is dropped onto the
    first floor below it).
  - Following vs frozen is only visible while you move (standing still they look the same).
- Trigger boxes: a box you stand in also tints the game's own HUD (it is drawn
  before the core's panels). The `triggers` test opens the bank exit with the
  RescueBot (`runrescuebot`), so the carry-and-deliver path is not exercised by a test.
- Translucent panels need a loaded world; in menus they are drawn opaque. Their
  colours are 4 bits per channel (the driver's only alpha format), so dim greys band.

## For module authors: core and feature modules

The core (`src/`) is services only; every feature is a self-registering module
in `src/features/`. The core never names a feature, so a feature can be dropped
from the build (`.\build.ps1 -Without glitch`, or `-Without *` for the core alone)
and everything else still compiles and runs.

| Core service | What a module gets |
|---|---|
| `addr` | address table: verified addresses by Id (never raw addresses) |
| `hooks` | `Create` (inline hook in a group), `OnFrameBegin` / `OnFrameEnd` / `OnLevelLoad`; `OnFrameEndLate` is for drawing |
| `cmd`, `console`, `binds` | variables, commands, config persistence (`RegisterConfigWriter`), `SetPracticeOnly` (refused in `fd_profile run`), console output; `binds::BindDefault(key, line)` for a feature's default key (the player's binds win) |
| `camera` | view size; one FOV filter (`fov` uses it); a world-transform override (`SetWorldOverride`, `LastWorldXForm`: the free camera, applied at the world render) |
| `pause` | `Set(on, why)` / `Active()`: skip the simulation step and pause the game clock (input blocked, ~60 fps cap); ends itself on leaving the level. `textdraw::HideWhilePaused` for view-relative panels |
| `state` | the frame's player snapshot |
| `gfx`, `textdraw` | in-frame drawing (below) |
| `profile` | run/practice gates + watermark (reads `SetPracticeOnly`) |
| `testpipe` (Debug) | `Register("<name>", handler)` for harness requests |
| `log`, `crashlog`, `patches`, `levelwatch` | logging, crash reports, the core's startup patches, level events |

A new feature is one file (or a few) in `src/features/`:

```cpp
#include "modules.h"
namespace fdcore::myfeature {
bool Init() { /* register cvars/commands, hooks::OnFrameEnd(...), ... */ return true; }
}
// name, required address Ids (or nullptr, 0), DllMain-phase step (or nullptr), init, order
FDCORE_MODULE(myfeature, "myfeature", nullptr, 0, nullptr, &fdcore::myfeature::Init, 70)
```

At startup the log lists `modules: N registered (...)` and one
`module <name>: enabled | disabled (reason)` line each.

## For module authors: drawing blocks (`src/gfx.h`)

| Block | Use it for |
|---|---|
| `gfx::Canvas` | GDI text on a monospace grid (the shared `fd_text_size` font) → 32-bit pixels |
| `gfx::Decal` | opaque bitmap (`geEngine_DrawBitmap`); works everywhere, drawn on top |
| `gfx::AlphaSurface` | per-pixel alpha (256 px 4444 tiles on the rendered world, `geEngine_RenderPoly`); only changed tiles re-upload; `Draw` returns false without a world, so fall back to a Decal |
| `gfx::SolidRect` | untextured opaque rectangle |
| `gfx::SubmitWorldBoxes` | translucent world-space boxes (`WorldBox`: min/max, colour, alpha, alpha while the camera is inside), depth-tested or x-ray. Submit from `OnFrameEnd` (static array); gfx draws them in its late step, under the text panels. `LastWorldBoxStats` for tests |
| `gfx::BeginDraw` / `World` / `ViewSize` | per-frame flush (once), the frame's world, render size |

`textdraw` anchors: `TOP_LEFT`, `TOP_RIGHT`, `BOTTOM_LEFT`, and `CENTER` (centred horizontally; `dy` from the view's
vertical centre to the panel top). Styles: `STYLE_TRANSLUCENT`, `STYLE_OPAQUE`, and `STYLE_TEXT_ONLY` (no
background at all; not drawn without a world, e.g. in menus).

Text panels should normally go through `textdraw::Create(...)` with `STYLE_TRANSLUCENT`
(default) or `STYLE_OPAQUE`, rather than using the blocks directly.
