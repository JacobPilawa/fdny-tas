# FDNY collision-warp overlay

A live, click-through overlay for FDNY Firefighter's out-of-bounds "collision
warp". It shows what the warp would do, why a frame did or didn't warp, and
whether the **next** frame can warp you.

Background: Genesis.dll keeps one global "last collision hit" slot. A player
sweep that starts inside solid and leaves a solid cell reports a hit without
writing a new one, so it is handed whatever someone else (victim, fire, hose,
engine) last wrote. If that plane isn't a floor and the step-up/forward sweeps
miss, the step code moves you there. Full rules: `WARP_CONDITIONS.md`,
`STALE_HIT_GEOMETRY.md`, `WRITERS_AND_ORDER.md`; KB article
`fdny-oob-warp-stale-impact`.

## Running it

```
python -m pip install pymem
python fdny_collision_overlay.py            # start the game first, then this
python fdny_collision_overlay.py --demo     # synthetic data, no game needed
```

Options: `--windowed` (normal window, e.g. second monitor), `--corner tl|tr|bl|br`,
`--maps DIR` (the game's `.bsp` folder if it isn't `<game>\Data\Maps`).

To draw over a fullscreen game, copy `dgVoodoo.overlay.conf` over the game's
`dgVoodoo.conf` (it only changes `FullscreenAttributes = fake`).

Hotkeys: **F9** show/hide, **F10** freeze the display, **F12** clear history.
The overlay never takes focus, because focusing another window pauses FDNY's
mission clock.

Closing the overlay, pressing Ctrl+C or closing its console window removes the
hooks from the game. If the tool is killed without cleaning up (Task Manager, a
crash), the next run finds its own leftover hooks, restores the original bytes
and carries on, so the game doesn't need a restart. It only refuses if a hook
site was patched by something else, such as another tool attached to the game.

## The boxes

| box | shows |
|---|---|
| WARP | stance, ground and position on one line (stance reads **Bugged** in orange during the crouchbug, see below); one status line for the **next** frame, worked out from the map (see below); and, only while you're inside solid, the **Target**: where a warp would send you and who put that spot there. In bounds your own movement rewrites the slot every frame, so there is no target to show. |
| LAST WARP | where the last warp took you and where from |
| HISTORY | the last 10 hits that could have warped you (not a floor, still in the slot when your sweep ran) and the warps |

The WARP status line says one of:

| status | meaning |
|---|---|
| In bounds - no warp possible | you're not inside solid |
| In solid - 2.2 u too high to warp | a warp needs the top of your box within about 1 unit above the underside of the solid you're in; drop this much |
| In solid - nothing here to line up with | the solid you're in has no underside above you |
| In solid - lined up, start moving to warp | standing still never warps |
| In solid - lined up, move toward open space | your move this frame doesn't leave the solid |
| In solid - lined up, but blocked above / ahead | the step-up or forward check hits something |
| In solid - lined up, but a floor stops it | the step-down lands on a real floor |
| LINED UP - waiting for a wall or slope target | everything lines up but the target is a floor, which never warps |
| LINED UP - target is your own spot (tiny move) | it would warp you onto your own hit this frame |
| WARP NEXT FRAME if you keep moving | everything lines up |

### The crouchbug ("Bugged")

The game stores your stance separately from the height of your collision box.
Camera height and speed follow the stance; collision, and so the warp, follow
the box (top 85 above you standing, 25 crouched). Walking into a climb volume
while crouched and back out of it (or jumping off it), or respawning at your
checkpoint while crouched, sets the stance back to standing but leaves the
crouched box. The stance column then reads **Bugged**: you see and move as if
standing, but your real head is 45 units below the camera. The WARP status
uses the real box, so trust it over the screen. Crouching twice on the ground
fixes it. The log notes when it starts and ends. Details: research.db H235-H237,
KB `fdny-oob-warp-stale-impact` update (e).

Everything else (function names, return addresses, plane values, the gap and
sweep-by-sweep details, the breakdown of each warp, and what the WARP status
predicted just before it) is printed to the console you started it from.
Nothing is saved to disk; copy it from the console if you want to keep it.
The last line says how many warps were predicted.

## How far to trust it

- **The WARP prediction's geometry test is a literal port of Genesis' box-vs-BSP code.**
  Run against the real Genesis.dll code (emulated) on 25,500 random sweeps
  across all 17 shipped maps, it matches every result: miss / hit / reuse,
  plus the hit plane.
- It covers **world geometry only**. Doors, lifts and other moving brush
  models aren't included.
- It predicts from your current movement. If you change direction or speed
  before the next frame, the prediction changes too.
- The hooks and the frame analysis were tested in an emulator against the
  real hook sites. **The tool hasn't been run against the live game yet.**
  The first live session is the real test, and its console output will show
  whether the predictions held.

## Speedrun timing note

- The tool patches the game's code while it runs. Use it for practice and
  research, not for submitted runs.
- **Installing the hooks** freezes the game for a few milliseconds.
- **While attached,** each collision trace costs a few extra instructions:
  roughly microseconds per frame. The ring buffer is read from outside the
  game.
- **On exit,** the original bytes are put back. The small code block stays
  allocated so nothing can jump into freed memory.
- **Game logic is untouched.** No game value is written; the hooks only
  record.
