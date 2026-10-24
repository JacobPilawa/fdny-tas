"""Spike probe (demos / ghosts, 2026-10-23): static scan of FDNYFirefighter.exe and
Genesis.dll for any demo / record / replay facility, the clock, RNG and input APIs they
import, and Genesis.dll's actor exports (for a translucent ghost). Read-only: it only
reads the two files. Ghidra was not open, so this replaces the GhidrAssist lookup.

    python -I core/probes/demo_scan.py > core/evidence/demos/static_scan.txt

References to a string / import are found by their address bytes in executable
sections (push imm32, mov r,imm32, call [iat], mov r,[iat] ...), and attributed to the
nearest preceding known function (research.db symbols for the exe, exports for
Genesis.dll); "far" marks a reference more than 0x600 bytes past that name.
"""
import bisect
import os
import re
import sqlite3
import struct
import sys

import pefile

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
GAME = os.path.join(REPO, "FDNY SP 1.1")
KEYWORDS = re.compile(r"demo|record|playback|play back|replay|timedemo|benchmark|ghost|rewind|journal|"
                      r"capture|frame ?rate|\bfps\b", re.I)
IMPORTS = {"timeGetTime", "QueryPerformanceCounter", "QueryPerformanceFrequency", "GetTickCount",
           "GetSystemTime", "GetSystemTimeAsFileTime", "GetLocalTime", "timeBeginPeriod", "timeEndPeriod",
           "Sleep", "rand", "srand", "CreateThread", "_beginthread", "_beginthreadex", "DirectInput8Create",
           "DirectInputCreateA", "GetAsyncKeyState", "GetKeyState", "GetKeyboardState", "GetCursorPos",
           "PeekMessageA", "GetMessageA", "SetCursorPos", "time", "clock", "_ftime"}
EXPORTS = re.compile(r"^geActor_|^geWorld_(Add|Remove)Actor|Alpha|Blend|Light|Fog|Time|Clock|Demo|Record|Play",
                     re.I)


def strings(data, minlen=4):
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, data):
        yield m.start(), m.group().decode("ascii")


class Image:
    def __init__(self, path, names):
        self.pe = pefile.PE(path)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.data = self.pe.get_memory_mapped_image()
        self.code = [(s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize) for s in self.pe.sections
                     if s.Characteristics & 0x20000000]
        self.names = sorted(names)
        self.keys = [a for a, _ in self.names]

    def refs(self, va):
        pat = struct.pack("<I", va)
        out = []
        for lo, hi in self.code:
            i = self.data.find(pat, lo, hi)
            while i >= 0:
                out.append(self.base + i)
                i = self.data.find(pat, i + 1, hi)
        return out

    def owner(self, va):
        k = bisect.bisect_right(self.keys, va) - 1
        if k < 0:
            return "?"
        a, n = self.names[k]
        return f"{n}+0x{va - a:x}" + (" (far)" if va - a > 0x600 else "")


def exe_names():
    db = sqlite3.connect(os.path.join(REPO, "research", "research.db"))
    return [(int(a, 16), n) for a, n in db.execute("select address, name from symbols where kind='function' "
                                                   "and address like '0x%'")]


def scan(label, path, names):
    img = Image(path, names)
    print(f"\n######## {label}: {path}  (base 0x{img.base:08x})")
    print("\n== A. keyword strings (and code references)")
    n = 0
    for off, s in strings(img.data):
        if not KEYWORDS.search(s):
            continue
        va = img.base + off
        refs = img.refs(va)
        n += 1
        where = ", ".join(f"0x{r:08x} {img.owner(r)}" for r in refs[:6]) or "no code reference"
        print(f"  0x{va:08x}  {s[:90]!r}\n      -> {where}" + (f" (+{len(refs) - 6} more)" if len(refs) > 6 else ""))
    print(f"  ({n} keyword strings)")
    print("\n== C/D. imports of interest (call sites = IAT slot address in code)")
    for d in getattr(img.pe, "DIRECTORY_ENTRY_IMPORT", []):
        for imp in d.imports:
            name = imp.name.decode() if imp.name else f"ord{imp.ordinal}"
            if name not in IMPORTS:
                continue
            refs = img.refs(imp.address)
            owners = sorted({img.owner(r).split("+")[0] for r in refs})
            print(f"  {d.dll.decode():14s} {name:26s} {len(refs):3d} site(s): {', '.join(owners)[:300]}")
    return img


def main():
    exe = scan("EXE", os.path.join(GAME, "FDNYFirefighter.exe"), exe_names())
    gpe = pefile.PE(os.path.join(GAME, "Genesis.dll"))
    base = gpe.OPTIONAL_HEADER.ImageBase
    exports = [(base + e.address, e.name.decode()) for e in gpe.DIRECTORY_ENTRY_EXPORT.symbols if e.name]
    gen = scan("GENESIS", os.path.join(GAME, "Genesis.dll"), exports)
    print("\n== B. Genesis.dll exports of interest")
    names = sorted(n for _, n in exports if EXPORTS.search(n))
    for i in range(0, len(names), 4):
        print("  " + "  ".join(f"{n:34s}" for n in names[i:i + 4]))
    print(f"  ({len(names)} of {len(exports)} exports)")
    print("\n== B2. which of those the exe imports from Genesis.dll")
    used = sorted(imp.name.decode() for d in exe.pe.DIRECTORY_ENTRY_IMPORT if d.dll.lower() == b"genesis.dll"
                  for imp in d.imports if imp.name and EXPORTS.search(imp.name.decode()))
    print("  " + ", ".join(used))
    _ = gen
    return 0


if __name__ == "__main__":
    sys.exit(main())
