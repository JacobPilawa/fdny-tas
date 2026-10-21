"""Address-table tooling (build/VM-side only; never shipped).

  python -I addrgen.py sigs   <exe>        fill missing 'sig' fields in addresses.json
  python -I addrgen.py header <out.h>      emit the compiled-in table for the core
  python -I addrgen.py text   <out.txt>    emit the flat format the Debug override reads

Signature = the bytes of one real instruction in the exe's code that embeds
the entry's static root address as an absolute operand (the exe has no
relocations and always loads at 0x400000). The core compares those bytes in
memory at startup; a match proves the global is where the table says.
"""
import json
import os
import struct
import sys

CORE_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
TABLE = os.path.join(CORE_DIR, "addresses.json")
MODULE_IDS = {"exe": 0, "genesis": 1, "field": 2}  # field = struct offset (rva), no signature
TYPE_IDS = {"i32": 0, "u32": 1, "ptr": 2, "u8": 3, "f32": 4, "i16": 5}
MAX_CHAIN = 6
MAX_SIG = 16


def load_table():
    with open(TABLE, encoding="utf-8-sig") as f:
        return json.load(f)


def save_table(t):
    with open(TABLE, "w", encoding="utf-8", newline="\n") as f:
        json.dump(t, f, indent=2)
        f.write("\n")


# --- PE parsing --------------------------------------------------------------
def pe_sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = struct.unpack_from("<H", data, pe + 20)[0]
    image_base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    out = []
    for i in range(nsec):
        off = pe + 24 + opt + i * 40
        name = data[off:off + 8].rstrip(b"\0").decode("latin-1")
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, off + 8)
        chars = struct.unpack_from("<I", data, off + 36)[0]
        out.append((name, va, vsize, raw, rsize, chars))
    return image_base, out


# Instruction forms whose 32-bit absolute operand directly follows these
# opcode bytes: (prefix bytes before the address, total instruction length).
def _forms():
    f = {}
    for op in (0xA0, 0xA1, 0xA2, 0xA3):         # mov al/eax,[abs] / mov [abs],al/eax
        f[(op,)] = 5
    for op in range(0xB8, 0xC0):                # mov r32, imm32 (address as immediate)
        f[(op,)] = 5
    modrm_abs = [0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D]
    for op in (0x8B, 0x89, 0x8D, 0x39, 0x3B, 0x01, 0x03, 0x29, 0x2B, 0x85, 0xFF):
        for m in modrm_abs:
            f[(op, m)] = 6
    for op, extra in ((0x80, 1), (0x83, 1), (0xC6, 1), (0x81, 4), (0xC7, 4)):
        for m in modrm_abs:
            f[(op, m)] = 6 + extra
    for m in modrm_abs:                         # movzx/movsx r32, byte/word [abs]
        for op2 in (0xB6, 0xB7, 0xBE, 0xBF):
            f[(0x0F, op2, m)] = 7
        f[(0xD9, m)] = 6                        # fld/fst dword [abs]
    return f


FORMS = _forms()


def find_sig(data, root_va):
    """First code site whose instruction embeds root_va. Returns (rva, bytes)."""
    image_base, sections = pe_sections(data)
    needle = struct.pack("<I", root_va)
    for name, va, vsize, raw, rsize, chars in sections:
        if not chars & 0x20000000:              # IMAGE_SCN_MEM_EXECUTE
            continue
        blob = data[raw:raw + rsize]
        start = 0
        while True:
            i = blob.find(needle, start)
            if i < 0:
                break
            start = i + 1
            for plen in (3, 2, 1):
                key = tuple(blob[i - plen:i]) if i >= plen else None
                if key in FORMS:
                    n = FORMS[key]
                    # instruction + up to 6 bytes of following context (specificity for a future version scan)
                    ins = blob[i - plen:i - plen + min(n + 6, MAX_SIG)]
                    return va + i - plen, bytes(ins)
    return None, None


def rva_to_file(sections, rva):
    for name, va, vsize, raw, rsize, chars in sections:
        if va <= rva < va + max(vsize, rsize):
            return raw + rva - va
    return None


def export_sig(dll_path, rva):
    """Signature for a hooked export: its first MAX_SIG code bytes."""
    data = open(dll_path, "rb").read()
    _, sections = pe_sections(data)
    off = rva_to_file(sections, rva)
    return bytes(data[off:off + MAX_SIG])


def cmd_sigs(exe):
    data = open(exe, "rb").read()
    image_base, _ = pe_sections(data)
    genesis = os.path.join(os.path.dirname(exe), "Genesis.dll")
    t = load_table()
    for vname, v in t["versions"].items():
        for name, e in v["entries"].items():
            if e.get("sig") or e["module"] == "field":
                continue
            if e["module"] == "genesis" and e.get("export"):
                rva = int(e["rva"], 16)
                ins = export_sig(genesis, rva)
                e["sig"] = {"rva": e["rva"], "bytes": ins.hex(" ").upper(),
                            "ref": f"first {len(ins)} bytes of Genesis.dll export {e['export']}"}
                print(f"{vname}:{name}: Genesis+{rva:#x}  {ins.hex(' ').upper()}")
                continue
            if e.get("kind") == "code":
                # A game function we call or hook: signature = its first bytes.
                rva = int(e["rva"], 16)
                ins = export_sig(exe if e["module"] == "exe" else genesis, rva)
                e["sig"] = {"rva": e["rva"], "bytes": ins.hex(" ").upper(),
                            "ref": f"first {len(ins)} bytes of the function at {image_base + rva:#010x}"}
                print(f"{vname}:{name}: code {image_base + rva:#010x}  {ins.hex(' ').upper()}")
                continue
            if e["module"] != "exe":
                continue
            root = image_base + int(e["rva"], 16)
            rva, ins = find_sig(data, root)
            if rva is None:
                print(f"{vname}:{name}: no code reference to {root:#x} found")
                continue
            e["sig"] = {"rva": f"{rva:#x}", "bytes": ins.hex(" ").upper(),
                        "ref": f"instruction at {image_base + rva:#010x} embeds {root:#010x}"}
            print(f"{vname}:{name}: {image_base + rva:#010x}  {ins.hex(' ').upper()}")
    save_table(t)


