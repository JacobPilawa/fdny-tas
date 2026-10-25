# Testing gaps closed (suite review item 5)

## `text_alpha` asserts the opacity

It used to take screenshots at `fd_text_fg_alpha` 255 and 96 and assert nothing about them.
Unpaused, the moving world swamped the signal: outside the panels, 255 against 96 differed
by 5.78, and two shots at the same setting by 5.06.

The shots are now taken with the world paused (Debug pipe `pause on`; the HUD panels keep
drawing). Inside the player HUD panel (168x58 at 8,52):

| Comparison | Measured | Asserted |
|---|---|---|
| 255 vs 96 | 4.56, then 4.45 and 4.45 on two reruns | >= 2 |
| 255 vs 255-again | 0.00 | <= 0.5 |

## The crash reporter, without the game

A unit test (`core/unit/test_crashlog.cpp`) installs the handler and causes an access
violation that Windows catches itself (`IsBadReadPtr` on 0x10). It checks the report's
exception line and data address, the registers, and a stack walk that reaches the test
program's own frames. The program carries on. PASS.

A first version ran this in the game through a Debug console command. The owner's efficiency
rule (2026-10-25) moved it to the unit tests, so no game launch and no command are needed.

## Stale descriptions

- `hud_player` said it draws through geEngine_Printf; it uses a textdraw panel (since 0.15).
- `glitch_records` used the pre-0.19 name `fd_hud_glitch`; it now gives `fd_hud_warp`, with
  the alias noted.

## Dropped after review

A rule that every core source names its tests (19 `// Tests:` lines, and an arch_check check
with 2 unit tests) was added, then removed. It added upkeep without catching anything the
review hadn't already found.
