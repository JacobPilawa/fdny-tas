"""Architecture check for FDNYCore: the patterns the core is built on, enforced.

    python -I core/tools/arch_check.py            # source rules (build.ps1 runs this first)

Exit 0 = clean, 1 = violations (each printed with its rule id; the rules and
their reasons are in core/CLAUDE.md and core/DECISIONS.md). Fix the code, not
the rule; a rule changes only with an owner-approved entry in DECISIONS.md.
"""
import os
import re
import struct
import sys

CORE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SRC = os.path.join(CORE, "src")
FEAT = os.path.join(SRC, "features")
TESTS = os.path.join(CORE, "tests")

# R5: the Genesis drawing / bitmap API is gfx's. Allowed elsewhere only here (reason).
DRAW_IDS = re.compile(r"Id::genesis_(draw_bitmap|render_poly|bitmap_\w+|add_bitmap|remove_bitmap|"
                      r"world_add_bitmap|world_remove_bitmap|flush_pipeline|printf)\b")
DRAW_ALLOWED = {
    "gfx.cpp": "the drawing service itself",
    "bench.cpp": "Debug benchmark of the raw engine paths (in-frame text investigation)",
}
# R4: raw game addresses (8-digit hex in the exe or Genesis.dll image ranges).
ADDR = re.compile(r"\b0[xX]([0-9A-Fa-f]{8})\b")
RANGES = ((0x00400000, 0x0060FFFF), (0x10000000, 0x103FFFFF))
# R4b: code entries are reached through the address table, never by module base + RVA.
BASE_PLUS = re.compile(r"ModuleBase\([^)]*\)\s*\+\s*0[xX][0-9A-Fa-f]+")

violations = []


def fail(rule, path, line, msg):
    violations.append(f"[{rule}] {os.path.relpath(path, CORE)}:{line}: {msg}")


