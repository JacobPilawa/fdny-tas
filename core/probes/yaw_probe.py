"""Yaw convention probe for fd_juggle: for yaw 0 / 90 / 180 / 270, hold W (forward) and
then D (strafe right) briefly and log the world-space movement. Runs beside
tests/probes/probe_firejuggle.yaml (bank held open). Player pos +0x4C, yaw +0x3434.

python -I yaw_probe.py <out.log>
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from fdh import inputs  # noqa: E402

import pymem  # noqa: E402

out = open(sys.argv[1], "w", encoding="utf-8")


def log(m):
    print(m, flush=True)
    out.write(m + "\n")
    out.flush()


pm = None
for _ in range(600):
    try:
        pm = pymem.Pymem("FDNYFirefighter.exe")
        break
    except Exception:
        time.sleep(1)
u32 = lambda a: struct.unpack("<I", pm.read_bytes(a, 4))[0]
vec = lambda a: struct.unpack("<3f", pm.read_bytes(a, 12))


def mapname():
    try:
        return pm.read_bytes(u32(u32(u32(0x00502AAC) + 0x688)) + 0xC, 8).split(b"\0")[0].decode()
    except Exception:
        return ""


while mapname().lower() != "bank":
    time.sleep(1)
time.sleep(10)
pl = u32(u32(0x00503A78) + 0x30)
home = vec(pl + 0x4C)
# An open spot on bank's ground floor (spawn area); each trial starts here.
for yaw in (0.0, 90.0, 180.0, 270.0):
    for key in ("w", "d"):
        pm.write_bytes(pl + 0x4C, struct.pack("<3f", *home), 12)
        pm.write_bytes(pl + 0xFC, struct.pack("<3f", 0, 0, 0), 12)
        time.sleep(0.4)
        pm.write_bytes(pl + 0x3434, struct.pack("<f", yaw), 4)
        time.sleep(0.2)
        p0 = vec(pl + 0x4C)
        inputs.key_down(key)
        time.sleep(0.3)
        inputs.key_up(key)
        time.sleep(0.3)
        p1 = vec(pl + 0x4C)
        log(f"yaw {yaw:5.1f} key {key}: dx={p1[0] - p0[0]:+7.1f} dz={p1[2] - p0[2]:+7.1f} "
            f"(yaw now {struct.unpack('<f', pm.read_bytes(pl + 0x3434, 4))[0]:.1f})")
log("DONE")
