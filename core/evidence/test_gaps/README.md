# Testing gaps closed (suite review item 5)

## `text_alpha` asserts the opacity

It used to take screenshots at `fd_text_fg_alpha` 255 and 96 and assert nothing about them
("compared in the evidence"). Unpaused, the moving world swamped the signal. Over a
region outside the panels, 255 against 96 differed by 5.78 and 255 against 255-again by
5.06, because the sky and the clock move between shots.

The shots are now taken with the world paused (Debug pipe `pause on`; the HUD panels keep
drawing). Inside the player HUD panel (168x58 at 8,52), measured:

| Comparison | Mean RGB difference | Asserted |
|---|---|---|
| 255 vs 96 | 4.56, then 4.45 and 4.45 on two reruns | >= 2 |
| 255 vs 255-again | 0.00 | <= 0.5 |
| outside the panels, any pair | 0.00 | (not asserted) |

PASS twice. Files: `fg255.png`, `fg96.png`, `text_alpha_result.json`.

## A crash-report test: `tests/crashlog.yaml`

Nothing tested the crash reporter (`crashlog.cpp`). The new Debug hook `fd_dbg_fault`
causes an access violation that Windows catches itself (`IsBadReadPtr` on 0x10). The
vectored handler reports it first, and the game carries on. The test asserts:

- the exception line, with code and data address;
- the registers;
- a stack walk that reaches FDNYCore's own frame;
- the game still in Training and drawing.

PASS. The log lines are in `crashlog_lines.txt`:

```
crashlog: exception c0000005 at 763844ba (KERNEL32.DLL+144ba); data 00000010
crashlog: eax=00000010 ebx=00000000 ...
crashlog:   [esp+01c] 7203ef48 (FDNYCore.asi+ef48)
...
crashlog: test fault handled, game continues (IsBadReadPtr=1)
```

## Every core service names its tests (R3)

`arch_check` required a `// Tests:` line only in feature modules. Now every core source
(`src/*.cpp`, 19 files) has one, naming game-driven tests or unit files, and `arch_check`
fails a missing line or a named test that doesn't exist. Two new unit tests check the rule.

## Stale descriptions

- `hud_player` said it draws through geEngine_Printf; it uses a textdraw panel (since 0.15).
- `glitch_records` used the pre-0.19 name `fd_hud_glitch`; it now gives `fd_hud_warp`, with
  the alias noted.
- The "B (was F9)" style notes elsewhere are deliberate history and stay.

## Checks after the change

- unit tests: 34 C++ checks and 26 Python tests, all PASS;
- `docs_registry` (the new Debug command is registered and docgen reads it): PASS;
- `crashlog`, `hud_player`, `glitch_records`: PASS.