# --- emitters ------------------------------------------------------------------
def ordered_names(t):
    """Stable id order shared by all versions (first version's order, then extras)."""
    names = []
    for v in t["versions"].values():
        for n in v["entries"]:
            if n not in names:
                names.append(n)
    return names


def cident(name):
    return name.replace(".", "_")


def cmd_header(out):
    t = load_table()
    names = ordered_names(t)
    L = ["// GENERATED by core/tools/addrgen.py from core/addresses.json -- do not edit.",
         "#pragma once", "#include <stdint.h>", "", "namespace fdcore::addr {", "",
         "enum class Id : int {"]
    L += [f"    {cident(n)}," for n in names]
    L += ["    Count", "};", "",
          f"constexpr int kMaxChain = {MAX_CHAIN};", f"constexpr int kMaxSig = {MAX_SIG};", "",
          "struct Entry {", "    const char* name;", "    uint8_t module;   // 0 exe, 1 Genesis.dll",
          "    uint8_t type;", "    uint8_t derefRoot;", "    uint8_t nchain;", "    uint32_t rva;",
          f"    uint32_t chain[kMaxChain];", "    uint32_t sigRva;    // 0 = no signature",
          "    uint8_t sigLen;", f"    uint8_t sig[kMaxSig];",
          "    const char* exportName;  // Genesis.dll export (resolved by name, cross-checked vs rva)",
          "};", "",
          "struct Version {", "    const char* name;", "    const char* exeSha256;",
          "    const char* genesisSha256;", "    Entry entries[static_cast<int>(Id::Count)];", "};", ""]
    L.append(f"inline constexpr int kVersionCount = {len(t['versions'])};")
    L.append("inline const Version kVersions[kVersionCount] = {")
    for vname, v in t["versions"].items():
        L.append(f'    {{"{vname}", "{v["exe_sha256"]}", "{v["genesis_sha256"]}", {{')
        for n in names:
            e = v["entries"].get(n)
            if e is None:
                L.append(f'        {{"{n}", 0, 0, 0, 0, 0, {{0}}, 0, 0, {{0}}, nullptr}},  // absent in this version')
                continue
            chain = [int(c, 16) for c in e.get("chain", [])]
            assert len(chain) <= MAX_CHAIN, n
            sig = e.get("sig") or {}
            sb = bytes.fromhex(sig["bytes"].replace(" ", "")) if sig else b""
            assert len(sb) <= MAX_SIG, n
            L.append(
                f'        {{"{n}", {MODULE_IDS[e["module"]]}, {TYPE_IDS.get(e["type"], 1)}, '
                f'{0 if e.get("deref_root") is False else 1}, {len(chain)}, {e["rva"]}, '
                f'{{{", ".join(hex(c) for c in chain) or "0"}}}, '
                f'{sig.get("rva", "0")}, {len(sb)}, {{{", ".join(hex(b) for b in sb) or "0"}}}, '
                f'{chr(34) + e["export"] + chr(34) if e.get("export") else "nullptr"}}},')
        L.append("    }},")
    L += ["};", "", "} // namespace fdcore::addr", ""]
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L))
    print(f"wrote {out}: {len(names)} entries x {len(t['versions'])} version(s)")


def cmd_text(out):
    """Flat override format, one record per line, space separated:
         version <name> <exe_sha256> <genesis_sha256>
         entry <name> <module> <type> <derefRoot> <rva> <sigRva> <sighex|-> <export|-> <chain...>
    """
    t = load_table()
    L = ["# FDNYCore address-table override (Debug builds only). Generated by addrgen.py text."]
    for vname, v in t["versions"].items():
        L.append(f"version {vname} {v['exe_sha256']} {v['genesis_sha256']}")
        for n, e in v["entries"].items():
            sig = e.get("sig") or {}
            sighex = sig["bytes"].replace(" ", "") if sig else "-"
            chain = " ".join(e.get("chain", []))
            L.append(f"entry {n} {MODULE_IDS[e['module']]} {TYPE_IDS.get(e['type'], 1)} "
                     f"{0 if e.get('deref_root') is False else 1} {e['rva']} {sig.get('rva', '0x0')} "
                     f"{sighex} {e.get('export') or '-'} {chain}".rstrip())
    with open(out, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(L) + "\n")
    print(f"wrote {out}")


if __name__ == "__main__":
    cmd, arg = sys.argv[1], sys.argv[2]
    {"sigs": cmd_sigs, "header": cmd_header, "text": cmd_text}[cmd](arg)
