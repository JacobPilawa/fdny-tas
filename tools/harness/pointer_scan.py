"""VM-only: find static pointer chains from known roots to a target address
in the running game (like Cheat Engine's pointer scan, small depth).

python -I pointer_scan.py <target_hex> [depth=3] [span_hex=0x400]
Prints chains as root -> +off -> +off ... that end exactly at <target>.
"""
import struct
import sys

import pymem

ROOTS = {"GameSession 0x00502AAC": 0x00502AAC, "g_everGame 0x00503A78": 0x00503A78,
         "g_mainWindow 0x00503A70": 0x00503A70, "g_console 0x00502A90": 0x00502A90}


def main():
    target = int(sys.argv[1], 16)
    depth = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    span = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x400
    pm = pymem.Pymem("FDNYFirefighter.exe")

    def block(addr):
        try:
            return pm.read_bytes(addr, span)
        except Exception:
            return None

    found = 0
    for name, root in ROOTS.items():
        start = struct.unpack("<I", pm.read_bytes(root, 4))[0]
        frontier = [(start, [])]
        for level in range(depth):
            nxt = []
            for base, path in frontier:
                b = block(base)
                if b is None:
                    continue
                for off in range(0, len(b) - 3, 4):
                    v = struct.unpack_from("<I", b, off)[0]
                    if v == target:
                        print(f"{name} -> " + " -> ".join(f"+{o:#x}" for o in path + [off]))
                        found += 1
                    elif level + 1 < depth and 0x10000 < v < 0x7FFF0000:
                        nxt.append((v, path + [off]))
            frontier = nxt
    print(f"{found} chain(s) to {target:#x}")


if __name__ == "__main__":
    main()
