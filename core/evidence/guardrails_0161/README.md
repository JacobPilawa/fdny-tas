# Durable patterns: guardrails (0.16.1, 2026-10-22)

Owner: "how can I ensure future additions adhere to the patterns established so far
in a durable, transferrable manner".

What was added (all tracked in the repo):

| Piece | Where |
|---|---|
| Rules + "done means" + report format + owner decisions | `core/CLAUDE.md` (auto-loaded under core/) |
| Reasons + rejected alternatives (D1-D23) | `core/DECISIONS.md` |
| Enforced rules R1-R5 | `core/tools/arch_check.py`, run first by `build.ps1` and by `.githooks/pre-commit` |
| Enforced rule R7 | `-Wglobal-constructors -Wexit-time-destructors` (+ `-Werror`) in `build.ps1` |
| Procedure | `.claude/skills/new-feature/SKILL.md` |
| Templates | `core/templates/feature.cpp`, `core/templates/test.yaml` |
| Root pointer, git note corrected | `CLAUDE.md` |

## Verification

- **arch_check on the tree:** OK (32 core, 17 feature files, 7 modules).
- **Planted-violation test** (scratch copy of core): each planted violation was reported with its rule:
  - R1: a core file including `hud.h`;
  - R2: `tp` including `glitch.h`;
  - R3: a missing `// Tests:` line, and a feature without `FDCORE_MODULE`;
  - R4: `0x0044CD10` in code;
  - R5: `Id::genesis_draw_bitmap` in a feature.
- **R7:** a test file with `S g_s;` (constructor calling GetTickCount) fails with "declaration requires a global constructor".
  - A binary check (look for a `.ctors` section) was tried first and dropped: lld merges the constructor table into `.data`/`.rdata`, so it can't see it.
- **R7 found two real cases in the code**, both fixed:
  - `textdraw` `Panel g_panels[]` needed a constructor; it was benign because all-zero. Now `= {}`.
  - `glitch_bsp` `const double ON_EPSILON = OnEpsilon();` was never initialised in a no-CRT DLL, so it was 0 unless the compiler folded it. Now `constexpr` (0x3DCCCCCD = 0.1f).
  - `glitch_parity_fuzz` / `glitch_parity_log` stay identical after the fix; `hud_player`, `profile_run` and `text_alpha` PASS.
- **Template dry run:** both templates instantiated as `sample`.
  - The build was clean (arch_check: 8 modules) under every flag.
  - In-game: `module sample: enabled` PASS. Only the template's placeholder assertion failed, as intended.
  - Sample removed afterwards.
- **The dry run found one gap:** PowerShell 5.1 `-Encoding utf8` writes a BOM, which hid line 1 from the R3 check. arch_check now reads files as `utf-8-sig`.
- **Pre-commit hook:** `core.hooksPath = .githooks`. A staged `core/src` file including `hud.h` was refused ("[R1 core-includes-feature] ... pre-commit: arch_check failed", exit 1, HEAD unchanged). The file was removed afterwards.