def strip_comments(text):
    """Comments (and string literals) out, line numbers kept."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append('""')
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def files(d, exts=(".cpp", ".h")):
    return sorted(os.path.join(d, f) for f in os.listdir(d) if f.endswith(exts))


def group(name):
    """Feature group: glitch_analysis.cpp -> glitch."""
    return os.path.splitext(name)[0].split("_")[0]


def includes(text):
    return [(i + 1, m.group(1)) for i, l in enumerate(text.splitlines())
            for m in [re.match(r'\s*#include\s+"([^"]+)"', l)] if m]


def main():
    core_files = files(SRC)
    feat_files = files(FEAT)
    feat_headers = {os.path.basename(f) for f in feat_files if f.endswith(".h")}

    # R1: the core never includes a feature header.
    for f in core_files:
        for ln, inc in includes(open(f, encoding="utf-8-sig").read()):
            if os.path.basename(inc) in feat_headers or inc.startswith("features/"):
                fail("R1 core-includes-feature", f, ln, f'core file includes feature header "{inc}"')

    # R2: a feature includes core headers and its own group's headers only.
    for f in feat_files:
        g = group(os.path.basename(f))
        for ln, inc in includes(open(f, encoding="utf-8-sig").read()):
            b = os.path.basename(inc)
            if b in feat_headers and group(b) != g:
                fail("R2 feature-includes-feature", f, ln, f'"{inc}" belongs to feature "{group(b)}"')

    # R3: one FDCORE_MODULE per feature group, in a file that names its tests.
    modules = {}
    for f in feat_files:
        text = open(f, encoding="utf-8-sig").read()
        for m in re.finditer(r"^FDCORE_MODULE\((\w+)", text, re.M):
            modules.setdefault(group(os.path.basename(f)), []).append((f, text))
    for g in sorted({group(os.path.basename(f)) for f in feat_files}):
        regs = modules.get(g, [])
        if len(regs) != 1:
            fail("R3 one-module-per-feature", FEAT, 0, f'feature "{g}" has {len(regs)} FDCORE_MODULE (want 1)')
            continue
        f, text = regs[0]
        m = re.search(r"^// Tests: (.+)$", text, re.M)
        if not m:
            fail("R3 module-names-tests", f, 1, 'missing "// Tests: tests/<name>.yaml ..." line')
            continue
        for t in m.group(1).split():
            if not os.path.exists(os.path.join(CORE, t.rstrip(","))):
                fail("R3 module-names-tests", f, 1, f"named test {t} does not exist")

    # R4 / R5 on code (comments and strings removed). A line may carry an explicit,
    # reasoned exception: "// arch-allow: R4 <why>" (reviewed like any other code).
    for f in core_files + feat_files:
        raw_lines = open(f, encoding="utf-8-sig").read().splitlines()
        code = strip_comments("\n".join(raw_lines))
        name = os.path.basename(f)
        for i, l in enumerate(code.splitlines(), 1):
            allow = re.search(r"arch-allow: (R\d+) \S", raw_lines[i - 1]) if i <= len(raw_lines) else None
            if allow:
                continue
            for m in ADDR.finditer(l):
                v = int(m.group(1), 16)
                if any(lo <= v <= hi for lo, hi in RANGES):
                    fail("R4 raw-address", f, i, f"{m.group(0)} looks like a game address: use addresses.json")
            if BASE_PLUS.search(l):
                fail("R4 raw-address", f, i, "module base + RVA: add an addresses.json entry instead")
            if DRAW_IDS.search(l) and name not in DRAW_ALLOWED:
                fail("R5 drawing-is-gfx", f, i, "Genesis drawing/bitmap API outside gfx: use gfx:: / textdraw::")

    # R8: the draw order is gfx's (D40). Only gfx subscribes to the late frame step; everything
    # else draws from a gfx layer (gfx::OnLayer / SubmitWorldBoxes / textdraw).
    for f in core_files + feat_files:
        if os.path.basename(f) in ("gfx.cpp", "hooks.cpp", "hooks.h"):
            continue
        code = strip_comments(open(f, encoding="utf-8-sig").read())
        for i, l in enumerate(code.splitlines(), 1):
            if re.search(r"\bOnFrameEndLate\s*\(", l):
                fail("R8 draw-order-is-gfx", f, i, "late-frame subscriber outside gfx: draw from gfx::OnLayer(...)")

    # R9: the key sheet (README.md, tools/docgen.py) is built from the source (D41). Every
    # feature module says what it is for the player, and a feature that reads keys itself
    # (not through a bind) declares them with binds::DescribeKey, so the sheet lists them.
    for g, regs in sorted(modules.items()):
        if len(regs) == 1 and not re.search(r"^// User: .+ -- .+$", regs[0][1], re.M):
            fail("R9 sheet-from-source", regs[0][0], 1, 'missing "// User: <Title> -- <one sentence>" line')
    for f in feat_files:
        code = strip_comments(open(f, encoding="utf-8-sig").read())
        if re.search(r"\bGetAsyncKeyState\s*\(", code) and not re.search(r"\bDescribeKey\s*\(", code):
            fail("R9 sheet-from-source", f, 1, "reads keys itself (GetAsyncKeyState) but declares none with "
                 "binds::DescribeKey: the key sheet would miss them")

    # Not here, enforced by the compiler instead (build.ps1 flags, -Werror):
    #   R6 Debug-only testpipe::Register: declared only in Debug, a stray call breaks Release.
    #   R7 no static constructors / exit-time destructors (no CRT runs them):
    #      -Wglobal-constructors -Wexit-time-destructors.

    if violations:
        print(f"arch_check: {len(violations)} violation(s)")
        for v in violations:
            print("  " + v)
        print("rules and reasons: core/CLAUDE.md, core/DECISIONS.md")
        return 1
    print(f"arch_check: OK ({len(core_files)} core, {len(feat_files)} feature files, "
          f"{len(modules)} feature modules)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
