"""Address table access by name (core/addresses.json), shared with the core."""
import json
import os
import struct

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
        """Final address of an entry, or None if a pointer in the chain is null."""
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

    def read(self, name):
        e = self.entries[name]
        addr = self.address(name)
        if addr is None:
            return None
        t = e["type"]
        if t.startswith("str"):
            raw = self.pm.read_bytes(addr, int(t[3:]))
            return raw.split(b"\0", 1)[0].decode("latin-1")
        fmt, size = _TYPES[t]
        return struct.unpack(fmt, self.pm.read_bytes(addr, size))[0]
