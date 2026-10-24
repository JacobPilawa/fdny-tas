"""Address table access by name (core/addresses.json), shared with the core."""
import json
import os
import struct

import pymem.exception

TABLE_PATH = os.path.join(os.path.dirname(__file__), "..", "..", "addresses.json")

_TYPES = {"i32": ("<i", 4), "u32": ("<I", 4), "ptr": ("<I", 4), "u8": ("<B", 1),
          "f32": ("<f", 4), "i16": ("<h", 2)}


def load(path=TABLE_PATH):
    with open(path, encoding="utf-8-sig") as f:  # tolerate a BOM (PowerShell 5.1 writes one)
        return json.load(f)


def select_version(table, exe_sha256, genesis_sha256):
    for name, v in table["versions"].items():
        if v["exe_sha256"] == exe_sha256 and v["genesis_sha256"] == genesis_sha256:
            return name, v
    return None, None


class Resolver:
    """Resolves address-table names against a live pymem process."""

    def __init__(self, pm, version, module_bases):
        self.pm = pm
        self.entries = version["entries"]
        self.bases = module_bases  # {"exe": int, "genesis": int}

    def address(self, name):
        """Final address of an entry, or None if a pointer in the chain is null
        or unreadable (a bad chain must fail the assertion, not the harness)."""
        try:
            return self._address(name)
        except pymem.exception.MemoryReadError:
            return None

    def _address(self, name):
        e = self.entries[name]
        addr = self.bases[e["module"]] + int(e["rva"], 16)
        chain = [int(o, 16) for o in e.get("chain", [])]
        if not chain:
            return addr
        if e.get("deref_root") is False:
            # Root is a static struct, not a pointer: offsets index into it.
            return addr + sum(chain)
        p = self.pm.read_uint(addr)
        for off in chain[:-1]:
            if not p:
                return None
            p = self.pm.read_uint(p + off)
        return p + chain[-1] if p else None

    def field(self, name):
        """Offset of a struct-field entry (module "field")."""
        return int(self.entries[name]["rva"], 16)

    def victims(self):
        """Walk the level entity list; return [(name, x, y, z, spot0_x, spot0_z)] for every
        entity of kind 2 (victim), in list order."""
        rd = lambda a: struct.unpack("<I", self.pm.read_bytes(a, 4))[0]
        rf = lambda a: struct.unpack("<f", self.pm.read_bytes(a, 4))[0]
        out = []
        try:
            node = rd(self.address("entities.head"))
            for _ in range(10000):
                if not node:
                    break
                ent = rd(node + self.field("node.elem"))
                if struct.unpack("<i", self.pm.read_bytes(ent + self.field("entity.kind"), 4))[0] == 2:
                    p = ent + self.field("victim.pos")
                    name = ""
                    try:
                        s = rd(rd(ent + self.field("victim.def")) + self.field("def.name"))
                        name = self.pm.read_bytes(s, 32).split(b"\0", 1)[0].decode("latin-1")
                    except pymem.exception.MemoryReadError:
                        pass
                    spot0 = rd(rd(ent + self.field("victim.spots")))
                    out.append((name, rf(p), rf(p + 4), rf(p + 8), rf(spot0), rf(spot0 + 8)))
                node = rd(node + self.field("node.next"))
        except pymem.exception.MemoryReadError:
            pass
        return out

    def fires(self):
        """(total, burning, out) over entities of kind 6 (FireRescueEnt)."""
        rd = lambda a: struct.unpack("<I", self.pm.read_bytes(a, 4))[0]
        ri = lambda a: struct.unpack("<i", self.pm.read_bytes(a, 4))[0]
        total = burning = out = 0
        try:
            node = rd(self.address("entities.head"))
            for _ in range(20000):
                if not node:
                    break
                ent = rd(node + self.field("node.elem"))
                if ent and ri(ent + self.field("entity.kind")) == 6:
                    mode = ri(ent + self.field("fire.mode"))
                    total += 1
                    burning += mode == 4
                    out += mode == 5
                node = rd(node + self.field("node.next"))
        except pymem.exception.MemoryReadError:
            pass
        return total, burning, out

    def menu_screen(self):
        try:
            cur = self.read("menu.screen_current")
            lst = self.read("menu.screen_list")
            n = self.read("menu.screen_count")
            for i in range(n or 0):
                if struct.unpack("<I", self.pm.read_bytes(lst + 4 * i, 4))[0] == cur:
                    return i
        except (pymem.exception.MemoryReadError, TypeError):
            pass
        return -1

    def read(self, name):
        e = self.entries[name]
        addr = self.address(name)
        if addr is None:
            return None
        t = e["type"]
        try:
            return self._read_at(addr, t)
        except pymem.exception.MemoryReadError:
            return None

    def _read_at(self, addr, t):
        if t.startswith("str"):
            raw = self.pm.read_bytes(addr, int(t[3:]))
            return raw.split(b"\0", 1)[0].decode("latin-1")
        fmt, size = _TYPES[t]
        return struct.unpack(fmt, self.pm.read_bytes(addr, size))[0]
